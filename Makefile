ifneq ($(KERNELRELEASE),)
obj-m := armdsp.o
else

# for the development host =======================================
ARMDSP_DIR=/usr/local/armdsp

ARMDSP_NFSROOT=/arm

# for compiling arm kernel module ================================
KERNELDIR = /opt/ti/linux-03.20.00.11/
# KERNELDIR = /opt/hawkboard/linux-omapl1/
PWD := $(shell pwd)
ARCH=arm
CROSS_COMPILE=arm-none-linux-gnueabi-
export ARCH CROSS_COMPILE

# for compiling user level arm programs ==========================
ARMCC = $(CROSS_COMPILE)gcc -g -Wall

# for compiling dsp programs =====================================
CL6X = cl6x
CL6X_FLAGS = --abi=eabi \
	--symdebug:none \
	--printf_support=minimal \
	--mem_model:const=far \
	--mem_model:data=far

.SUFFIXES: .obj .asm

.c.obj:
	$(CL6X) $(CL6X_FLAGS) -c $*.c

.asm.obj:
	$(CL6X) $(CL6X_FLAGS) -c $*.asm
# ================================================================

all: armdsp.ko dsptest.dsp rundsp armhost

armdsp.ko: armdsp.c armdsp.h
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

DSPTEST_OBJS = vecs.obj dsptrg.obj dsptest.obj
dsptest.dsp: $(DSPTEST_OBJS) armdsp-link.cmd
	$(CL6X) $(CL6X_FLAGS) -z armdsp-link.cmd $(DSPTEST_OBJS) \
		--output_file dsptest.elf
	hex6x -q -m --order=M --romwidth=32 -o=dsptest.dsp dsptest.elf

rundsp: rundsp.c armdsp.h
	$(ARMCC) -o rundsp rundsp.c

armhost: armhost.c armdsp.h
	$(ARMCC) -o armhost armhost.c

install: all
	mkdir -p -m 755 $(ARMDSP_DIR)/bin
	mkdir -p -m 755 $(ARMDSP_DIR)/arm
	mkdir -p -m 755 $(ARMDSP_DIR)/dsp
	install -c -m 755 armdsp-link $(ARMDSP_DIR)/bin/.
	install -c -m 644 Makefile.armdsp $(ARMDSP_DIR)/.
	install -c -m 644 armdsp.ko $(ARMDSP_DIR)/arm/.
	install -c -m 755 rundsp $(ARMDSP_DIR)/arm/.
	install -c -m 755 armhost $(ARMDSP_DIR)/arm/.
	install -c -m 755 armdsp-ldmod $(ARMDSP_DIR)/arm/.
	install -c -m 644 dsptest.dsp $(ARMDSP_DIR)/dsp/.
	install -c -m 644 vecs.obj $(ARMDSP_DIR)/dsp/.
	install -c -m 644 dsptrg.obj $(ARMDSP_DIR)/dsp/.
	install -c -m 644 armdsp-link.cmd $(ARMDSP_DIR)/dsp/.

test: all
	install -c -m 644 armdsp.ko $(ARMDSP_NFSROOT)/.
	install -c -m 755 rundsp $(ARMDSP_NFSROOT)/.
	install -c -m 755 armhost $(ARMDSP_NFSROOT)/.
	install -c -m 755 armdsp-ldmod $(ARMDSP_NFSROOT)/.
	install -c -m 644 dsptest.dsp $(ARMDSP_NFSROOT)/.

clean:
	rm -f *.o *.ko *.obj *.elf *.mod.c *~ .*~ ?
	rm -f rundsp armhost dsptest.dsp dsptest.elf
	rm -f Module.symvers modules.order
	rm -rf .tmp_versions .*.cmd
	$(MAKE) -C example clean

endif
