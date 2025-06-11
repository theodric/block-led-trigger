obj-m := block_led_trigger.o

KDIR  := /lib/modules/$(shell uname -r)/build
PWD   := $(shell pwd)

.PHONY: default clean

default:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
