/*
 * Copyright (C) Cvitek Co., Ltd. 2019-2021. All rights reserved.
 *
 * File Name: cfg_cv182x.c
 * Description: camera interface driver
 */

#include <linux/module.h>
#include <linux/cdev.h>
#include <linux/clk.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/io.h>
#include "cvi_vcodec.h"
#include "vcodec_common.h"

static void vpu_cfg_clk_enable(struct cvi_vpu_device *vdev, int mask);
static void vpu_cfg_clk_put(struct cvi_vpu_device *vdev);
static void vpu_cfg_clk_get(struct cvi_vpu_device *vdev);
static DEFINE_MUTEX(vpu_clock_lock);
static void vpu_cfg_clk_disable(struct cvi_vpu_device *vdev, int mask);
static void vpu_fpga_init(struct cvi_vpu_device *vdev, int mask);

const struct vpu_ops asic_vpu_ops = {
	.clk_get = vpu_cfg_clk_get,
	.clk_put = vpu_cfg_clk_put,
	.clk_enable = vpu_cfg_clk_enable,
	.clk_disable = vpu_cfg_clk_disable,
	.config_ddr = NULL,
	.clk_get_rate = vpu_clk_get_rate,
};

const struct vpu_ops fpga_vpu_ops = {
	.clk_get = NULL,
	.clk_put = NULL,
	.clk_enable = vpu_fpga_init,
	.clk_disable = NULL,
	.config_ddr = NULL,
	.clk_get_rate = NULL,
};

static const struct vpu_pltfm_data fpga_vpu_pdata = {
	.ops = &fpga_vpu_ops,
	.quirks = VCDOEC_QUIRK_SUPPORT_FPGA          |
		  VCODEC_QUIRK_SUPPORT_VC_CTRL_REG   |
		  VCODEC_QUIRK_SUPPORT_VC_SBM        |
		  VCODEC_QUIRK_SUPPORT_VC_ADDR_REMAP,
	.version = 0x1821A,
};

static const struct vpu_pltfm_data pxp_vpu_pdata = {
	.ops = NULL,
	.quirks = VCODEC_QUIRK_SUPPORT_VC_CTRL_REG |
		  VCODEC_QUIRK_SUPPORT_VC_SBM      |
		  VCODEC_QUIRK_SUPPORT_VC_ADDR_REMAP,
	.version = 0x1821A,
};

static const struct vpu_pltfm_data asic_vpu_pdata = {
	.ops = &asic_vpu_ops,
	.quirks = VCODEC_QUIRK_SUPPORT_CLOCK_CONTROL |
		  VCODEC_QUIRK_SUPPORT_VC_CTRL_REG   |
		  VCODEC_QUIRK_SUPPORT_VC_SBM        |
		  VCODEC_QUIRK_SUPPORT_VC_ADDR_REMAP,
	.version = 0x1821A,
};

const struct of_device_id cvi_vpu_match_table[] = {
	{ .compatible = "cvitek,fpga-vcodec", .data = &fpga_vpu_pdata},  // for fpga
	{ .compatible = "cvitek,pxp-vcodec",  .data = &pxp_vpu_pdata},   // for pxp
	{ .compatible = "cvitek,asic-vcodec", .data = &asic_vpu_pdata},  // for asic
	{},
};

MODULE_DEVICE_TABLE(of, cvi_vpu_match_table);

static void sram_share_config(vpudrv_buffer_t *pReg, int mask)
{
	if (mask & BIT(H264_CORE_IDX)) {
		// devmem 0xb030024 32 1
		writel(0x1, pReg->virt_addr + 0x24);
	} else if (mask & BIT(H265_CORE_IDX)) {
		// devmem 0xb030024 32 2
		writel(0x2, pReg->virt_addr + 0x24);
	}
}

/* Based on patch from dormancygrace/NanoKVM-OS */
/* Keep the vendor no-gating policy, but own at most one reference per clock.
 * Both core clocks must be enabled during a new core's SRAM transition. */
