#ifndef __BASE_H__
#define __BASE_H__

#include <linux/cvi_base.h>
#include "base_debug.h"

unsigned int cvi_base_read_chip_id(void);
void vip_set_base_addr(void *base);

#endif
