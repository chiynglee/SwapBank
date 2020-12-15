KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

COMPCACHE := compcache-domu
XVM := sub-projects/allocators/xvmalloc-kmod
EXTRA_CFLAGS    :=      -DCONFIG_RAMZSWAP_STATS         \
                        -Wall

obj-m := shcomp_domu.o

shcomp_domu-y := grant-refer.o frontend_xenbus.o shalloc.o \
		$(COMPCACHE)/ramzswap_drv.o $(COMPCACHE)/$(XVM)/xvmalloc.o

all:
	make -C $(KDIR) M=$(PWD) modules
	make -C $(COMPCACHE)/sub-projects/rzscontrol

doc:
	make -C $(COMPCACHE)/sub-projects/rzscontrol doc

clean:
	make -C $(KDIR) M=$(PWD) clean
	make -C $(COMPCACHE)/sub-projects/rzscontrol clean

