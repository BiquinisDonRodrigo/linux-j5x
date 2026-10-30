// SPDX-License-Identifier: GPL-2.0-only
/*
 * Silicon Mitus SM5703 flash LED driver (torch mode)
 *
 * The flash LED driver shares the boost converter with the charger. Like the
 * Samsung vendor driver, keep the charger in "charging on" mode while VBUS is
 * present and only switch to the flash boost mode when running from battery.
 */

#include <linux/leds.h>
#include <linux/mfd/sm5703.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

/* Movie mode current is 10 mA + 10 mA * code; cap at the vendor 170 mA */
#define SM5703_TORCH_MAX_BRIGHTNESS	16

struct sm5703_flash {
	struct regmap *regmap;
	struct led_classdev cdev;
	struct mutex lock;	/* serialises mode changes */
	bool boost;		/* charger switched to flash boost mode */
};

static int sm5703_torch_on(struct sm5703_flash *flash, unsigned int code)
{
	unsigned int status5;
	bool vbus;
	int ret;

	ret = regmap_read(flash->regmap, SM5703_REG_STATUS5, &status5);
	if (ret)
		return ret;
	vbus = status5 & SM5703_STATUS5_VBUSOK;

	ret = regmap_update_bits(flash->regmap, SM5703_REG_OTGCURRENTCNTL,
				 SM5703_OTGCURRENT_MASK,
				 vbus ? SM5703_OTGCURRENT_0P9A :
					SM5703_OTGCURRENT_0P5A);
	if (ret)
		return ret;

	ret = regmap_update_bits(flash->regmap, SM5703_REG_FLEDCNTL6,
				 SM5703_BSTOUT_MASK, SM5703_BSTOUT_4P5V);
	if (ret)
		return ret;

	if (!vbus && !flash->boost) {
		ret = regmap_update_bits(flash->regmap, SM5703_REG_CNTL,
					 SM5703_OPERATION_MODE_MASK,
					 SM5703_OPERATION_MODE_FLASH_BOOST_MODE);
		if (ret)
			return ret;
		flash->boost = true;
	}

	ret = regmap_update_bits(flash->regmap, SM5703_REG_FLEDCNTL4,
				 SM5703_IMLED_MASK, code);
	if (ret)
		return ret;

	return regmap_update_bits(flash->regmap, SM5703_REG_FLEDCNTL1,
				  SM5703_FLEDEN_MASK, SM5703_FLEDEN_MOVIE);
}

static int sm5703_torch_off(struct sm5703_flash *flash)
{
	int ret;

	ret = regmap_update_bits(flash->regmap, SM5703_REG_FLEDCNTL1,
				 SM5703_FLEDEN_MASK, SM5703_FLEDEN_DISABLE);
	if (ret)
		return ret;

	if (flash->boost) {
		ret = regmap_update_bits(flash->regmap, SM5703_REG_CNTL,
					 SM5703_OPERATION_MODE_MASK,
					 SM5703_OPERATION_MODE_CHARGING_ON);
		if (ret)
			return ret;
		flash->boost = false;
	}

	return 0;
}

static int sm5703_torch_set(struct led_classdev *cdev,
			    enum led_brightness brightness)
{
	struct sm5703_flash *flash = container_of(cdev, struct sm5703_flash,
						  cdev);
	int ret;

	mutex_lock(&flash->lock);
	if (brightness == LED_OFF)
		ret = sm5703_torch_off(flash);
	else
		ret = sm5703_torch_on(flash, brightness);
	mutex_unlock(&flash->lock);

	return ret;
}

static int sm5703_flash_probe(struct platform_device *pdev)
{
	struct sm5703_dev *sm5703 = dev_get_drvdata(pdev->dev.parent);
	struct device *dev = &pdev->dev;
	struct sm5703_flash *flash;
	int ret;

	flash = devm_kzalloc(dev, sizeof(*flash), GFP_KERNEL);
	if (!flash)
		return -ENOMEM;

	flash->regmap = sm5703->regmap;
	platform_set_drvdata(pdev, flash);

	ret = devm_mutex_init(dev, &flash->lock);
	if (ret)
		return ret;

	/* LED off, safety timer enabled (1.6 s), as set by the vendor driver */
	ret = regmap_write(flash->regmap, SM5703_REG_FLEDCNTL1,
			   SM5703_FLEDCNTL1_DEFAULT);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to initialise LED\n");

	flash->cdev.name = "white:torch";
	flash->cdev.max_brightness = SM5703_TORCH_MAX_BRIGHTNESS;
	flash->cdev.brightness_set_blocking = sm5703_torch_set;
	flash->cdev.flags = LED_CORE_SUSPENDRESUME;

	return devm_led_classdev_register(dev, &flash->cdev);
}

static void sm5703_flash_remove(struct platform_device *pdev)
{
	struct sm5703_flash *flash = platform_get_drvdata(pdev);

	if (flash) {
		mutex_lock(&flash->lock);
		sm5703_torch_off(flash);
		mutex_unlock(&flash->lock);
	}
}

static const struct platform_device_id sm5703_flash_id[] = {
	{ "sm5703-flash" },
	{ }
};
MODULE_DEVICE_TABLE(platform, sm5703_flash_id);

static struct platform_driver sm5703_flash_driver = {
	.driver = {
		.name = "sm5703-flash",
	},
	.probe = sm5703_flash_probe,
	.remove = sm5703_flash_remove,
	.id_table = sm5703_flash_id,
};
module_platform_driver(sm5703_flash_driver);

MODULE_DESCRIPTION("Silicon Mitus SM5703 flash LED driver");
MODULE_LICENSE("GPL");
