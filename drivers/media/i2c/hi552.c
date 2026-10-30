// SPDX-License-Identifier: GPL-2.0-only
/*
 * SK Hynix Hi-552 5 megapixel camera sensor driver
 *
 * Found as the front camera ("SR552") of the Samsung Galaxy J5 2016.
 * The register sequence comes from the Samsung SM-J510FN kernel; the
 * driver structure follows other mainline sensor drivers (ov02c10, hi556).
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <media/v4l2-cci.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>

#include "hi552_regs.h"

#define HI552_REG_CHIP_ID		CCI_REG16(0x0f16)
#define HI552_CHIP_ID			0x0552

#define HI552_REG_MODE_SELECT		CCI_REG16(0x0a00)
#define HI552_MODE_STANDBY		0x0000
#define HI552_MODE_STREAMING		0x0100

#define HI552_REG_EXPOSURE		CCI_REG16(0x0004)
#define HI552_EXPOSURE_MIN		4
#define HI552_EXPOSURE_MAX_MARGIN	4
#define HI552_EXPOSURE_STEP		1

/* Frame length lines and line length (in pixel clocks) */
#define HI552_REG_FLL			CCI_REG16(0x0006)
#define HI552_FLL_MAX			0xffff
#define HI552_REG_LLP			CCI_REG16(0x0008)

#define HI552_MCLK			23880000UL
#define HI552_LINK_FREQ			440000000ULL
#define HI552_DATA_LANES		2
#define HI552_RGB_DEPTH			10

static const char * const hi552_supply_names[] = {
	"dovdd",	/* Digital I/O, 1.8V */
	"avdd",		/* Analog */
	"dvdd",		/* Digital core */
};

struct hi552_mode {
	u32 width;
	u32 height;
	u32 llp;
	u32 fll_def;
	const struct cci_reg_sequence *regs;
	unsigned int num_regs;
};

static const struct hi552_mode hi552_modes[] = {
	{
		.width = 2576,
		.height = 1932,
		.llp = 0x0b60,
		.fll_def = 0x07c2,
		.regs = hi552_init_regs,
		.num_regs = ARRAY_SIZE(hi552_init_regs),
	},
};

static const s64 hi552_link_freq_menu[] = {
	HI552_LINK_FREQ,
};

struct hi552 {
	struct device *dev;
	struct regmap *regmap;
	struct clk *mclk;
	struct gpio_desc *reset;
	struct regulator_bulk_data supplies[ARRAY_SIZE(hi552_supply_names)];

	struct v4l2_subdev sd;
	struct media_pad pad;
	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_ctrl *exposure;
	struct v4l2_ctrl *vblank;
	struct v4l2_ctrl *hblank;
};

static inline struct hi552 *to_hi552(struct v4l2_subdev *sd)
{
	return container_of(sd, struct hi552, sd);
}

