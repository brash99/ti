#
# File:
#    Makefile
#
# Description:
#    Makefile for the TI (v3) Library using a VME Controller running Linux
#
# SVN: $Rev$
#
# Uncomment DEBUG line, to include some debugging info ( -g and -Wall)
DEBUG=1
#
ARCH=Linux
ifndef ARCH
  ARCH=VXWORKSPPC
endif

# Defs and build for VxWorks
ifeq ($(ARCH),VXWORKSPPC)
VXWORKS_ROOT = /site/vxworks/5.5/ppc/target

CC			= ccppc
LD			= ldppc
DEFS			= -mcpu=604 -DCPU=PPC604 -DVXWORKS -D_GNU_TOOL -mlongcall \
				-fno-for-scope -fno-builtin -fvolatile -DVXWORKSPPC
INCS			= -I. -I$(VXWORKS_ROOT)/h -I$(VXWORKS_ROOT)/h/rpc -I$(VXWORKS_ROOT)/h/net
CFLAGS			= $(INCS) $(DEFS)

endif #ARCH=VXWORKSPPC#

# Defs and build for Linux
ifeq ($(ARCH),Linux)
LINUXVME_LIB		?= ${CODA}/extensions/linuxvme/libs
LINUXVME_INC		?= ${CODA}/extensions/linuxvme/include

CC			= gcc
AR                      = ar
RANLIB                  = ranlib
CFLAGS			= -I. -I${LINUXVME_INC} -I/usr/include \
				-L${LINUXVME_LIB} -L.

LIBS			= libti.a
endif #ARCH=Linux#

ifdef DEBUG
CFLAGS			+= -Wall -g
else
CFLAGS			+= -O2
endif
MAINSRC			= tiLib.c
SRC			= tiLib.c sdLib.c gtpLib.c ctpLib.c
HDRS			= $(SRC:.c=.h)
OBJ			= tiLib.o

ifeq ($(ARCH),Linux)
all: echoarch $(LIBS) links
else
all: echoarch $(OBJ) copy
endif

$(OBJ): $(SRC) $(HDRS)
	$(CC) $(CFLAGS) -c -o $@ $(MAINSRC)

$(LIBS): $(OBJ)
	$(CC) -fpic -shared $(CFLAGS) -o $(@:%.a=%.so) $(MAINSRC)
	$(AR) ruv $@ $<
	$(RANLIB) $@

ifeq ($(ARCH),Linux)
links: $(LIBS)
	@ln -vsf $(PWD)/$< $(LINUXVME_LIB)/$<
	@ln -vsf $(PWD)/$(<:%.a=%.so) $(LINUXVME_LIB)/$(<:%.a=%.so)
	@ln -vsf ${PWD}/*Lib.h $(LINUXVME_INC)

tiEMload: tiEMload.c
	$(CC) $(CFLAGS) -o $@ $(@:%=%.c) $(LIBS_$@) -lrt -ljvme -lti
else
copy: $(OBJ)
	cp $< vx/
endif

clean:
	@rm -vf *.{o,a,so}

echoarch:
	@echo "Make for $(ARCH)"

.PHONY: clean echoarch
