/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Silicon Mitus SM5703 multi-function device
 *
 * Register definitions follow the Samsung vendor kernel
 * (include/linux/battery/charger/sm5703_charger.h,
 * drivers/regulator/sm5703_regulator.c).
 */

#ifndef _SM5703_H
#define _SM5703_H

#include <linux/bits.h>

struct device;
struct gpio_desc;
struct regmap;

struct sm5703_dev {
	struct device *dev;
	struct regmap *regmap;
	struct gpio_desc *reset_gpio;
};

/* Registers */
#define SM5703_REG_CNTL				0x0c
#define SM5703_REG_VBUSCNTL			0x0d
#define SM5703_REG_CHGCNTL1			0x0e
#define SM5703_REG_CHGCNTL2			0x0f
#define SM5703_REG_CHGCNTL3			0x10
#define SM5703_REG_CHGCNTL4			0x11
#define SM5703_REG_CHGCNTL5			0x12
#define SM5703_REG_CHGCNTL6			0x13
#define SM5703_REG_LDO1				0x1a
#define SM5703_REG_LDO2				0x1b
#define SM5703_REG_LDO3				0x1c
#define SM5703_REG_BUCK				0x1d
#define SM5703_REG_DEVICE_ID			0x1e
#define SM5703_REG_OTGCURRENTCNTL		0x60
#define SM5703_REG_MAX				0xff

/* SM5703_REG_CNTL also holds the USBLDO enable bits */
#define SM5703_REG_USBLDO12			SM5703_REG_CNTL
#define SM5703_REG_EN_USBLDO1			BIT(6)
#define SM5703_REG_EN_USBLDO2			BIT(7)

#define SM5703_OPERATION_MODE_MASK		GENMASK(2, 0)
#define SM5703_OPERATION_MODE_SUSPEND		0x0
#define SM5703_OPERATION_MODE_CHARGING_OFF	0x4
#define SM5703_OPERATION_MODE_CHARGING_ON	0x5
#define SM5703_OPERATION_MODE_FLASH_BOOST_MODE	0x6
#define SM5703_OPERATION_MODE_USB_OTG_MODE	0x7

#define SM5703_LDO_EN				BIT(3)
#define SM5703_LDO_VOLT_MASK			GENMASK(2, 0)
#define SM5703_REG_EN_BUCK			BIT(6)
#define SM5703_BUCK_VOLT_MASK			GENMASK(4, 0)

#define SM5703_USBLDO_MICROVOLT			4800000
#define SM5703_VBUS_MICROVOLT			5000000

#endif /* _SM5703_H */