static int hi552_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct hi552 *hi552 = container_of(ctrl->handler, struct hi552,
					   ctrl_handler);
	const struct hi552_mode *mode = &hi552_modes[0];
	int ret = 0;

	if (ctrl->id == V4L2_CID_VBLANK) {
		s64 exposure_max = mode->height + ctrl->val -
				   HI552_EXPOSURE_MAX_MARGIN;

		__v4l2_ctrl_modify_range(hi552->exposure,
					 hi552->exposure->minimum,
					 exposure_max, hi552->exposure->step,
					 min_t(s64, hi552->exposure->val,
					       exposure_max));
	}

	if (!pm_runtime_get_if_in_use(hi552->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		cci_write(hi552->regmap, HI552_REG_EXPOSURE, ctrl->val, &ret);
		break;
	case V4L2_CID_VBLANK:
		cci_write(hi552->regmap, HI552_REG_FLL,
			  mode->height + ctrl->val, &ret);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	pm_runtime_put(hi552->dev);

	return ret;
}

static const struct v4l2_ctrl_ops hi552_ctrl_ops = {
	.s_ctrl = hi552_set_ctrl,
};

static int hi552_init_controls(struct hi552 *hi552)
{
	struct v4l2_ctrl_handler *hdl = &hi552->ctrl_handler;
	const struct hi552_mode *mode = &hi552_modes[0];
	struct v4l2_fwnode_device_properties props;
	struct v4l2_ctrl *ctrl;
	s64 pixel_rate, exposure_max;
	u32 hblank;
	int ret;

	v4l2_ctrl_handler_init(hdl, 8);

	ctrl = v4l2_ctrl_new_int_menu(hdl, &hi552_ctrl_ops, V4L2_CID_LINK_FREQ,
				      ARRAY_SIZE(hi552_link_freq_menu) - 1, 0,
				      hi552_link_freq_menu);
	if (ctrl)
		ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	/* MIPI is DDR: two bits per lane per link clock */
	pixel_rate = div_u64(HI552_LINK_FREQ * 2 * HI552_DATA_LANES,
			     HI552_RGB_DEPTH);
	v4l2_ctrl_new_std(hdl, &hi552_ctrl_ops, V4L2_CID_PIXEL_RATE,
			  pixel_rate, pixel_rate, 1, pixel_rate);

	hi552->vblank = v4l2_ctrl_new_std(hdl, &hi552_ctrl_ops,
					  V4L2_CID_VBLANK,
					  mode->fll_def - mode->height,
					  HI552_FLL_MAX - mode->height, 1,
					  mode->fll_def - mode->height);

	hblank = mode->llp - mode->width;
	hi552->hblank = v4l2_ctrl_new_std(hdl, &hi552_ctrl_ops,
					  V4L2_CID_HBLANK, hblank, hblank, 1,
					  hblank);
	if (hi552->hblank)
		hi552->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	exposure_max = mode->fll_def - HI552_EXPOSURE_MAX_MARGIN;
	hi552->exposure = v4l2_ctrl_new_std(hdl, &hi552_ctrl_ops,
					    V4L2_CID_EXPOSURE,
					    HI552_EXPOSURE_MIN, exposure_max,
					    HI552_EXPOSURE_STEP, exposure_max);

	ret = v4l2_fwnode_device_parse(hi552->dev, &props);
	if (ret)
		goto error;

	v4l2_ctrl_new_fwnode_properties(hdl, &hi552_ctrl_ops, &props);

	if (hdl->error) {
		ret = hdl->error;
		goto error;
	}

	hi552->sd.ctrl_handler = hdl;

	return 0;

error:
	v4l2_ctrl_handler_free(hdl);
	return ret;
}

static void hi552_update_pad_format(const struct hi552_mode *mode,
				    struct v4l2_mbus_framefmt *fmt)
{
	fmt->width = mode->width;
	fmt->height = mode->height;
	fmt->code = MEDIA_BUS_FMT_SGBRG10_1X10;
	fmt->field = V4L2_FIELD_NONE;
	fmt->colorspace = V4L2_COLORSPACE_RAW;
	fmt->ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	fmt->quantization = V4L2_QUANTIZATION_FULL_RANGE;
	fmt->xfer_func = V4L2_XFER_FUNC_NONE;
}

static int hi552_enable_streams(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *state, u32 pad,
				u64 streams_mask)
{
	const struct hi552_mode *mode = &hi552_modes[0];
	struct hi552 *hi552 = to_hi552(sd);
	int ret;

	ret = pm_runtime_resume_and_get(hi552->dev);
	if (ret)
		return ret;

	ret = cci_multi_reg_write(hi552->regmap, mode->regs, mode->num_regs,
				  NULL);
	if (ret) {
		dev_err(hi552->dev, "failed to write mode registers: %d\n", ret);
		goto error;
	}

	ret = __v4l2_ctrl_handler_setup(hi552->sd.ctrl_handler);
	if (ret)
		goto error;

	ret = cci_write(hi552->regmap, HI552_REG_MODE_SELECT,
			HI552_MODE_STREAMING, NULL);
	if (ret)
		goto error;

	return 0;

error:
	pm_runtime_put(hi552->dev);
	return ret;
}

static int hi552_disable_streams(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *state, u32 pad,
				 u64 streams_mask)
{
	struct hi552 *hi552 = to_hi552(sd);

	cci_write(hi552->regmap, HI552_REG_MODE_SELECT, HI552_MODE_STANDBY,
		  NULL);
	pm_runtime_put(hi552->dev);

	return 0;
}

static int hi552_set_format(struct v4l2_subdev *sd,
			    struct v4l2_subdev_state *state,
			    struct v4l2_subdev_format *fmt)
{
	hi552_update_pad_format(&hi552_modes[0], &fmt->format);
	*v4l2_subdev_state_get_format(state, fmt->pad) = fmt->format;

	return 0;
}

static int hi552_enum_mbus_code(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *state,
				struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index > 0)
		return -EINVAL;

	code->code = MEDIA_BUS_FMT_SGBRG10_1X10;

	return 0;
}

static int hi552_enum_frame_size(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *state,
				 struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index >= ARRAY_SIZE(hi552_modes) ||
	    fse->code != MEDIA_BUS_FMT_SGBRG10_1X10)
		return -EINVAL;

	fse->min_width = hi552_modes[fse->index].width;
	fse->max_width = fse->min_width;
	fse->min_height = hi552_modes[fse->index].height;
	fse->max_height = fse->min_height;

	return 0;
}

