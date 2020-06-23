// SPDX-License-Identifier: GPL-2.0
/* Ampere Altra SoC Hardware Monitoring Driver
 *
 * Copyright (C) 2020 Ampere Computing LLC
 * Author: Loc Ho <loc.ho@os.amperecompting.com>
 *         Hoan Tran <hoan@os.amperecomputing.com>
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
 */
#include <linux/acpi.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/hwmon.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/types.h>

#define DRVNAME				"altra_hwmon"
#define ALTRA_HWMON_VER1		1
#define ALTRA_HWMON_VER2		2

#define HW_SUPPORTED_VER		1

#define UNIT_DEGREE_CELSIUS		0x0001
#define UNIT_JOULE			0x0010
#define UNIT_MILLI_JOULE		0x0011
#define UNIT_MICRO_JOULE		0x0012

#define HW_METRIC_LABEL_REG		0x0000
#define HW_METRIC_LABEL_SIZE		16
#define HW_METRIC_INFO_REG		0x0010
#define HW_METRIC_INFO_UNIT_RD(x)	((x) & 0xFF)
#define HW_METRIC_INFO_DATASIZE_RD(x)	(((x) >> 8) & 0xFF)
#define HW_METRIC_INFO_DATACNT_RD(x)	(((x) >> 16) & 0xFFFF)
#define HW_METRIC_DATA_REG		0x0018
#define HW_METRIC_HDRSIZE		24

#define HW_METRICS_ID_REG		0x0000
#define HW_METRICS_ID			0x304D5748 /* HWM0 */
#define HW_METRICS_INFO_REG		0x0004
#define HW_METRICS_INFO_VER_RD(x)	((x) & 0xFFFF)
#define HW_METRICS_INFO_CNT_RD(x)	(((x) >> 16) & 0xFFFF)
#define HW_METRICS_DATA_REG		0x0008
#define HW_METRICS_HDRSIZE		8

#define SENSOR_ITEM_LABEL_SIZE		(HW_METRIC_LABEL_SIZE + 3 + 1)

struct sensor_item {
	char label[SENSOR_ITEM_LABEL_SIZE]; /* NULL terminator label */
	u32 scale_factor; /* Convert HW unit to HWmon unnt */
	u8 data_size;	  /* 4 or 8 bytes */
	u32 hw_reg;	  /* Registor offset to data */
};

struct altra_hwmon_context {
	struct hwmon_channel_info *channel_info;
	const struct hwmon_channel_info **info;
	struct hwmon_chip_info chip;
	struct sensor_item *sensor_list[hwmon_max];
	u32 sensor_list_cnt[hwmon_max];
	struct device *dev;
	struct device *hwmon_dev;
	void __iomem *base;
	u32 base_size;
};

static u32 altra_hwmon_read32(struct altra_hwmon_context *ctx, u32 reg)
{
	return readl_relaxed(ctx->base + reg);
}

static u64 altra_hwmon_read64(struct altra_hwmon_context *ctx, u32 reg)
{
	return readq_relaxed(ctx->base + reg);
}

static int altra_hwmon_read_labels(struct device *dev,
				   enum hwmon_sensor_types type, u32 attr,
				   int channel, const char **str)
{
	struct altra_hwmon_context *ctx = dev_get_drvdata(dev);
	struct sensor_item *item;

	if (type >= hwmon_max)
		return -EINVAL;
	if (channel >= ctx->sensor_list_cnt[type])
		return -EINVAL;

	item = ctx->sensor_list[type];
	*str = item[channel].label;
	return 0;
}

static int altra_hwmon_read(struct device *dev, enum hwmon_sensor_types type,
			    u32 attr, int channel, long *val)
{
	struct altra_hwmon_context *ctx = dev_get_drvdata(dev);
	struct sensor_item *item;

	if (type >= hwmon_max)
		return -EINVAL;
	if (channel >= ctx->sensor_list_cnt[type])
		return -EINVAL;

	item = ctx->sensor_list[type];
	if (item[channel].data_size == 4)
		*val = altra_hwmon_read32(ctx, item[channel].hw_reg);
	else
		*val = altra_hwmon_read64(ctx, item[channel].hw_reg);
	*val *= item[channel].scale_factor;

	return 0;
}

static umode_t altra_hwmon_is_visible(const void *_data,
				      enum hwmon_sensor_types type,
				      u32 attr, int channel)
{
	return 0444;
}

static const struct hwmon_ops altra_hwmon_ops = {
	.is_visible = altra_hwmon_is_visible,
	.read = altra_hwmon_read,
	.read_string = altra_hwmon_read_labels,
};

static enum hwmon_sensor_types altra_hwmon_unit2type(u8 unit)
{
	switch (unit) {
	case UNIT_DEGREE_CELSIUS:
		return hwmon_temp;
	case UNIT_JOULE:
	case UNIT_MILLI_JOULE:
	case UNIT_MICRO_JOULE:
		return hwmon_energy;
	}
	return hwmon_max;
}

