#ifndef __MON_PLATFORM_H__
#define __MON_PLATFORM_H__

#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include "cvi_mon_interface.h"


struct AXIMON_INFO_PORT;

void axi_mon_reset_all(void);
void axi_mon_start_all(void);
void axi_mon_stop_all(void);
void axi_mon_snapshot_all(void);
void axi_mon_get_info_all(uint32_t duration);
void axi_mon_dump_single(struct AXIMON_INFO_PORT *port_info);
void axi_mon_dump(void);
void axi_mon_init(struct cvi_mon_device *ndev);

#endif