static void vpu_cfg_clk_enable(struct cvi_vpu_device *vdev, int mask)
{
	struct clk *clocks[] = {vdev->clk_vc_src0, vdev->clk_cfg_reg_vc,
		vdev->clk_axi_video_codec, vdev->clk_h264c, vdev->clk_apb_h264c,
		vdev->clk_h265c, vdev->clk_apb_h265c};
	u32 before, added;
	int i, err;

	if ((mask & BIT(H264_CORE_IDX)) && (mask & BIT(H265_CORE_IDX))) return;
	mutex_lock(&vpu_clock_lock);
	before = vdev->vpu_clock_holds;
	for (i = 0; i < ARRAY_SIZE(clocks); ++i) {
		if (!(vdev->vpu_clock_holds & BIT(i))) {
			err = clk_prepare_enable(clocks[i]);
			if (err) goto rollback;
			vdev->vpu_clock_holds |= BIT(i);
		}
		if (i == 2) sram_share_config(&vcodec_dev.ctrl_register, mask);
	}
	/* Drop only temporary references acquired by this call. Never steal a
	 * reference retained for a previously used core or another clock owner. */
	added = vdev->vpu_clock_holds & ~before;
	for (i = 6; i >= 3; --i) {
		bool other = (i >= 5 && (mask & BIT(H264_CORE_IDX))) ||
		             (i <= 4 && (mask & BIT(H265_CORE_IDX)));
		if (other && (added & BIT(i))) {
			clk_disable_unprepare(clocks[i]);
			vdev->vpu_clock_holds &= ~BIT(i);
		}
	}
	mutex_unlock(&vpu_clock_lock);
	return;
rollback:
	dev_err(vdev->dev, "VPU clock enable failed: %d\n", err);
	for (--i; i >= 0; --i) {
		if ((vdev->vpu_clock_holds & ~before) & BIT(i))
			clk_disable_unprepare(clocks[i]);
	}
	vdev->vpu_clock_holds = before;
	mutex_unlock(&vpu_clock_lock);
}

/* Hold register clocks before IRQ registration and higher-level driver access.
 * A zero mask preserves SRAM selection and retains both core clocks. */
static void vpu_cfg_clk_get(struct cvi_vpu_device *vdev)
{
	struct clk *clocks[9];
	int i;

	vdev->vpu_clock_error = 0;
	vpu_clk_get(vdev);
	clocks[0] = vdev->clk_vc_src0;
	clocks[1] = vdev->clk_cfg_reg_vc;
	clocks[2] = vdev->clk_axi_video_codec;
	clocks[3] = vdev->clk_h264c;
	clocks[4] = vdev->clk_apb_h264c;
	clocks[5] = vdev->clk_h265c;
	clocks[6] = vdev->clk_apb_h265c;
	clocks[7] = vdev->clk_vc_src1;
	clocks[8] = vdev->clk_vc_src2;
	for (i = 0; i < ARRAY_SIZE(clocks); ++i) {
		if (IS_ERR_OR_NULL(clocks[i])) {
			vdev->vpu_clock_error = clocks[i] ? PTR_ERR(clocks[i]) : -ENOENT;
			return;
		}
	}
	vpu_cfg_clk_enable(vdev, 0);
	if (vdev->vpu_clock_holds != GENMASK(6, 0))
		vdev->vpu_clock_error = -EIO;
}

static void vpu_cfg_clk_put(struct cvi_vpu_device *vdev)
{
	struct clk *clocks[] = {vdev->clk_vc_src0, vdev->clk_cfg_reg_vc,
		vdev->clk_axi_video_codec, vdev->clk_h264c, vdev->clk_apb_h264c,
		vdev->clk_h265c, vdev->clk_apb_h265c};
	int i;

	mutex_lock(&vpu_clock_lock);
	for (i = ARRAY_SIZE(clocks) - 1; i >= 0; --i)
		if (vdev->vpu_clock_holds & BIT(i)) clk_disable_unprepare(clocks[i]);
	vdev->vpu_clock_holds = 0;
	mutex_unlock(&vpu_clock_lock);
	vpu_clk_put(vdev);
}

static void vpu_cfg_clk_disable(struct cvi_vpu_device *vdev, int mask)
{
	if (vcodec_mask & VCODEC_MASK_DISABLE_CLK_GATING) {
		return;
	}

	if ((mask & BIT(H264_CORE_IDX)) && (mask & BIT(H265_CORE_IDX))) {
		VCODEC_DBG_WARN("not supported mask\n");
		return;
	}

#if 0
	if (mask & BIT(H264_CORE_IDX) && __clk_is_enabled(vdev->clk_h264c)) {
		clk_disable_unprepare(vdev->clk_h264c);
		clk_disable_unprepare(vdev->clk_apb_h264c);
	} else if (mask & BIT(H265_CORE_IDX) && __clk_is_enabled(vdev->clk_h265c)) {
		clk_disable_unprepare(vdev->clk_h265c);
		clk_disable_unprepare(vdev->clk_apb_h265c);
	}

	clk_disable_unprepare(vdev->clk_vc_src0);
	clk_disable_unprepare(vdev->clk_cfg_reg_vc);
	clk_disable_unprepare(vdev->clk_axi_video_codec);
#endif
}

static void vpu_fpga_init(struct cvi_vpu_device *vdev, int mask)
{
	vpudrv_buffer_t *pReg = &vcodec_dev.ctrl_register;

	VCODEC_DBG_TRACE("mask = 0x%X\n", mask);

	sram_share_config(pReg, mask);

	writel(0xF8, pReg->virt_addr + 0x8);    // reg_dummy
#if 0
	writel(0xFF, pReg->virt_addr + 0x30);   // reg_dis_fab_lp_opt, bus low power control off
#endif
}
