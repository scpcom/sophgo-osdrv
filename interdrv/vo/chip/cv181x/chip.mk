CONFIG_CVI_LOG = 1

soph_vo-objs += chip/$(CVIARCH_L)/vo.o
soph_vo-objs += chip/$(CVIARCH_L)/vo_sdk_layer.o
soph_vo-objs += chip/$(CVIARCH_L)/proc/vo_disp_proc.o
soph_vo-objs += chip/$(CVIARCH_L)/proc/vo_proc.o

soph_mipi_tx-objs := chip/$(CVIARCH_L)/vo_mipi_tx.o \
					chip/$(CVIARCH_L)/proc/vo_mipi_tx_proc.o

ifeq ($(CONFIG_CVI_LOG), 1)
ccflags-y += -DCONFIG_CVI_LOG
endif