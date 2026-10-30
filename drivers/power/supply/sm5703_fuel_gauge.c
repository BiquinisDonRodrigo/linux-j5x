// SPDX-License-Identifier: GPL-2.0-only
/*
 * Silicon Mitus SM5703 fuel gauge driver
 *
 * Register layout and conversions follow the Samsung vendor driver
 * (drivers/battery/sm5703_fuelgauge.c). The driver is read-only: it does
 * not load battery parameters or write calibration registers, so it relies
 * on the fuel gauge having been initialised before (e.g. by the vendor
 * firmware on a previous boot).
 */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/i2c.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/power_supply.h>
#include <linux/regmap.h>

#define SM5703_FG_REG_DEVICE_ID		0x00
#define SM5703_FG_REG_SOC		0x05
#define SM5703_FG_REG_OCV		0x06
#define SM5703_FG_REG_VOLTAGE		0x07
#define SM5703_FG_REG_CURRENT		0x08
#define SM5703_FG_REG_OP_STATUS		0x10

#define SM5703_FG_OP_STATUS_INIT_MASK	GENMASK(7, 0)
#define SM5703_FG_OP_STATUS_INIT_DONE	0x07

/* Voltage/current: bits 10:8 are whole units, bits 7:0 are 1/256 units */
#define SM5703_FG_INT_MASK		GENMASK(10, 8)
#define SM5703_FG_FRAC_MASK		GENMASK(7, 0)
#define SM5703_FG_SIGN			BIT(15)

/* SoC: bits 15:8 are whole percent, bits 7:0 are 1/256 percent */
#define SM5703_FG_SOC_INT_MASK		GENMASK(15, 8)

/* Current threshold for deriving the status without a charger driver */
#define SM5703_FG_STATUS_THRESHOLD_UA	20000

struct sm5703_fg {
	struct regmap *regmap;
	struct power_supply *psy;
};

static const struct regmap_config sm5703_fg_regmap_config = {
	.reg_bits = 8,
	.val_bits = 16,
	.val_format_endian = REGMAP_ENDIAN_LITTLE,
	.max_register = 0xff,
};

static int sm5703_fg_read_fixed(struct sm5703_fg *fg, unsigned int reg,
				bool is_signed, int *val)
{
	unsigned int raw;
	int ret;

	ret = regmap_read(fg->regmap, reg, &raw);
	if (ret)
		return ret;

	/* Convert to micro units */
	*val = FIELD_GET(SM5703_FG_INT_MASK, raw) * 1000000 +
	       FIELD_GET(SM5703_FG_FRAC_MASK, raw) * 1000000 / 256;
	if (is_signed && (raw & SM5703_FG_SIGN))
		*val = -*val;

	return 0;
}

static int sm5703_fg_read_capacity(struct sm5703_fg *fg, int *val)
{
	unsigned int raw;
	int ret;

	ret = regmap_read(fg->regmap, SM5703_FG_REG_SOC, &raw);
	if (ret)
		return ret;

	*val = DIV_ROUND_CLOSEST(raw, 256);
	*val = min(*val, 100);

	return 0;
}

static int sm5703_fg_get_status(struct sm5703_fg *fg, int *val)
{
	int current_ua, capacity;
	int ret;

	ret = sm5703_fg_read_fixed(fg, SM5703_FG_REG_CURRENT, true,
				   &current_ua);
	if (ret)
		return ret;

	if (current_ua > SM5703_FG_STATUS_THRESHOLD_UA) {
		*val = POWER_SUPPLY_STATUS_CHARGING;
		return 0;
	}

	if (current_ua < -SM5703_FG_STATUS_THRESHOLD_UA) {
		*val = POWER_SUPPLY_STATUS_DISCHARGING;
		return 0;
	}

	ret = sm5703_fg_read_capacity(fg, &capacity);
	if (ret)
		return ret;

	*val = capacity == 100 ? POWER_SUPPLY_STATUS_FULL :
				 POWER_SUPPLY_STATUS_NOT_CHARGING;

	return 0;
}