static int hi552_init_state(struct v4l2_subdev *sd,
			    struct v4l2_subdev_state *state)
{
	hi552_update_pad_format(&hi552_modes[0],
				v4l2_subdev_state_get_format(state, 0));

	return 0;
}

static const struct v4l2_subdev_video_ops hi552_video_ops = {
	.s_stream = v4l2_subdev_s_stream_helper,
};

static const struct v4l2_subdev_pad_ops hi552_pad_ops = {
	.set_fmt = hi552_set_format,
	.get_fmt = v4l2_subdev_get_fmt,
	.enum_mbus_code = hi552_enum_mbus_code,
	.enum_frame_size = hi552_enum_frame_size,
	.enable_streams = hi552_enable_streams,
	.disable_streams = hi552_disable_streams,
};

static const struct v4l2_subdev_ops hi552_subdev_ops = {
	.video = &hi552_video_ops,
	.pad = &hi552_pad_ops,
};

static const struct media_entity_operations hi552_entity_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

static const struct v4l2_subdev_internal_ops hi552_internal_ops = {
	.init_state = hi552_init_state,
};

static int hi552_power_on(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct hi552 *hi552 = to_hi552(sd);
	int ret;

	/*
	 * Vendor sequence: DOVDD, AVDD (5 ms), DVDD (2 ms), MCLK (5 ms),
	 * then release reset.
	 */
	ret = regulator_enable(hi552->supplies[0].consumer);
	if (ret)
		return ret;
	usleep_range(1000, 2000);

	ret = regulator_enable(hi552->supplies[1].consumer);
	if (ret)
		goto disable_dovdd;
	usleep_range(5000, 6000);

	ret = regulator_enable(hi552->supplies[2].consumer);
	if (ret)
		goto disable_avdd;
	usleep_range(2000, 3000);

	ret = clk_prepare_enable(hi552->mclk);
	if (ret)
		goto disable_dvdd;
	usleep_range(5000, 6000);

	gpiod_set_value_cansleep(hi552->reset, 0);
	usleep_range(3000, 4000);

	return 0;

disable_dvdd:
	regulator_disable(hi552->supplies[2].consumer);
disable_avdd:
	regulator_disable(hi552->supplies[1].consumer);
disable_dovdd:
	regulator_disable(hi552->supplies[0].consumer);
	return ret;
}

static int hi552_power_off(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct hi552 *hi552 = to_hi552(sd);

	gpiod_set_value_cansleep(hi552->reset, 1);
	clk_disable_unprepare(hi552->mclk);
	regulator_disable(hi552->supplies[2].consumer);
	regulator_disable(hi552->supplies[1].consumer);
	regulator_disable(hi552->supplies[0].consumer);

	return 0;
}

static int hi552_identify(struct hi552 *hi552)
{
	u64 id;
	int ret;

	ret = cci_read(hi552->regmap, HI552_REG_CHIP_ID, &id, NULL);
	if (ret)
		return dev_err_probe(hi552->dev, ret, "failed to read chip id\n");

	if (id != HI552_CHIP_ID)
		return dev_err_probe(hi552->dev, -ENODEV,
				     "unexpected chip id 0x%04llx\n", id);

	return 0;
}

static int hi552_check_hwcfg(struct hi552 *hi552)
{
	struct v4l2_fwnode_endpoint bus_cfg = {
		.bus_type = V4L2_MBUS_CSI2_DPHY,
	};
	struct fwnode_handle *ep;
	unsigned long link_freq_bitmap;
	int ret;

	ep = fwnode_graph_get_endpoint_by_id(dev_fwnode(hi552->dev), 0, 0, 0);
	if (!ep)
		return dev_err_probe(hi552->dev, -EPROBE_DEFER,
				     "waiting for fwnode graph endpoint\n");

	ret = v4l2_fwnode_endpoint_alloc_parse(ep, &bus_cfg);
	fwnode_handle_put(ep);
	if (ret)
		return dev_err_probe(hi552->dev, ret, "failed to parse endpoint\n");

	if (bus_cfg.bus.mipi_csi2.num_data_lanes != HI552_DATA_LANES) {
		ret = dev_err_probe(hi552->dev, -EINVAL,
				    "only %d data lanes are supported\n",
				    HI552_DATA_LANES);
		goto out;
	}

	ret = v4l2_link_freq_to_bitmap(hi552->dev, bus_cfg.link_frequencies,
				       bus_cfg.nr_of_link_frequencies,
				       hi552_link_freq_menu,
				       ARRAY_SIZE(hi552_link_freq_menu),
				       &link_freq_bitmap);
out:
	v4l2_fwnode_endpoint_free(&bus_cfg);
	return ret;
}

