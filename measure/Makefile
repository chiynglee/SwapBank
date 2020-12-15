obj-m := measure.o

measure-y := page_cnt.o

KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

default:
	make -C $(KDIR) M=$(PWD) modules
	gcc -o read_result read_result.c
clean:
	make -C $(KDIR) M=$(PWD) clean
