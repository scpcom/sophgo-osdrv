/*
 * Copyright (C) Cvitek Co., Ltd. 2019-2021. All rights reserved.
 *
 * File Name: cv182x_jpeg_cfg.c
 * Description: jpeg cv182x implementation
 */

#include <linux/module.h>
#include <linux/clk.h>
#include <linux/mutex.h>
#include <linux/device.h>
#include <linux/of.h>
#include "cvi_jpeg.h"
#include "jpeg_common.h"

static void jpu_cfg_clk_enable(struct cvi_jpu_device *jdev);
static void jpu_cfg_clk_get(struct cvi_jpu_device *jdev);
static DEFINE_MUTEX(jpu_clock_lock);
static void jpu_cfg_clk_disable(struct cvi_jpu_device *jdev);
// static void jpu_fpga_init(struct cvi_jpu_device *jdev);

const struct jpu_ops asic_jpu_ops = {
	.clk_get = jpu_cfg_clk_get,
	.clk_put = jpu_clk_put,
	.clk_enable = jpu_cfg_clk_enable,
	.clk_disable = jpu_cfg_clk_disable,
	.config_pll = NULL,
};
#if 0

const struct jpu_ops fpga_jpu_ops = {
	.clk_get = NULL,
	.clk_put = NULL,
	.clk_enable = jpu_fpga_init,
	.clk_disable = NULL,
	.config_pll = NULL,
};

static const struct jpu_pltfm_data fpga_jpu_pdata = {
	.ops = &fpga_jpu_ops,
	.quirks = JPU_QUIRK_SUPPORT_FPGA,
	.version = 0x1821A,
};

static const struct jpu_pltfm_data pxp_jpu_pdata = {
	.ops = NULL,
	.quirks = 0,
	.version = 0x1821A,
};
#endif
static const struct jpu_pltfm_data asic_jpu_pdata = {
	.ops = &asic_jpu_ops,
	.quirks = JPU_QUIRK_SUPPORT_CLOCK_CONTROL,
	.version = 0x1821A,
};

const struct of_device_id cvi_jpu_match_table[] = {
#if 0
	{ .compatible = "cvitek,fpga-jpeg", .data = &fpga_jpu_pdata},  // for fpga
	{ .compatible = "cvitek,pxp-jpeg",  .data = &pxp_jpu_pdata},   // for pxp
#endif
	{ .compatible = "cvitek,asic-jpeg", .data = &asic_jpu_pdata},  // for asic
	{},
};

MODULE_DEVICE_TABLE(of, cvi_jpu_match_table);

/* Based on patch from dormancygrace/NanoKVM-OS */
/* Retain one owned reference until device removal, never one per request. */
static void jpu_cfg_clk_enable(struct cvi_jpu_device *jdev)
{
	struct clk *clocks[] = {jdev->clk_vc_src0, jdev->clk_cfg_reg_vc,
		jdev->clk_axi_video_codec, jdev->clk_jpeg, jdev->clk_apb_jpeg};
	u32 before;
	int i, err;

	mutex_lock(&jpu_clock_lock);
	before = jdev->jpu_clock_holds;
	for (i = 0; i < ARRAY_SIZE(clocks); ++i) {
		if (jdev->jpu_clock_holds & BIT(i)) continue;
		err = clk_prepare_enable(clocks[i]);
		if (err) goto rollback;
		jdev->jpu_clock_holds |= BIT(i);
	}
	mutex_unlock(&jpu_clock_lock);
	return;
rollback:
	jdev->jpu_clock_error = err;
	dev_err(jdev->dev, "JPU clock enable failed: %d\n", err);
	for (--i; i >= 0; --i) {
		if ((jdev->jpu_clock_holds & ~before) & BIT(i)) {
			clk_disable_unprepare(clocks[i]);
		}
	}
	jdev->jpu_clock_holds = before;
	mutex_unlock(&jpu_clock_lock);
}

static void jpu_release_clocks(void *data)
{
	struct cvi_jpu_device *jdev = data;
	struct clk *clocks[] = {jdev->clk_vc_src0, jdev->clk_cfg_reg_vc,
		jdev->clk_axi_video_codec, jdev->clk_jpeg, jdev->clk_apb_jpeg};
	int i;

	mutex_lock(&jpu_clock_lock);
	for (i = ARRAY_SIZE(clocks) - 1; i >= 0; --i) {
		if (jdev->jpu_clock_holds & BIT(i)) {
			clk_disable_unprepare(clocks[i]);
		}
	}
	jdev->jpu_clock_holds = 0;
	mutex_unlock(&jpu_clock_lock);
}

static void jpu_cfg_clk_get(struct cvi_jpu_device *jdev)
{
	struct clk *clocks[5];
	int i;

	jpu_clk_get(jdev);
	clocks[0] = jdev->clk_vc_src0;
	clocks[1] = jdev->clk_cfg_reg_vc;
	clocks[2] = jdev->clk_axi_video_codec;
	clocks[3] = jdev->clk_jpeg;
	clocks[4] = jdev->clk_apb_jpeg;
	for (i = 0; i < ARRAY_SIZE(clocks); ++i) {
		if (IS_ERR_OR_NULL(clocks[i])) {
			jdev->jpu_clock_error = clocks[i] ? PTR_ERR(clocks[i]) : -ENOENT;
			return;
		}
	}
	jpu_cfg_clk_enable(jdev);
	if (jdev->jpu_clock_error) return;
	/* Registered after clock handles: release runs before devm_clk_put, on
	 * probe failure as well as normal removal, after register users stop. */
	jdev->jpu_clock_error = devm_add_action_or_reset(jdev->dev,
		jpu_release_clocks, jdev);
}

static void jpu_cfg_clk_disable(struct cvi_jpu_device *jdev)
{
	if (jpu_mask & JPU_MASK_DISABLE_CLK_GATING)
		return;
#if 0
	clk_disable_unprepare(jdev->clk_jpeg);
	clk_disable_unprepare(jdev->clk_apb_jpeg);

	clk_disable_unprepare(jdev->clk_vc_src0);
	clk_disable_unprepare(jdev->clk_cfg_reg_vc);
	clk_disable_unprepare(jdev->clk_axi_video_codec);
#endif
}
#if 0
static void jpu_fpga_init(struct cvi_jpu_device *jdev)
{
#if 0
	jpudrv_buffer_t *pReg = &jdev->jpu_control_register;

	writel(0xFF, pReg->virt_addr + 0x30); // reg_dis_fab_lp_opt, bus low power control off
#endif
}
#endif