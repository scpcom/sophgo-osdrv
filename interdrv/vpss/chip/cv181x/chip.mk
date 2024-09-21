CONFIG_SCLR_TEST = 0
CONFIG_CVI_LOG = 1
CONFIG_REG_DUMP = 1
CONFIG_TILE_MODE = 0
CONFIG_RGN_EX = 0

soph_vpss-objs += chip/$(CVIARCH_L)/vip_img.o
soph_vpss-objs += chip/$(CVIARCH_L)/vip_sc.o
soph_vpss-objs += chip/$(CVIARCH_L)/dsi_phy.o
soph_vpss-objs += chip/$(CVIARCH_L)/scaler.o
soph_vpss-objs += chip/$(CVIARCH_L)/vpss_core.o


ifeq ($(CONFIG_SCLR_TEST), 1)
ccflags-y += -DCONFIG_SCLR_TEST
soph_vpss-objs += chip/$(CVIARCH_L)/sclr_test.o
endif

ifeq ($(CONFIG_CVI_LOG), 1)
ccflags-y += -DCONFIG_CVI_LOG
endif

ifeq ($(CONFIG_REG_DUMP), 1)
ccflags-y += -DCONFIG_REG_DUMP
endif

ifeq ($(CONFIG_TILE_MODE), 1)
ccflags-y += -DCONFIG_TILE_MODE
endif

ifeq ($(CONFIG_RGN_EX), 1)
ccflags-y += -DCONFIG_RGN_EX
soph_vpss-objs += chip/$(CVIARCH_L)/cmdq.o
endif
