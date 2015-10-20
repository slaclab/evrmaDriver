BUILDROOT_HOME = /afs/slac/g/lcls/package/linuxRT/buildroot-2014.11/

### EVR testing machines
XCROSS_HOME := $(BUILDROOT_HOME)/host/linux-x86_64/x86_64-glibc-linux/usr/bin/x86_64-buildroot-linux-gnu-
KERNELDIR := $(BUILDROOT_HOME)/buildroot-uclibc-x86_64/output/build/linux-3.14.12/

# ### EMCOR testing machine
# XCROSS_HOME = $(BUILDROOT_HOME)/host/linux-x86_64/i686-uclibc-linux/usr/bin/i686-linux-
# KERNELDIR := $(BUILDROOT_HOME)/buildroot-uclibc-x86/output/build/linux-3.14.12/


all:
	### copy the sources to make a directory for building
	cp -r ../src/* .
	$(MAKE) compile

evrma-test-objs	+= main_test.o mng-dev.o virt-dev.o rm.o packet-queue.o 
evrma-test-objs	+= evr.o evr-irq-events.o evr-dbg.o 
evrma-test-objs	+= plx.o pci-evr.o 
evrma-test-objs	+= evr-sim.o event-list.o

obj-m += evrma-test.o

compile:
	$(MAKE) ARCH=x86 CROSS_COMPILE=$(XCROSS_HOME) -C $(KERNELDIR) M=$(PWD) modules

clean:
	rm -rf *.o *~ core .depend .*.cmd *.ko *.mod.c .tmp_versions *.c *.h
 
install:
	# nothing to do at the moment
	
	
	

