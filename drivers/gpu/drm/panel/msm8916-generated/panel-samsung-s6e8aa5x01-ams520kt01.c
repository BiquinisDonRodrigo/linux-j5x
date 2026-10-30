// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2025 FIXME
// Generated with linux-mdss-dsi-panel-driver-generator from vendor device tree:
//   Copyright (c) 2013, The Linux Foundation. All rights reserved. (FIXME)

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>

struct s6e8aa5x01_ams520kt01 {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct regulator_bulk_data *supplies;
	struct gpio_desc *reset_gpio;
};

/*
 * Brightness is controlled through AID (AMOLED impulsive driving), which sets
 * how many of the 1304 lines of a frame the panel stays dark, while keeping
 * the 360 cd/m2 reference gamma written in the init sequence. The vendor
 * kernel limits the AID value to 15..1262 lines (1.15%..96.78%).
 */
#define AMS520KT01_AID_LINES_TOTAL	1304
#define AMS520KT01_AID_MIN		15
#define AMS520KT01_AID_MAX		1262
#define AMS520KT01_MAX_BRIGHTNESS	(AMS520KT01_AID_MAX - AMS520KT01_AID_MIN)
#define AMS520KT01_MAX_LUMINANCE	360

/* ELVSS settings per luminance, from the vendor smart ACL ELVSS map */
static const struct {
	u16 max_luminance;
	u8 elvss[2];
} s6e8aa5x01_ams520kt01_elvss[] = {
	{  20, { 0xac, 0x0f } },
	{  21, { 0xac, 0x11 } },
	{  22, { 0xac, 0x13 } },
	{  24, { 0xac, 0x15 } },
	{  25, { 0xac, 0x17 } },
	{  27, { 0xac, 0x19 } },
	{  29, { 0xac, 0x1c } },
	{  39, { 0xac, 0x1f } },
	{  72, { 0xbc, 0x1f } },
	{  98, { 0xbc, 0x1e } },
	{ 111, { 0xbc, 0x1d } },
	{ 172, { 0xbc, 0x1c } },
	{ 183, { 0xbc, 0x1b } },
	{ 207, { 0xbc, 0x19 } },
	{ 220, { 0xbc, 0x18 } },
	{ 234, { 0xbc, 0x17 } },
	{ 249, { 0xbc, 0x16 } },
	{ 265, { 0xbc, 0x15 } },
	{ 282, { 0xbc, 0x14 } },
	{ 300, { 0xbc, 0x13 } },
	{ 316, { 0xbc, 0x12 } },
	{ 333, { 0xbc, 0x11 } },
	{ 360, { 0xbc, 0x0f } },
};

static const struct regulator_bulk_data s6e8aa5x01_ams520kt01_supplies[] = {
	{ .supply = "vdd3" },
	{ .supply = "vci" },
};

static inline
struct s6e8aa5x01_ams520kt01 *to_s6e8aa5x01_ams520kt01(struct drm_panel *panel)
{
	return container_of(panel, struct s6e8aa5x01_ams520kt01, panel);
}

static void s6e8aa5x01_ams520kt01_reset(struct s6e8aa5x01_ams520kt01 *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(5000, 6000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(1000, 2000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(10000, 11000);
}

static int s6e8aa5x01_ams520kt01_on(struct s6e8aa5x01_ams520kt01 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	ctx->dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0x5a, 0x5a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xcc, 0x4c);
	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 20);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xca,
				     0x01, 0x00, 0x01, 0x00, 0x01, 0x00, 0x80,
				     0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
				     0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
				     0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
				     0x80, 0x80, 0x00, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb2, 0x04, 0xee);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb6, 0xac, 0x0f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf7, 0x03);
	mipi_dsi_usleep_range(&dsi_ctx, 16000, 17000);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc0, 0xd8, 0xd8, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0x06);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb8, 0xa8);
	mipi_dsi_msleep(&dsi_ctx, 120);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0xa5, 0xa5);
	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);

	return dsi_ctx.accum_err;
}

static int s6e8aa5x01_ams520kt01_bl_update_status(struct backlight_device *bl)
{
	struct s6e8aa5x01_ams520kt01 *ctx = bl_get_data(bl);
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };
	u16 brightness = backlight_get_brightness(bl);
	unsigned int luminance;
	u16 aid;
	int i;

	if (!ctx->panel.enabled)
		return 0;

	aid = AMS520KT01_AID_MAX - brightness;
	luminance = DIV_ROUND_UP(AMS520KT01_MAX_LUMINANCE *
				 (AMS520KT01_AID_LINES_TOTAL - aid),
				 AMS520KT01_AID_LINES_TOTAL);

	for (i = 0; i < ARRAY_SIZE(s6e8aa5x01_ams520kt01_elvss) - 1; i++)
		if (luminance <= s6e8aa5x01_ams520kt01_elvss[i].max_luminance)
			break;

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0x5a, 0x5a);
	mipi_dsi_dcs_write_var_seq_multi(&dsi_ctx, 0xb2, aid >> 8, aid & 0xff);
	mipi_dsi_dcs_write_var_seq_multi(&dsi_ctx, 0xb6,
					 s6e8aa5x01_ams520kt01_elvss[i].elvss[0],
					 s6e8aa5x01_ams520kt01_elvss[i].elvss[1]);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf7, 0x03);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0xa5, 0xa5);

	return dsi_ctx.accum_err;
}

static const struct backlight_ops s6e8aa5x01_ams520kt01_bl_ops = {
	.update_status = s6e8aa5x01_ams520kt01_bl_update_status,
};

