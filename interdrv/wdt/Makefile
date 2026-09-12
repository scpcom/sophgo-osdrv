ifneq ($(OSDRV_IN_TREE),)
PWD := $(shell pwd)/$(src)
endif
include $(PWD)/../Makefile.interdrv.param

WDT_PATH = $(CVIARCH_L)

soph_wdt-objs = $(WDT_PATH)/cvi_wdt.o

obj-m += soph_wdt.o

ccflags-y +=-Wall -Wextra -Werror -Wno-unused-parameter -Wno-sign-compare -Wno-type-limits

all:
	$(MAKE) ARCH=${ARCH} -C $(KERNEL_DIR) M=$(PWD) modules

clean:
	@rm -f *.o .*.cmd modules.order Module.symvers *.ko *.mod.c
	@rm -rf .tmp_versions
	@find . -name \*.o* -type f -delete
