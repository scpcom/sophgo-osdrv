/*
 * Copyright (C) Cvitek Co., Ltd. 2019-2021. All rights reserved.
 *
 * File Name: jpeg_common.h
 * Description: jpeg chip common interface definition
 */

#ifndef __JPEG_COMMON_H__
#define __JPEG_COMMON_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <linux/of.h>
#include "cvi_jpeg.h"

extern const struct of_device_id cvi_jpu_match_table[];

void jpu_clk_disable(struct cvi_jpu_device *jdev);
void jpu_clk_enable(struct cvi_jpu_device *jdev);
void jpu_clk_get(struct cvi_jpu_device *jdev);
void jpu_clk_put(struct cvi_jpu_device *jdev);
void cv1835_config_pll(struct cvi_jpu_device *jdev);

int jpu_wait_interrupt(int timeout);
int jpu_set_clock_gate(int *pEnable);
int jpu_get_instance_pool(jpudrv_buffer_t *p_jdb);
int jpu_open_instance(unsigned long *pInstIdx);
int jpu_close_instance(unsigned long *pInstIdx);
int jpu_get_instance_num(int *pInstNum);
int jpu_get_register_info(jpudrv_buffer_t *p_jdb_register);
int jpu_reset(int InstIdx);
unsigned long jpu_get_interrupt_flag(int chnIdx);
void jpu_set_channel_num(int chnIdx);

#ifdef __cplusplus
}
#endif

#endif
