RHEL = rhel7
KERNELDIR ?= /lib/modules/$(shell uname -r)/build
OUTPUT_DIR=../../$(RHEL)-x86_64

all:
	### copy the sources to make a directory for building
	cp -r ../../src/* .
	$(MAKE) module_load
	chmod a+x module_*
	$(MAKE) compile
	

evrma-objs	+= main_evrma.o mng-dev.o virt-dev.o rm.o packet-queue.o
evrma-objs	+= evr.o evr-irq-events.o evr-dbg.o
evrma-objs	+= plx.o pci-evr.o
evrma-objs	+= evr-sim.o event-list.o

obj-m += evrma.o

compile:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

module_load: original.module_load
	sed 's/RHEL/$(RHEL)/' $< >$@

clean:
	rm -rf *.o *~ core .depend .*.cmd *.ko *.ko.unsigned *.mod.c .tmp_versions *.symvers  *.order *.c *.h module_* original.*

install:
	mkdir -p $(OUTPUT_DIR)/include/
	cp  linux-evrma.h linux-evr-regs.h linux-modac.h  $(OUTPUT_DIR)/include/.
	cp  *.ko $(OUTPUT_DIR)/.
	cp module_* $(OUTPUT_DIR)/.

