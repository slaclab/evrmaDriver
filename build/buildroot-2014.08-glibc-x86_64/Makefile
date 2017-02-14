BUILDROOT_HOME = /afs/slac/g/lcls/package/linuxRT/buildroot-2014.11/
OUTPUT_DIR=../../buildroot-2014.08-glibc-x86_64

### EVR testing machines
XCROSS_HOME := $(BUILDROOT_HOME)/host/linux-x86_64/x86_64-glibc-linux/usr/bin/x86_64-buildroot-linux-gnu-
KERNELDIR := $(BUILDROOT_HOME)/buildroot-glibc-x86_64/output/build/linux-3.14.12/

all:
	### copy the sources to make a directory for building
	cp -r ../../src/* .
	$(MAKE) compile

evrma-objs	+= main_evrma.o mng-dev.o virt-dev.o rm.o packet-queue.o 
evrma-objs	+= evr.o evr-irq-events.o evr-dbg.o 
evrma-objs	+= plx.o pci-evr.o 
evrma-objs	+= evr-sim.o event-list.o

obj-m += evrma.o

compile:
	$(MAKE) ARCH=x86_64 CROSS_COMPILE=$(XCROSS_HOME) -C $(KERNELDIR) M=$(PWD) modules

clean:
	rm -rf *.o *~ core .depend .*.cmd *.ko *.mod.c .tmp_versions *.symvers  *.order *.c *.h
 
install:
	mkdir -p $(OUTPUT_DIR)/include/
	cp  linux-evrma.h linux-evr-regs.h  linux-modac.h $(OUTPUT_DIR)/include/.
	cp  *.ko $(OUTPUT_DIR)/.

