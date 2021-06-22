/* LEDs driver for Altra SPCI
 *
 * Copyright (C) 2020 Ampere Computing LLC
 * Author: Dung Cao <dcao@os.amperecompting.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * This driver support control LED via SPCI interface on Altra system.
 * Currently, it is 2-LEDs system(Activity LED, Status LED). Activity LED is
 * completely controlled by hardware. Status LED is controlled by this driver.
 * There are 4 stages of Status LED:
 *     - ON
 *     - OFF
 *     - Locate(blink at 1Hz)
 *     - Rebuild(blink at 4Hz)
 * Example:
 * To On, Off the LED on slot 9:
 *     1. Write the seg:bus_number corresponding to LED on slot 9 to altra:led
 *          echo 000b:03 > /sys/class/leds/altra:led/address
 *     2. Write 1 to brightness attr to ON, 0 to brightness attr to OFF
 *          echo 1 > /sys/class/leds/altra:led/brightness
 *          echo 0 > /sys/class/leds/altra:led/brightness
 * To blink the LED on slot 9:
 *     1. Write the seg:bus_number corresponding to LED on slot 9 to altra:led
 *          echo 000b:03 > /sys/class/leds/altra:led/address
 *     2. Write 1 to blink attr to select Locate(blink 1Hz), or 4 to blink attr
 *        to select Rebuild(blink 4Hz)
 *          echo 1 > /sys/class/leds/altra:led/blink
 *          echo 4 > /sys/class/leds/altra:led/blink
 *     3. Write 1 to shot attr to execute blink
 *          echo 1 > /sys/class/leds/altra:led/shot
 */

#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/leds.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/acpi.h>
#include <linux/device.h>
#include <linux/arm-smccc.h>
#include <linux/types.h>
#include <uapi/linux/uleds.h>
#include <linux/bitfield.h>

/* LED miscellaneous functions */
#define LED_CMD			4	/* Control LED state */
#define	GETPORT_CMD		9	/* Get PCIE port */
#define LED_FAULT		1	/* LED type - Fault */
#define LED_ATT			2	/* LED type - Attention */
#define LED_SET_ON		1
#define LED_SET_OFF		2
#define LED_SET_BLINK_1HZ	3
#define LED_SET_BLINK_4HZ	4
#define BLINK_1HZ		1
#define BLINK_4HZ		4
#define ARG_SEG_MASK		GENMASK(3,0)
#define ARG_BUS_MASK		GENMASK(7,4)

/* Function id for SMC calling convention */
#define FUNCID_TYPE_SHIFT	31
#define FUNCID_CC_SHIFT		30
#define FUNCID_OEN_SHIFT	24
#define FUNCID_NUM_SHIFT	0
#define FUNCID_TYPE_MASK	0x1
#define FUNCID_CC_MASK		0x1
#define FUNCID_OEN_MASK		0x3f
#define FUNCID_NUM_MASK		0xffff
#define FUNCID_TYPE_WIDTH	1
#define FUNCID_CC_WIDTH		1
#define FUNCID_OEN_WIDTH	6
#define FUNCID_NUM_WIDTH	16
#define SMC_64			1
#define SMC_32			0
#define SMC_TYPE_FAST		1
#define SMC_TYPE_STD		0

/* SPCI error codes. */
#define SPCI_SUCCESS		0
#define SPCI_NOT_SUPPORTED	-1
#define SPCI_INVALID_PARAMETER	-2
#define SPCI_NO_MEMORY		-3
#define SPCI_BUSY		-4
#define SPCI_QUEUED		-5
#define SPCI_DENIED		-6
#define SPCI_NOT_PRESENT	-7

/* Client ID used for SPCI calls */
#define SPCI_CLIENT_ID		0x0000ACAC

/* This error code must be different to the ones used by SPCI */
#define SPCI_ERROR 		-42