static struct backlight_device *
s6e8aa5x01_ams520kt01_create_backlight(struct s6e8aa5x01_ams520kt01 *ctx)
{
	struct device *dev = &ctx->dsi->dev;
	const struct backlight_properties props = {
		.type = BACKLIGHT_RAW,
		.scale = BACKLIGHT_SCALE_LINEAR,
		.brightness = AMS520KT01_MAX_BRIGHTNESS / 2,
		.max_brightness = AMS520KT01_MAX_BRIGHTNESS,
	};

	return devm_backlight_device_register(dev, dev_name(dev), dev, ctx,
					      &s6e8aa5x01_ams520kt01_bl_ops,
					      &props);
}

static int s6e8aa5x01_ams520kt01_off(struct s6e8aa5x01_ams520kt01 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	ctx->dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);

	return dsi_ctx.accum_err;
}

static int s6e8aa5x01_ams520kt01_prepare(struct drm_panel *panel)
{
	struct s6e8aa5x01_ams520kt01 *ctx = to_s6e8aa5x01_ams520kt01(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(s6e8aa5x01_ams520kt01_supplies), ctx->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators: %d\n", ret);
		return ret;
	}

	s6e8aa5x01_ams520kt01_reset(ctx);

	ret = s6e8aa5x01_ams520kt01_on(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		regulator_bulk_disable(ARRAY_SIZE(s6e8aa5x01_ams520kt01_supplies), ctx->supplies);
		return ret;
	}

	return 0;
}

static int s6e8aa5x01_ams520kt01_unprepare(struct drm_panel *panel)
{
	struct s6e8aa5x01_ams520kt01 *ctx = to_s6e8aa5x01_ams520kt01(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = s6e8aa5x01_ams520kt01_off(ctx);
	if (ret < 0)
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(s6e8aa5x01_ams520kt01_supplies), ctx->supplies);

	return 0;
}

static const struct drm_display_mode s6e8aa5x01_ams520kt01_mode = {
	.clock = (720 + 224 + 20 + 100) * (1280 + 14 + 2 + 8) * 60 / 1000,
	.hdisplay = 720,
	.hsync_start = 720 + 224,
	.hsync_end = 720 + 224 + 20,
	.htotal = 720 + 224 + 20 + 100,
	.vdisplay = 1280,
	.vsync_start = 1280 + 14,
	.vsync_end = 1280 + 14 + 2,
	.vtotal = 1280 + 14 + 2 + 8,
	.width_mm = 65,
	.height_mm = 115,
	.type = DRM_MODE_TYPE_DRIVER,
};

static int s6e8aa5x01_ams520kt01_get_modes(struct drm_panel *panel,
					   struct drm_connector *connector)
{
	return drm_connector_helper_get_modes_fixed(connector, &s6e8aa5x01_ams520kt01_mode);
}

static const struct drm_panel_funcs s6e8aa5x01_ams520kt01_panel_funcs = {
	.prepare = s6e8aa5x01_ams520kt01_prepare,
	.unprepare = s6e8aa5x01_ams520kt01_unprepare,
	.get_modes = s6e8aa5x01_ams520kt01_get_modes,
};

static int s6e8aa5x01_ams520kt01_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct s6e8aa5x01_ams520kt01 *ctx;
	int ret;

	ctx = devm_drm_panel_alloc(dev, struct s6e8aa5x01_ams520kt01, panel,
				   &s6e8aa5x01_ams520kt01_panel_funcs,
				   DRM_MODE_CONNECTOR_DSI);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	ret = devm_regulator_bulk_get_const(dev,
					    ARRAY_SIZE(s6e8aa5x01_ams520kt01_supplies),
					    s6e8aa5x01_ams520kt01_supplies,
					    &ctx->supplies);
	if (ret < 0)
		return ret;

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio),
				     "Failed to get reset-gpios\n");

	ctx->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	ctx->panel.backlight = s6e8aa5x01_ams520kt01_create_backlight(ctx);
	if (IS_ERR(ctx->panel.backlight))
		return dev_err_probe(dev, PTR_ERR(ctx->panel.backlight),
				     "Failed to create backlight\n");

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
			  MIPI_DSI_MODE_VIDEO_NO_HFP;

	ctx->panel.prepare_prev_first = true;

	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		drm_panel_remove(&ctx->panel);
		return dev_err_probe(dev, ret, "Failed to attach to DSI host\n");
	}

	return 0;
}

static void s6e8aa5x01_ams520kt01_remove(struct mipi_dsi_device *dsi)
{
	struct s6e8aa5x01_ams520kt01 *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id s6e8aa5x01_ams520kt01_of_match[] = {
	{ .compatible = "samsung,s6e8aa5x01-ams520kt01" }, // FIXME
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, s6e8aa5x01_ams520kt01_of_match);

static struct mipi_dsi_driver s6e8aa5x01_ams520kt01_driver = {
	.probe = s6e8aa5x01_ams520kt01_probe,
	.remove = s6e8aa5x01_ams520kt01_remove,
	.driver = {
		.name = "panel-samsung-s6e8aa5x01-ams520kt01",
		.of_match_table = s6e8aa5x01_ams520kt01_of_match,
	},
};
module_mipi_dsi_driver(s6e8aa5x01_ams520kt01_driver);

MODULE_AUTHOR("linux-mdss-dsi-panel-driver-generator <fix@me>"); // FIXME
MODULE_DESCRIPTION("DRM driver for ss_dsi_panel_S6E8AA5X01_AMS520KT01_HD");
MODULE_LICENSE("GPL");
