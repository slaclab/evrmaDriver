RHEL = rhel7
K_VERS := $(wildcard /lib/modules/*/build)
K_VERS := $(patsubst %/,%,$(dir $(K_VERS)))
K_VERS := $(notdir $(K_VERS))

LINUX_VERSION=rhel7-x86_64
HEADERS:=../../$(LINUX_VERSION)

all: $(K_VERS) $(HEADERS)


evrma-objs	+= main_evrma.o mng-dev.o virt-dev.o rm.o packet-queue.o
evrma-objs	+= evr.o evr-irq-events.o evr-dbg.o
evrma-objs	+= plx.o pci-evr.o
evrma-objs	+= evr-sim.o event-list.o

obj-m += evrma.o

.PHONY: $(K_VERS) $(HEADERS)

$(HEADERS):
	mkdir -p $@/include
	cp  linux-evrma.h linux-evr-regs.h linux-modac.h  $@/include/.

$(K_VERS): OUTPUT_DIR=../../$@

$(K_VERS):
	### copy the sources to make a directory for building
	cp -r ../../src/* .
	$(MAKE) module_load
	chmod a+x module_*
	$(MAKE) -C /lib/modules/$@/build M=$(PWD) modules
	mkdir -p $(OUTPUT_DIR)/include/
	cp  linux-evrma.h linux-evr-regs.h linux-modac.h  $(OUTPUT_DIR)/include/.
	cp  *.ko $(OUTPUT_DIR)/.
	cp module_* $(OUTPUT_DIR)/.

install: OUTPUT_DIR=../../$(LINUX_VERSION)
install:
	mkdir -p $(OUTPUT_DIR)/include/
	cp  linux-evrma.h linux-evr-regs.h linux-modac.h  $(OUTPUT_DIR)/include/.
	cp  *.ko $(OUTPUT_DIR)/.
	cp module_* $(OUTPUT_DIR)/.

module_load: original.module_load
	sed 's/RHEL/$(RHEL)/' $< >$@

clean:
	rm -rf *.o *~ core .depend .*.cmd *.ko *.ko.unsigned *.mod.c .tmp_versions *.symvers  *.order *.c *.h module_* original.*

