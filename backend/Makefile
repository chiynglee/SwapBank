obj-m := shcomp_dom0.o

shcomp_dom0-y := grant-offer.o backend_xenbus.o

KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

default:
	make -C $(KDIR) M=$(PWD) modules
clean:
	make -C $(KDIR) M=$(PWD) clean
