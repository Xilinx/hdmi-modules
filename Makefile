SRC := $(shell pwd)

obj-m	+= hdmi/
obj-m	+= misc/

all:
	$(MAKE) -C $(KERNEL_SRC) M=$(SRC) O=$(O) modules

modules_install:
	$(MAKE) -C $(KERNEL_SRC) M=$(SRC) modules_install

clean:
	find . -name "*.ko" -type f -delete
	find . -name "*.o" -type f -delete
	find . -name "*.order" -type f -delete
	find . -name "*.cmd" -type f -delete
	find . -name "*.mod*" -type f -delete
	rm -f Module.symvers