/* Definitions to build the complete SMC ID */
#define SPCI_FID_MISC_FLAG	(0 << 27)
#define SPCI_FID_MISC_SHIFT	20
#define SPCI_FID_MISC_MASK 	0x7F
#define SPCI_FID_TUN_FLAG	BIT(27)
#define SPCI_FID_TUN_SHIFT	24
#define SPCI_FID_TUN_MASK	0x7
#define OEN_SPCI_START		0x30
#define OEN_SPCI_END		0x3F
#define ARG_RES_MASK		GENMASK(15,0)
#define ARG_HANDLE_MASK		GENMASK(31,16)
#define SPCI_SMC(spci_fid)	((OEN_SPCI_START << FUNCID_OEN_SHIFT) | \
				 (1 << 31) | (spci_fid))
#define SPCI_MISC_32(misc_fid)	((SMC_32 << FUNCID_CC_SHIFT) |	\
				 SPCI_FID_MISC_FLAG |		\
				 SPCI_SMC((misc_fid) << SPCI_FID_MISC_SHIFT))
#define SPCI_MISC_64(misc_fid)	((SMC_64 << FUNCID_CC_SHIFT) |	\
				 SPCI_FID_MISC_FLAG |		\
				 SPCI_SMC((misc_fid) << SPCI_FID_MISC_SHIFT))
#define SPCI_TUN_32(tun_fid)	((SMC_32 << FUNCID_CC_SHIFT) |	\
				 SPCI_FID_TUN_FLAG |		\
				 SPCI_SMC((tun_fid) << SPCI_FID_TUN_SHIFT))
#define SPCI_TUN_64(tun_fid)	((SMC_64 << FUNCID_CC_SHIFT) |	\
				 SPCI_FID_TUN_FLAG |		\
				 SPCI_SMC((tun_fid) << SPCI_FID_TUN_SHIFT))

/* SPCI miscellaneous functions */
#define SPCI_FID_VERSION 			0x0
#define SPCI_FID_SERVICE_HANDLE_OPEN		0x2
#define SPCI_FID_SERVICE_HANDLE_CLOSE		0x3
#define SPCI_FID_SERVICE_MEM_REGISTER		0x4
#define SPCI_FID_SERVICE_MEM_UNREGISTER		0x5
#define SPCI_FID_SERVICE_MEM_PUBLISH		0x6
#define SPCI_FID_SERVICE_REQUEST_BLOCKING	0x7
#define SPCI_FID_SERVICE_REQUEST_START		0x8
#define SPCI_FID_SERVICE_GET_RESPONSE		0x9
#define SPCI_FID_SERVICE_RESET_CLIENT_STATE	0xA

/* SPCI tunneling functions */
#define SPCI_FID_SERVICE_TUN_REQUEST_START	0x0
#define SPCI_FID_SERVICE_REQUEST_RESUME		0x1
#define SPCI_FID_SERVICE_TUN_REQUEST_BLOCKING	0x2

/* Complete SMC IDs and associated values */
#define SPCI_VERSION				SPCI_MISC_32(SPCI_FID_VERSION)
#define SPCI_SERVICE_HANDLE_OPEN		\
		SPCI_MISC_32(SPCI_FID_SERVICE_HANDLE_OPEN)
#define SPCI_SERVICE_HANDLE_OPEN_NOTIFY_BIT	1
#define SPCI_SERVICE_HANDLE_CLOSE		\
		SPCI_MISC_32(SPCI_FID_SERVICE_HANDLE_CLOSE)
#define SPCI_SERVICE_REQUEST_BLOCKING_AARCH32	\
		SPCI_MISC_32(SPCI_FID_SERVICE_REQUEST_BLOCKING)
#define SPCI_SERVICE_REQUEST_BLOCKING_AARCH64	\
		SPCI_MISC_64(SPCI_FID_SERVICE_REQUEST_BLOCKING)

struct spci_led {
	struct led_classdev cdev;
	u8 seg;
	u8 bus_num;
	u8 blink;
	u32 uuid[4];
};

static int spci_service_handle_open(u16 client_id, u16 *handle,
				    u32 uuid1, u32 uuid2, u32 uuid3, u32 uuid4)
{
	int ret;
	struct arm_smccc_res res;
	u32 x1;

	arm_smccc_smc(SPCI_SERVICE_HANDLE_OPEN, uuid1, uuid2, uuid3, uuid4,
		      0, 0, client_id, &res);

	ret = res.a0;
	if (ret != SPCI_SUCCESS) {
		return ret;
	}

	x1 = res.a1;

	if (FIELD_GET(ARG_RES_MASK, x1) != 0) {
		return SPCI_ERROR;
	}

	/* The handle is returned in the top 16 bits */
	*handle = FIELD_GET(ARG_HANDLE_MASK, x1);

	return SPCI_SUCCESS;
}

