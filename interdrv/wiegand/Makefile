include $(PWD)/../Makefile.interdrv.param

PWD := $(shell pwd)

ccflags-y += -I$(src)

ccflags-y +=-Wall -Wextra -Werror -Wno-unused-parameter -Wno-sign-compare

soph_wiegand-objs := cvi_wiegand.o
obj-m += soph_wiegand.o

all:
	$(MAKE) ARCH=${ARCH} -C $(KERNEL_DIR) M=$(PWD) modules

clean:
	rm -f *.o* *.ko
	rm -f Module.symvers modules.order
	rm -f soph_wiegand.mod.c
	rm -rf .tmp_versions
	rm -f .*cmd
	rm -f $(CHIP)/*.o*
	rm -f $(CHIP)/.*cmd
