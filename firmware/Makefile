#
# File:
#    Makefile
#
# Description:
#    Makefile for the JLAB extensions to the GEFANUC Controller API
#
#
# $Date$
# $Rev$
#
DEBUG=1

ifndef ARCH
	ifdef LINUXVME_LIB
		ARCH=Linux
	else
		ARCH=VXWORKSPPC
	endif
endif

FWFILES	= $(shell cat .currentfw)

# Defs and build for VxWorks
ifeq ($(ARCH),VXWORKSPPC)
VXWORKS_ROOT = /site/vxworks/5.5/ppc/target

CC			= ccppc
LD			= ldppc
DEFS			= -mcpu=604 -DCPU=PPC604 -DVXWORKS -D_GNU_TOOL -mlongcall \
				-fno-for-scope -fno-builtin -fvolatile -DVXWORKSPPC
INCS			= -I. -I../ -I$(VXWORKS_ROOT)/h -I$(VXWORKS_ROOT)/h/rpc -I$(VXWORKS_ROOT)/h/net
CFLAGS			= $(INCS) $(DEFS)
OBJS			= tiFirmwareUpdate.o
PROGS			=
endif #ARCH=VXWORKSPPC#

# Defs and build for Linux
ifeq ($(ARCH),Linux)

LINUXVME_LIB	?= ../../libs
LINUXVME_INC	?= ../../include
LINUXVME_BIN	?= ../../bin
LINUXVME_FW	?= ../../firmware

CC			= $(CROSS_COMPILE)gcc
AR                      = ar
RANLIB                  = ranlib
CFLAGS			= -I. -I${LINUXVME_INC} \
			  -L. -L${LINUXVME_LIB}
ifdef DEBUG
CFLAGS			+= -Wall -g
else
CFLAGS			+= -O2
endif
OBJS			=
PROGS			= tiFirmwareUpdate
endif

ifeq ($(ARCH),VXWORKSPPC)
all: $(OBJS)
else
all: $(PROGS)
endif

clean:
	@rm -vf $(PROGS) $(OBJS) *~

%: %.c
	@echo "Making $@ for $(ARCH)"
	$(CC) $(CFLAGS) -o $@ $(@:%=%.c) -lrt -ljvme -lti 

ifeq ($(ARCH),VXWORKSPPC)
tiFirmwareUpdate.o: tiFirmwareUpdate.c
	@echo "Making $@ for $(ARCH)"
	$(CC) $(CFLAGS) -c -DTEMPE -o $@ $(<)
	$(CC) $(CFLAGS) -c -o $(@:.o=_univ.o) $(<)
else
install: $(PROGS)
	@echo "Installing $(PROGS)"
	@install -v -d $(LINUXVME_BIN)
	@install -v -p $(PROGS) -D $(LINUXVME_BIN)/

fwinstall:
	@ln -sf $(FWFILES) ti_current_firmware.svf
	@install -v -d $(LINUXVME_FW)/ti
	@install -p -m 664 -v $(FWFILES) ti_current_firmware.svf -D $(LINUXVME_FW)/ti

endif

.PHONY: all clean
