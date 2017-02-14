BUILDROOT_VER=buildroot-2016.11.1

BUILDROOT_HOME = /afs/slac/package/linuxRT/$(BUILDROOT_VER)
ARCH=x86_64
OUTPUT_DIR=../../buildroot-2016.11.1-x86_64

# ===============================================================
# We don't want to use the compiler cache; especially since not
# everyone has write access to the buildroot installation.
# ===============================================================
export CCACHE_DISABLE=1

### EVR testing machines
XCROSS_HOME := $(BUILDROOT_HOME)/host/linux-x86_64/x86_64/usr/bin/x86_64-buildroot-linux-gnu-
KERNELDIR := $(BUILDROOT_HOME)/buildroot-2016.11.1-x86_64/output/build/linux-4.8.11

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
	$(MAKE) ARCH=$(ARCH) CROSS_COMPILE=$(XCROSS_HOME) -C $(KERNELDIR) M=$(PWD) modules

clean:
	rm -rf *.o *~ core .depend .*.cmd *.ko *.mod.c .tmp_versions *.symvers  *.order *.c *.h
 
install:
	mkdir -p $(OUTPUT_DIR)/include/
	cp  linux-evrma.h linux-evr-regs.h linux-modac.h $(OUTPUT_DIR)/include/.
	cp  *.ko $(OUTPUT_DIR)/.