static int spci_service_handle_close(u16 client_id, u16 handle)
{
	struct arm_smccc_res res;

	arm_smccc_smc(SPCI_SERVICE_HANDLE_CLOSE, (client_id | (handle << 16)),
		      0, 0, 0, 0, 0, 0, &res);

	return (int) res.a0;
}

static int spci_service_request_locking(u64 x1, u64 x2, u64 x3, u64 x4,
				 u64 x5, u64 x6, u16 client_id, u16 handle,
				 u64 *rx1, u64 *rx2, u64 *rx3)
{
	struct arm_smccc_res res;
	int ret;

	arm_smccc_smc(SPCI_SERVICE_REQUEST_BLOCKING_AARCH64, x1, x2, x3, x4,
		      x5, x6, (client_id | (handle << 16)), &res);

	ret = res.a0;
	if (ret != SPCI_SUCCESS)
		return ret;

	if (rx1 != NULL)
		*rx1 = res.a1;
	if (rx2 != NULL)
		*rx2 = res.a2;
	if (rx2 != NULL)
		*rx3 = res.a3;

	return SPCI_SUCCESS;
}


static const struct acpi_device_id altra_spci_leds_acpi_match[] = {
	{"AMPC0008", 1},
	{ },
};
MODULE_DEVICE_TABLE(acpi, altra_spci_leds_acpi_match);

static void spci_led_set(struct led_classdev *cdev,
			 enum led_brightness value)
{
	struct spci_led *led = container_of(cdev, struct spci_led, cdev);
	u64 x1, x2, x3;
	u16 handle;
	int ret;

	if (!led) {
		dev_err(cdev->dev, "Failed to get resource\n");
		return;
	}

	ret = spci_service_handle_open(SPCI_CLIENT_ID, &handle,
				led->uuid[0], led->uuid[1],
				led->uuid[2], led->uuid[3]);
	if (ret != SPCI_SUCCESS) {
		dev_err(cdev->dev, "spci_service_handle_open has failure %d\n", ret);
		return;
	}

	x1 = value ? LED_SET_ON : LED_SET_OFF;
	x2 = LED_ATT;
	x3 = FIELD_PREP(ARG_SEG_MASK, led->seg) |
	     FIELD_PREP(ARG_BUS_MASK, led->bus_num);
	ret = spci_service_request_locking(LED_CMD, x1, x2,
					   x3, 0, 0, SPCI_CLIENT_ID, handle,
					   NULL, NULL, NULL);
	if (ret != SPCI_SUCCESS) {
		dev_err(cdev->dev, "spci_service_request_locking has failure %d\n", ret);
		return;
	}

	ret = spci_service_handle_close(SPCI_CLIENT_ID, handle);
	if (ret != SPCI_SUCCESS) {
		dev_err(cdev->dev, "spci_service_handle_close has failure %d\n", ret);
		return;
	}
}

static ssize_t led_shot(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct spci_led *led = dev_get_drvdata(dev);
	u64 x1, x2, x3;
	u16 handle;
	int ret;

	ret = spci_service_handle_open(SPCI_CLIENT_ID, &handle,
			led->uuid[0], led->uuid[1],
			led->uuid[2], led->uuid[3]);
	if (ret != SPCI_SUCCESS) {
		dev_err(dev, "spci_service_handle_open has failure %d\n", ret);
		return ret;
	}

	if (led->blink == BLINK_1HZ)
		x1 = LED_SET_BLINK_1HZ;
	else if (led->blink == BLINK_4HZ)
		x1 = LED_SET_BLINK_4HZ;
	else {
		dev_err(dev, "Wrong blink set mode\n");
		return -EINVAL;
	}
	x2 = LED_ATT;
	x3 = FIELD_PREP(ARG_SEG_MASK, led->seg) |
	     FIELD_PREP(ARG_BUS_MASK, led->bus_num);
	ret = spci_service_request_locking(LED_CMD, x1, x2,
					   x3, 0, 0, SPCI_CLIENT_ID, handle,
					   NULL, NULL, NULL);
	if (ret != SPCI_SUCCESS) {
		dev_err(dev, "spci_service_request_locking has failure %d\n", ret);
		return ret;
	}

	ret = spci_service_handle_close(SPCI_CLIENT_ID, handle);
	if (ret != SPCI_SUCCESS) {
		dev_err(dev, "spci_service_handle_close has failure %d\n", ret);
		return ret;
	}

	return size;
}

