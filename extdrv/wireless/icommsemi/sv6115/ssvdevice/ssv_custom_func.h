#ifndef _SSV_CUSTOM_FUNC_H_
#define _SSV_CUSTOM_FUNC_H_


int ssv_custom_modify_rf_conf_table(struct ssv_hw *sh);
int ssv_custom_modify_macaddr(struct ssv_hw *sh);

struct ssv6xxx_hwif_ops;

int ssv6xxx_platform_init(struct device *dev, struct ssv6xxx_hwif_ops *hwif_ops, void **plat_hw);
void ssv6xxx_platform_deinit(void *plat_hw);
#endif