static int sm5703_fg_get_property(struct power_supply *psy,
				  enum power_supply_property psp,
				  union power_supply_propval *val)
{
	struct sm5703_fg *fg = power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		return sm5703_fg_get_status(fg, &val->intval);
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = 1;
		return 0;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
		return 0;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		return sm5703_fg_read_fixed(fg, SM5703_FG_REG_VOLTAGE, false,
					    &val->intval);
	case POWER_SUPPLY_PROP_VOLTAGE_OCV:
		return sm5703_fg_read_fixed(fg, SM5703_FG_REG_OCV, false,
					    &val->intval);
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		return sm5703_fg_read_fixed(fg, SM5703_FG_REG_CURRENT, true,
					    &val->intval);
	case POWER_SUPPLY_PROP_CAPACITY:
		return sm5703_fg_read_capacity(fg, &val->intval);
	default:
		return -EINVAL;
	}
}

static const enum power_supply_property sm5703_fg_properties[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_OCV,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
};

static const struct power_supply_desc sm5703_fg_desc = {
	.name = "sm5703-battery",
	.type = POWER_SUPPLY_TYPE_BATTERY,
	.properties = sm5703_fg_properties,
	.num_properties = ARRAY_SIZE(sm5703_fg_properties),
	.get_property = sm5703_fg_get_property,
};

static int sm5703_fg_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct power_supply_config cfg = {};
	struct sm5703_fg *fg;
	unsigned int id, status;
	int ret;

	fg = devm_kzalloc(dev, sizeof(*fg), GFP_KERNEL);
	if (!fg)
		return -ENOMEM;

	fg->regmap = devm_regmap_init_i2c(client, &sm5703_fg_regmap_config);
	if (IS_ERR(fg->regmap))
		return dev_err_probe(dev, PTR_ERR(fg->regmap),
				     "failed to initialise regmap\n");

	ret = regmap_read(fg->regmap, SM5703_FG_REG_DEVICE_ID, &id);
	if (ret)
		return dev_err_probe(dev, ret, "failed to read device id\n");

	ret = regmap_read(fg->regmap, SM5703_FG_REG_OP_STATUS, &status);
	if (ret)
		return dev_err_probe(dev, ret, "failed to read status\n");

	dev_info(dev, "device id 0x%04x, op status 0x%04x\n", id, status);
	if (FIELD_GET(SM5703_FG_OP_STATUS_INIT_MASK, status) !=
	    SM5703_FG_OP_STATUS_INIT_DONE)
		dev_warn(dev, "fuel gauge not initialised, capacity may be inaccurate\n");

	cfg.drv_data = fg;
	cfg.fwnode = dev_fwnode(dev);

	fg->psy = devm_power_supply_register(dev, &sm5703_fg_desc, &cfg);
	if (IS_ERR(fg->psy))
		return dev_err_probe(dev, PTR_ERR(fg->psy),
				     "failed to register power supply\n");

	return 0;
}

static const struct of_device_id sm5703_fg_of_match[] = {
	{ .compatible = "siliconmitus,sm5703-fg" },
	{ }
};
MODULE_DEVICE_TABLE(of, sm5703_fg_of_match);

static const struct i2c_device_id sm5703_fg_id[] = {
	{ "sm5703-fg" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, sm5703_fg_id);

static struct i2c_driver sm5703_fg_driver = {
	.driver = {
		.name = "sm5703-fuel-gauge",
		.of_match_table = sm5703_fg_of_match,
	},
	.probe = sm5703_fg_probe,
	.id_table = sm5703_fg_id,
};
module_i2c_driver(sm5703_fg_driver);

MODULE_DESCRIPTION("Silicon Mitus SM5703 fuel gauge driver");
MODULE_LICENSE("GPL");
