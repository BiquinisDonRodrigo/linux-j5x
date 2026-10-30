// SPDX-License-Identifier: GPL-2.0-only
/*
 * Silicon Mitus SM5703 charger driver
 *
 * Status reporting only: the driver never writes to the charger, so the
 * charging parameters stay as programmed by the bootloader or the chip
 * defaults. Register layout and conversions follow the Samsung vendor
 * driver (drivers/battery/sm5703_charger.c).
 */

#include <linux/bitfield.h>
#include <linux/mfd/sm5703.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/property.h>
#include <linux/regmap.h>

struct sm5703_charger {
	struct regmap *regmap;
	struct power_supply *psy;
};

static int sm5703_charger_read(struct sm5703_charger *chg, unsigned int reg,
			       unsigned int *val)
{
	return regmap_read(chg->regmap, reg, val);
}

static int sm5703_charger_get_status(struct sm5703_charger *chg, int *val)
{
	unsigned int status3, status5;
	int ret;

	ret = sm5703_charger_read(chg, SM5703_REG_STATUS5, &status5);
	if (ret)
		return ret;

	ret = sm5703_charger_read(chg, SM5703_REG_STATUS3, &status3);
	if (ret)
		return ret;

	if (!(status5 & SM5703_STATUS5_VBUSOK))
		*val = POWER_SUPPLY_STATUS_DISCHARGING;
	else if (status3 & (SM5703_STATUS3_DONE | SM5703_STATUS3_TOPOFF))
		*val = POWER_SUPPLY_STATUS_FULL;
	else if (status3 & SM5703_STATUS3_CHGON)
		*val = POWER_SUPPLY_STATUS_CHARGING;
	else
		*val = POWER_SUPPLY_STATUS_NOT_CHARGING;

	return 0;
}

static int sm5703_charger_get_health(struct sm5703_charger *chg, int *val)
{
	unsigned int status5;
	int ret;

	ret = sm5703_charger_read(chg, SM5703_REG_STATUS5, &status5);
	if (ret)
		return ret;

	if (status5 & SM5703_STATUS5_VBUSOVP)
		*val = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
	else if (status5 & SM5703_STATUS5_VBUSUVLO)
		*val = POWER_SUPPLY_HEALTH_UNDERVOLTAGE;
	else
		*val = POWER_SUPPLY_HEALTH_GOOD;

	return 0;
}

/* Read a "100 mA + 50 mA * n" current field, in uA */
static int sm5703_charger_get_current(struct sm5703_charger *chg,
				      unsigned int reg, unsigned int mask,
				      int *val)
{
	unsigned int data;
	int ret;

	ret = sm5703_charger_read(chg, reg, &data);
	if (ret)
		return ret;

	*val = (100 + 50 * field_get(mask, data)) * 1000;

	return 0;
}

static int sm5703_charger_get_property(struct power_supply *psy,
				       enum power_supply_property psp,
				       union power_supply_propval *val)
{
	struct sm5703_charger *chg = power_supply_get_drvdata(psy);
	unsigned int data;
	int ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		ret = sm5703_charger_read(chg, SM5703_REG_STATUS5, &data);
		if (ret)
			return ret;
		val->intval = !!(data & SM5703_STATUS5_VBUSOK);
		return 0;
	case POWER_SUPPLY_PROP_STATUS:
		return sm5703_charger_get_status(chg, &val->intval);
	case POWER_SUPPLY_PROP_HEALTH:
		return sm5703_charger_get_health(chg, &val->intval);
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		return sm5703_charger_get_current(chg, SM5703_REG_VBUSCNTL,
						  SM5703_VBUSLIMIT_MASK,
						  &val->intval);
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		return sm5703_charger_get_current(chg, SM5703_REG_CHGCNTL2,
						  SM5703_FASTCHG_MASK,
						  &val->intval);
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
		ret = sm5703_charger_read(chg, SM5703_REG_CHGCNTL3, &data);
		if (ret)
			return ret;
		val->intval = (4120 + 10 * FIELD_GET(SM5703_BATREG_MASK, data)) *
			      1000;
		return 0;
	default:
		return -EINVAL;
	}
}

static const enum power_supply_property sm5703_charger_properties[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE,
};

static const struct power_supply_desc sm5703_charger_desc = {
	.name = "sm5703-charger",
	.type = POWER_SUPPLY_TYPE_USB,
	.properties = sm5703_charger_properties,
	.num_properties = ARRAY_SIZE(sm5703_charger_properties),
	.get_property = sm5703_charger_get_property,
};

static int sm5703_charger_probe(struct platform_device *pdev)
{
	struct sm5703_dev *sm5703 = dev_get_drvdata(pdev->dev.parent);
	struct device *dev = &pdev->dev;
	struct power_supply_config cfg = {};
	struct sm5703_charger *chg;

	chg = devm_kzalloc(dev, sizeof(*chg), GFP_KERNEL);
	if (!chg)
		return -ENOMEM;

	chg->regmap = sm5703->regmap;

	cfg.drv_data = chg;
	cfg.fwnode = dev_fwnode(dev->parent);

	chg->psy = devm_power_supply_register(dev, &sm5703_charger_desc, &cfg);
	if (IS_ERR(chg->psy))
		return dev_err_probe(dev, PTR_ERR(chg->psy),
				     "Failed to register power supply\n");

	return 0;
}

static const struct platform_device_id sm5703_charger_id[] = {
	{ "sm5703-charger" },
	{ }
};
MODULE_DEVICE_TABLE(platform, sm5703_charger_id);

static struct platform_driver sm5703_charger_driver = {
	.driver = {
		.name = "sm5703-charger",
	},
	.probe = sm5703_charger_probe,
	.id_table = sm5703_charger_id,
};
module_platform_driver(sm5703_charger_driver);

MODULE_DESCRIPTION("Silicon Mitus SM5703 charger driver");
MODULE_LICENSE("GPL");
