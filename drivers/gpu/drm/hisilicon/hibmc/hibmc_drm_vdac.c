// SPDX-License-Identifier: GPL-2.0-or-later
/* Hisilicon Hibmc SoC drm driver
 *
 * Based on the bochs drm driver.
 *
 * Copyright (c) 2016 Huawei Limited.
 *
 * Author:
 *	Rongrong Zou <zourongrong@huawei.com>
 *	Rongrong Zou <zourongrong@gmail.com>
 *	Jianhua Li <lijianhua@huawei.com>
 */

#include <drm/drm_atomic_helper.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_print.h>

#include "hibmc_drm_drv.h"
#include "hibmc_drm_regs.h"

struct hibmc_resolution {
	int w;
	int h;
};

static const struct hibmc_resolution hibmc_modetables[] = {
	{640, 480},
	{800, 600},
	{1024, 768},
	{1152, 864},
	{1280, 768},
	{1280, 720},
	{1280, 960},
	{1280, 1024},
	{1440, 900},
	{1600, 900},
	{1600, 1200},
	{1920, 1080},
	{1920, 1200}
};

static int hibmc_valid_mode(int w, int h)
{
	int i;

	for (i = 0; i < sizeof(hibmc_modetables) /
		sizeof(struct hibmc_resolution); i++) {
		if (hibmc_modetables[i].w == w && hibmc_modetables[i].h == h)
			return 0;
	}

	return -1;
}

static int hibmc_connector_get_modes(struct drm_connector *connector)
{
	int count;

	drm_connector_update_edid_property(connector, NULL);
	count = drm_add_modes_noedid(connector, 1920, 1200);
	drm_set_preferred_mode(connector, 1024, 768);

	return count;
}

static enum drm_mode_status hibmc_connector_mode_valid(struct drm_connector *connector,
				      struct drm_display_mode *mode)
{
	int vrefresh = drm_mode_vrefresh(mode);

	if (vrefresh < 59 || vrefresh > 61)
		return MODE_NOMODE;
	else if (hibmc_valid_mode(mode->hdisplay, mode->vdisplay) != 0)
		return MODE_NOMODE;
	else
		return MODE_OK;
}

static const struct drm_connector_helper_funcs
	hibmc_connector_helper_funcs = {
	.get_modes = hibmc_connector_get_modes,
	.mode_valid = hibmc_connector_mode_valid,
};

static const struct drm_connector_funcs hibmc_connector_funcs = {
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static struct drm_connector *
hibmc_connector_init(struct hibmc_drm_private *priv)
{
	struct drm_device *dev = priv->dev;
	struct drm_connector *connector;
	int ret;

	connector = devm_kzalloc(dev->dev, sizeof(*connector), GFP_KERNEL);
	if (!connector) {
		DRM_ERROR("failed to alloc memory when init connector\n");
		return ERR_PTR(-ENOMEM);
	}

	ret = drm_connector_init(dev, connector,
				 &hibmc_connector_funcs,
				 DRM_MODE_CONNECTOR_VGA);
	if (ret) {
		DRM_ERROR("failed to init connector: %d\n", ret);
		return ERR_PTR(ret);
	}
	drm_connector_helper_add(connector,
				 &hibmc_connector_helper_funcs);

	return connector;
}

static void hibmc_encoder_mode_set(struct drm_encoder *encoder,
				   struct drm_display_mode *mode,
				   struct drm_display_mode *adj_mode)
{
	u32 reg;
	struct drm_device *dev = encoder->dev;
	struct hibmc_drm_private *priv = dev->dev_private;

	reg = readl(priv->mmio + HIBMC_DISPLAY_CONTROL_HISILE);
	reg |= HIBMC_DISPLAY_CONTROL_FPVDDEN(1);
	reg |= HIBMC_DISPLAY_CONTROL_PANELDATE(1);
	reg |= HIBMC_DISPLAY_CONTROL_FPEN(1);
	reg |= HIBMC_DISPLAY_CONTROL_VBIASEN(1);
	writel(reg, priv->mmio + HIBMC_DISPLAY_CONTROL_HISILE);
}

static const struct drm_encoder_helper_funcs hibmc_encoder_helper_funcs = {
	.mode_set = hibmc_encoder_mode_set,
};

static const struct drm_encoder_funcs hibmc_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

int hibmc_vdac_init(struct hibmc_drm_private *priv)
{
	struct drm_device *dev = priv->dev;
	struct drm_encoder *encoder;
	struct drm_connector *connector;
	int ret;

	connector = hibmc_connector_init(priv);
	if (IS_ERR(connector)) {
		DRM_ERROR("failed to create connector: %ld\n",
			  PTR_ERR(connector));
		return PTR_ERR(connector);
	}

	encoder = devm_kzalloc(dev->dev, sizeof(*encoder), GFP_KERNEL);
	if (!encoder) {
		DRM_ERROR("failed to alloc memory when init encoder\n");
		return -ENOMEM;
	}

	encoder->possible_crtcs = 0x1;
	ret = drm_encoder_init(dev, encoder, &hibmc_encoder_funcs,
			       DRM_MODE_ENCODER_DAC, NULL);
	if (ret) {
		DRM_ERROR("failed to init encoder: %d\n", ret);
		return ret;
	}

	drm_encoder_helper_add(encoder, &hibmc_encoder_helper_funcs);
	drm_connector_attach_encoder(connector, encoder);

	return 0;
}