static u32 altra_hwmon_scale_factor(u8 unit)
{
	switch (unit) {
	case UNIT_DEGREE_CELSIUS:
		return 1000;
	case UNIT_JOULE:
		return 1000000;
	case UNIT_MILLI_JOULE:
		return 1000;
	case UNIT_MICRO_JOULE:
		return 1;
	}
	return 1;
}

static u32 altra_hwmon_type2flag(enum hwmon_sensor_types type)
{
	switch (type) {
	case hwmon_energy:
		return HWMON_E_INPUT | HWMON_E_LABEL;
	case hwmon_temp:
		return HWMON_T_LABEL | HWMON_T_INPUT;
	default:
		return 0;
	}
}

static int altra_sensor_is_valid(struct altra_hwmon_context *ctx, u32 reg, u32 data_size)
{
	int val;

	if (data_size == 4)
		val = altra_hwmon_read32(ctx, reg);
	else
		val = altra_hwmon_read64(ctx, reg);

	return val ? 1 : 0;
}

static int altra_create_sensor(struct altra_hwmon_context *ctx,
			       u32 metric_info,
			       struct hwmon_channel_info *info)
{
	enum hwmon_sensor_types type;
	char label[SENSOR_ITEM_LABEL_SIZE];
	struct sensor_item *item_list;
	struct sensor_item *item;
	int data_size;
	u32 *s_config;
	u32 hw_info;
	u32 total;
	int i, j;

	/* Check for supported type */
	hw_info = altra_hwmon_read32(ctx, metric_info + HW_METRIC_INFO_REG);
	type = altra_hwmon_unit2type(HW_METRIC_INFO_UNIT_RD(hw_info));
	if (type == hwmon_max) {
		dev_err(ctx->dev,
			"malform info header @ 0x%x value 0x%x. Ignore remaining\n",
			metric_info + HW_METRIC_INFO_REG, hw_info);
		return -ENODEV;
	}

	/* Label */
	for (i = 0; i < HW_METRIC_LABEL_SIZE; i += 4)
		*(u32 *)&label[i] = altra_hwmon_read32(ctx,
					metric_info + HW_METRIC_LABEL_REG + i);
	label[sizeof(label) - 1] = '\0';
	if (strlen(label) <= 0) {
		dev_err(ctx->dev,
			"malform label header 0x%x. Ignore remaining\n",
			metric_info + HW_METRIC_LABEL_REG);
		return -ENODEV;
	}

	total = HW_METRIC_INFO_DATACNT_RD(hw_info);
	data_size = HW_METRIC_INFO_DATASIZE_RD(hw_info);
	/* Get the total valid sensors */
	j = 0;
	for (i = 0; i < total; i++) {
		if (altra_sensor_is_valid(ctx, metric_info + HW_METRIC_DATA_REG +
					  i * data_size, data_size))
			j++;
	}
	total = j;

	if (!ctx->sensor_list[type]) {
		ctx->sensor_list[type] = devm_kzalloc(ctx->dev,
						      sizeof(struct sensor_item) * total,
						      GFP_KERNEL);
	} else {
		item_list = devm_kzalloc(ctx->dev,
					 sizeof(*item) * (ctx->sensor_list_cnt[type] + total),
					 GFP_KERNEL);
		if (!item_list)
			return -ENOMEM;
		memcpy(item_list, ctx->sensor_list[type],
		       sizeof(*item) * ctx->sensor_list_cnt[type]);
		devm_kfree(ctx->dev, ctx->sensor_list[type]);
		ctx->sensor_list[type] = item_list;
	}

	s_config = devm_kcalloc(ctx->dev, total, sizeof(u32), GFP_KERNEL);
	if (!s_config)
		return -ENOMEM;
	info->type = type;
	info->config = s_config;

	/* Set up sensor entry */
	item_list = ctx->sensor_list[type];
	j = 0;
	for (i = 0; i < HW_METRIC_INFO_DATACNT_RD(hw_info); i++) {
		/* Check if sensor is valid */
		if (!altra_sensor_is_valid(ctx, metric_info + HW_METRIC_DATA_REG +
					   i * data_size, data_size))
			continue;

		item = &item_list[ctx->sensor_list_cnt[type]];
		item->hw_reg = metric_info + HW_METRIC_DATA_REG + i * data_size;
		scnprintf(item->label, SENSOR_ITEM_LABEL_SIZE, "%s %03u", label, j);
		item->scale_factor = altra_hwmon_scale_factor(HW_METRIC_INFO_UNIT_RD(hw_info));
		item->data_size = data_size;
		s_config[j] = altra_hwmon_type2flag(type);
		ctx->sensor_list_cnt[type]++;
		j++;
	}

	return 0;
}