static int hi552_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct hi552 *hi552;
	unsigned long freq;
	unsigned int i;
	int ret;

	hi552 = devm_kzalloc(dev, sizeof(*hi552), GFP_KERNEL);
	if (!hi552)
		return -ENOMEM;

	hi552->dev = dev;
	v4l2_i2c_subdev_init(&hi552->sd, client, &hi552_subdev_ops);

	ret = hi552_check_hwcfg(hi552);
	if (ret)
		return ret;

	hi552->mclk = devm_v4l2_sensor_clk_get(dev, NULL);
	if (IS_ERR(hi552->mclk))
		return dev_err_probe(dev, PTR_ERR(hi552->mclk),
				     "failed to get clock\n");

	/* The GCC can only approximate the vendor rate (23880597 Hz) */
	freq = clk_get_rate(hi552->mclk);
	if (abs_diff(freq, HI552_MCLK) > HI552_MCLK / 1000)
		return dev_err_probe(dev, -EINVAL,
				     "external clock %lu is not supported\n",
				     freq);

	for (i = 0; i < ARRAY_SIZE(hi552_supply_names); i++)
		hi552->supplies[i].supply = hi552_supply_names[i];

	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(hi552->supplies),
				      hi552->supplies);
	if (ret)
		return dev_err_probe(dev, ret, "failed to get supplies\n");

	hi552->reset = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(hi552->reset))
		return dev_err_probe(dev, PTR_ERR(hi552->reset),
				     "failed to get reset gpio\n");

	hi552->regmap = devm_cci_regmap_init_i2c(client, 16);
	if (IS_ERR(hi552->regmap))
		return PTR_ERR(hi552->regmap);

	ret = hi552_power_on(dev);
	if (ret)
		return dev_err_probe(dev, ret, "failed to power on\n");

	ret = hi552_identify(hi552);
	if (ret)
		goto error_power_off;

	ret = hi552_init_controls(hi552);
	if (ret)
		goto error_power_off;

	hi552->sd.internal_ops = &hi552_internal_ops;
	hi552->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	hi552->sd.entity.ops = &hi552_entity_ops;
	hi552->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;
	hi552->pad.flags = MEDIA_PAD_FL_SOURCE;

	ret = media_entity_pads_init(&hi552->sd.entity, 1, &hi552->pad);
	if (ret)
		goto error_ctrl_free;

	hi552->sd.state_lock = hi552->ctrl_handler.lock;
	ret = v4l2_subdev_init_finalize(&hi552->sd);
	if (ret)
		goto error_entity_cleanup;

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);

	ret = v4l2_async_register_subdev_sensor(&hi552->sd);
	if (ret)
		goto error_pm_disable;

	pm_runtime_idle(dev);

	return 0;

error_pm_disable:
	pm_runtime_disable(dev);
	pm_runtime_set_suspended(dev);
	v4l2_subdev_cleanup(&hi552->sd);
error_entity_cleanup:
	media_entity_cleanup(&hi552->sd.entity);
error_ctrl_free:
	v4l2_ctrl_handler_free(&hi552->ctrl_handler);
error_power_off:
	hi552_power_off(dev);
	return ret;
}

static void hi552_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct hi552 *hi552 = to_hi552(sd);

	v4l2_async_unregister_subdev(sd);
	v4l2_subdev_cleanup(sd);
	media_entity_cleanup(&sd->entity);
	v4l2_ctrl_handler_free(&hi552->ctrl_handler);

	pm_runtime_disable(hi552->dev);
	if (!pm_runtime_status_suspended(hi552->dev))
		hi552_power_off(hi552->dev);
	pm_runtime_set_suspended(hi552->dev);
}

static DEFINE_RUNTIME_DEV_PM_OPS(hi552_pm_ops, hi552_power_off,
				 hi552_power_on, NULL);

static const struct of_device_id hi552_of_match[] = {
	{ .compatible = "hynix,hi552" },
	{ }
};
MODULE_DEVICE_TABLE(of, hi552_of_match);

static struct i2c_driver hi552_driver = {
	.driver = {
		.name = "hi552",
		.of_match_table = hi552_of_match,
		.pm = pm_ptr(&hi552_pm_ops),
	},
	.probe = hi552_probe,
	.remove = hi552_remove,
};
module_i2c_driver(hi552_driver);

MODULE_DESCRIPTION("SK Hynix Hi-552 camera sensor driver");
MODULE_LICENSE("GPL");
