KERNELDIR ?= /lib/modules/$(shell uname -r)/build

all:
	### copy the sources to make a directory for building
	cp -r ../src/* .
	$(MAKE) compile
	

evrma-objs	+= main_evrma.o mng-dev.o virt-dev.o rm.o packet-queue.o 
evrma-objs	+= evr.o evr-irq-events.o evr-dbg.o 
evrma-objs	+= plx.o pci-evr.o 
evrma-objs	+= evr-sim.o event-list.o

obj-m += evrma.o

compile:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

clean:
	rm -rf *.o *~ core .depend .*.cmd *.ko *.mod.c .tmp_versions *.c *.h
 
install:
	# nothing to do at the moment
	

	
	