static int altra_hwmon_create_sensors(struct altra_hwmon_context *ctx)
{
	u32 metrics_info;
	u32 total_metric;
	u32 hw_reg;
	u32 hw_end_reg;
	int ret;
	u32 val;
	int i;
	int used;

	if (altra_hwmon_read32(ctx, HW_METRICS_ID_REG) != HW_METRICS_ID)
		return -ENODEV;

	metrics_info = altra_hwmon_read32(ctx, HW_METRICS_INFO_REG);
	if (HW_METRICS_INFO_VER_RD(metrics_info) != HW_SUPPORTED_VER)
		return -ENODEV;

	total_metric = HW_METRICS_INFO_CNT_RD(metrics_info);
	ctx->channel_info = devm_kzalloc(ctx->dev,
					 sizeof(struct hwmon_channel_info) * total_metric,
					 GFP_KERNEL);
	if (!ctx->channel_info)
		return -ENOMEM;
	ctx->info = devm_kzalloc(ctx->dev,
				 sizeof(struct hwmon_channel_info *) * (total_metric + 1),
				 GFP_KERNEL);
	if (!ctx->info)
		return -ENOMEM;

	hw_reg = HW_METRICS_HDRSIZE;
	for (used = 0, i = 0; i < total_metric; i++) {
		/* Check for out of bound */
		if ((hw_reg + HW_METRIC_HDRSIZE) > ctx->base_size) {
			dev_err(ctx->dev,
				"malform metric header 0x%x (exceeded range). Ignore remaining\n",
				hw_reg);
			break;
		}

		/*
		 * At least a metric header. Check with data.
		 */
		val = altra_hwmon_read32(ctx, hw_reg + HW_METRIC_INFO_REG);
		hw_end_reg = hw_reg + HW_METRIC_HDRSIZE +
			   HW_METRIC_INFO_DATASIZE_RD(val) *
			   HW_METRIC_INFO_DATACNT_RD(val);
		if (hw_end_reg > ctx->base_size) {
			dev_err(ctx->dev,
				"malform metric data 0x%x (exceeded range). Ignore remaining\n",
				hw_reg);
			break;
		}
		ret = altra_create_sensor(ctx, hw_reg, &ctx->channel_info[used]);

		/* 64-bit alignment */
		hw_reg = hw_end_reg;
		hw_reg = ((hw_reg + 7) / 8) * 8;
		if (ret == -ENODEV)
			continue;
		if (ret < 0)
			return ret;
		ctx->info[used] = &ctx->channel_info[used];
		used++;
	}
	ctx->info[used] = NULL;
	return 0;
}

static int altra_hwmon_probe(struct platform_device *pdev)
{
	const struct acpi_device_id *acpi_id;
	struct altra_hwmon_context *ctx;
	struct device *dev = &pdev->dev;
	struct resource *res;
	int version;
	int err;

	ctx = devm_kzalloc(dev, sizeof(struct altra_hwmon_context), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	dev_set_drvdata(dev, ctx);
	ctx->dev = dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -EINVAL;

	acpi_id = acpi_match_device(dev->driver->acpi_match_table, dev);
	if (!acpi_id)
		return -EINVAL;

	version = (int)acpi_id->driver_data;

	ctx->base_size = resource_size(res);
	if (version == ALTRA_HWMON_VER1)
		ctx->base = devm_ioremap_resource(dev, res);
	else
		ctx->base = memremap(res->start, ctx->base_size, MEMREMAP_WB);
	if (IS_ERR(ctx->base))
		return PTR_ERR(ctx->base);

	/* Create sensors */
	err = altra_hwmon_create_sensors(ctx);
	if (err != 0) {
		if (err == -ENODEV)
			dev_err(dev, "No sensor\n");
		else
			dev_err(dev, "Failed to create sensors error %d\n", err);
		return err;
	}

	ctx->chip.ops = &altra_hwmon_ops;
	ctx->chip.info = ctx->info;
	ctx->hwmon_dev = devm_hwmon_device_register_with_info(dev, DRVNAME, ctx,
							      &ctx->chip, NULL);
	if (IS_ERR(ctx->hwmon_dev)) {
		dev_err(dev, "Fail to register with HWmon\n");
		err = PTR_ERR(ctx->hwmon_dev);
		return err;
	}

	return 0;
}

static int altra_hwmon_remove(struct platform_device *pdev)
{
	return 0;
}

#ifdef CONFIG_ACPI
static const struct acpi_device_id altra_hwmon_acpi_match[] = {
	{"AMPC0005", ALTRA_HWMON_VER1},
	{"AMPC0006", ALTRA_HWMON_VER2},
	{ },
};
MODULE_DEVICE_TABLE(acpi, altra_hwmon_acpi_match);
#endif

static struct platform_driver altra_hwmon_driver = {
	.probe = altra_hwmon_probe,
	.remove = altra_hwmon_remove,
	.driver = {
		.name = "altra-hwmon",
		.acpi_match_table = ACPI_PTR(altra_hwmon_acpi_match),
	},
};
module_platform_driver(altra_hwmon_driver);

MODULE_DESCRIPTION("Altra SoC hardware sensor monitor");
MODULE_LICENSE("GPL v2");
