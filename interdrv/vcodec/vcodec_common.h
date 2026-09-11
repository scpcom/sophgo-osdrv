/*
 * Copyright (C) Cvitek Co., Ltd. 2019-2021. All rights reserved.
 *
 * File Name: vcodec_common.h
 * Description:
 */

#ifndef __VCODEC_COMMON_H__
#define __VCODEC_COMMON_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <linux/of.h>
#include <linux/version.h>
#include "cvi_vcodec.h"
#include "vpuconfig.h"

typedef struct vpu_drv_context_t {
	struct fasync_struct *async_queue;
	u32 open_count; /*!<< device reference count. Not instance count */
} vpu_drv_context_t;

struct cvi_vcodec_context {
	vpudrv_buffer_t s_vpu_register;
	int s_vcodec_irq;
	int s_sbm_irq;
	int s_interrupt_flag;
	wait_queue_head_t s_interrupt_wait_q;
	int s_sbm_interrupt_flag;
	wait_queue_head_t s_sbm_interrupt_wait_q;
	unsigned long interrupt_reason;
	vpu_bit_firmware_info_t s_bit_firmware_info;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
	struct timespec64 irq_timestamp;
	struct timespec64 sbm_irq_timestamp;
#else
	struct timespec irq_timestamp;
	struct timespec sbm_irq_timestamp;
#endif
};

struct cvi_vcodec_device {
	struct cvi_vcodec_context vcodec_ctx[MAX_NUM_VPU_CORE];
	vpudrv_buffer_t s_instance_pool;
	vpudrv_buffer_t s_common_memory[MAX_NUM_VPU_CORE];
	vpudrv_buffer_t ctrl_register;
	vpudrv_buffer_t remap_register;
	vpudrv_buffer_t sbm_register;

	vpu_drv_context_t s_vpu_drv_context;
};

struct clk_ctrl_info;

extern const struct of_device_id cvi_vpu_match_table[];
extern struct cvi_vcodec_device vcodec_dev;

void vpu_clk_get(struct cvi_vpu_device *vdev);
void vpu_clk_put(struct cvi_vpu_device *vdev);
void vpu_clk_enable(struct cvi_vpu_device *vdev, int mask);
void vpu_clk_disable(struct cvi_vpu_device *vdev, int mask);
unsigned long vpu_clk_get_rate(struct cvi_vpu_device *vdev);
void cviConfigDDR(struct cvi_vpu_device *vdev);

void cvi_VENC_SBM_IrqEnable(void);
void cvi_VENC_SBM_IrqDisable(void);
void wake_sbm_waitinng(void);
int sbm_wait_interrupt(int timeout);
int vpu_wait_interrupt(vpudrv_intr_info_t *p_intr_info);
int vpu_set_clock_gate_ext(struct clk_ctrl_info *p_info);
int vpu_get_instance_pool(vpudrv_buffer_t *p_vdb);
#ifndef CVI_H26X_USE_ION_FW_BUFFER
int vpu_get_common_memory(vpudrv_buffer_t *p_vdb);
#else
void vpu_set_common_memory(unsigned long core, vpudrv_buffer_t *p_vdb);
#endif
int vpu_open_instance(vpudrv_inst_info_t *p_inst_info);
int vpu_close_instance(vpudrv_inst_info_t *p_inst_info);
int vpu_reset(void);
int vpu_get_register_info(vpudrv_buffer_t *p_vdb_register);
int vpu_get_chip_version(unsigned int *p_chip_version);
int vpu_get_chip_cabability(unsigned int *p_chip_capability);
int vpu_get_clock_frequency(unsigned long *p_clk_rate);
#ifndef CVI_H26X_USE_ION_FW_BUFFER
int vpu_release_common_memory(vpudrv_buffer_t *p_vdb);
#endif
int vpu_get_single_core_config(int *pSingleCoreConfig);
int vpu_op_write(vpu_bit_firmware_info_t *p_bit_firmware_info, size_t len);
void vpu_set_channel_core_mapping(int chnIdx, int coreIdx);
unsigned long vpu_get_interrupt_reason(int chnIdx);

void vcodec_lock(void);
void vcodec_unlock(void);
int vcodec_trylock(void);
int vcodec_is_locked(void);

#ifdef __cplusplus
}
#endif

#endif