static ssize_t led_blink_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct spci_led *led = dev_get_drvdata(dev);

	return sprintf(buf, "%u\n", led->blink);
}

static ssize_t led_blink_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct spci_led *led = dev_get_drvdata(dev);
	unsigned long state;
	int ret;

	ret = kstrtoul(buf, 0, &state);
	if (ret)
		return ret;

	led->blink = state;

	return size;
}

static ssize_t led_address_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct spci_led *led = dev_get_drvdata(dev);

	return sprintf(buf, "%04x:%02u\n", led->seg, led->bus_num);
}

static ssize_t led_address_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct spci_led *led = dev_get_drvdata(dev);
	char *t, *q;
	char m[16];
	unsigned long seg, bus_num;

	strcpy(m, buf);
	q = m;
	t = strchr(q, ':');
	if (t == NULL) {
		dev_err(dev, "Invalid address format %s\n", q);
		return size;
	}
	*t = '\0';
	if (kstrtoul(q, 16, &seg)) {
		dev_err(dev, "Fail to convert string %s to unsigned\n", q);
		return size;
	}
	q = t + 1;
	if (kstrtoul(q, 16, &bus_num)) {
		dev_err(dev, "Fail to convert string %s to unsigned\n", q);
		return size;
	}
	led->seg = seg;
	led->bus_num = bus_num;

	return size;
}

static DEVICE_ATTR(blink, 0644, led_blink_show, led_blink_store);
static DEVICE_ATTR(address, 0644, led_address_show, led_address_store);
static DEVICE_ATTR(shot, 0200, NULL, led_shot);

static int spci_led_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct fwnode_handle *fwnode = NULL;
	struct spci_led *led_data;
	char name[LED_MAX_NAME_SIZE];
	int ret = 0;

	led_data = devm_kzalloc(dev, sizeof(struct spci_led), GFP_KERNEL);
	if (led_data == NULL)
		return -ENOMEM;

	fwnode = acpi_fwnode_handle(ACPI_COMPANION(dev));
	ret = fwnode_property_read_u32_array(fwnode, "uuid", led_data->uuid, 4);
	if (ret) {
		dev_err(dev, "Failed to get uuid\n");
		return ret;
	}
	snprintf(name, LED_MAX_NAME_SIZE, "altra:led");
	led_data->cdev.name = name;
	led_data->cdev.brightness_set = spci_led_set;
	ret = devm_led_classdev_register(dev, &led_data->cdev);
	if (ret < 0) {
		dev_err(dev, "Failed to register %s: %d\n", name, ret);
		return ret;
	}
	dev_set_drvdata(led_data->cdev.dev, led_data);
	if (device_create_file(led_data->cdev.dev, &dev_attr_shot))
		device_remove_file(led_data->cdev.dev, &dev_attr_shot);
	if (device_create_file(led_data->cdev.dev, &dev_attr_blink))
		device_remove_file(led_data->cdev.dev, &dev_attr_blink);
	if (device_create_file(led_data->cdev.dev, &dev_attr_address))
		device_remove_file(led_data->cdev.dev, &dev_attr_address);

	dev_info(dev, "Altra SPCI LED driver probed\n");

	return 0;
}

static struct platform_driver spci_led_driver = {
	.probe		= spci_led_probe,
	.driver		= {
		.name	= "leds-altra-spci",
		.acpi_match_table = ACPI_PTR(altra_spci_leds_acpi_match),
	},
};

module_platform_driver(spci_led_driver);

MODULE_AUTHOR("Dung Cao <dcao@os.amperecompting.com>");
MODULE_DESCRIPTION("Altra SPCI LED driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:leds-spci");
