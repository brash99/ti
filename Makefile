#
# File:
#    Makefile
#
# Description:
#    Makefile for the TI (v3) Library using a VME Controller running Linux
#
BASENAME=ti
#
# Uncomment DEBUG line, to include some debugging info ( -g and -Wall)
DEBUG	?= 1
QUIET	?= 1

#
ifeq ($(QUIET),1)
        Q = @
else
        Q =
endif

ARCH	?= $(shell uname -m)

# Check for CODA 3 environment
ifdef CODA_VME

INC_CODA	= -I${CODA_VME}/include
LIB_CODA	= -L${CODA_VME_LIB}

endif

# Defs and build for PPC using VxWorks
ifeq (${ARCH}, PPC)
OS			= VXWORKS
VXWORKS_ROOT		?= /site/vxworks/5.5/ppc/target

ifdef LINUXVME_INC
VME_INCLUDE             ?= -I$(LINUXVME_INC)
endif

CC			= ccppc
LD			= ldppc
DEFS			= -mcpu=604 -DCPU=PPC604 -DVXWORKS -D_GNU_TOOL -mlongcall \
				-fno-for-scope -fno-builtin -fvolatile -DVXWORKSPPC
INCS			= -I. -I$(VXWORKS_ROOT)/h  \
				$(VME_INCLUDE) ${INC_CODA}
CFLAGS			= $(INCS) $(DEFS)
else
OS			= LINUX
endif

# Defs and build for i686, x86_64 Linux
ifeq ($(OS),LINUX)

# Safe defaults
LINUXVME_LIB		?= ../lib
LINUXVME_INC		?= ../include

CC			= gcc
CXX			= g++
ifeq ($(ARCH),i686)
CC			+= -m32
endif
AR                      = ar
RANLIB                  = ranlib
CFLAGS			= -fpic -L. -L${LINUXVME_LIB} ${LIB_CODA}
INCS			= -I. -I${LINUXVME_INC} ${INC_CODA}

LIBS			= lib${BASENAME}.a lib${BASENAME}.so
endif #OS=LINUX#

ifdef DEBUG
CFLAGS			+= -Wall -Wno-unused -g
else
CFLAGS			+= -O2
endif

SRC			= ${BASENAME}Lib.c ${BASENAME}Config.cpp
HDRS			= ${BASENAME}Lib.h ${BASENAME}Config.h
OBJ			= $(HDRS:%.h=%.o)

DEPDIR			:= .deps
DEPFLAGS		= -MT $@ -MMD -MP -MF $(DEPDIR)/$*.d
DEPFILES		:= $(OBJ:%.o=$(DEPDIR)/%.d)

ifeq ($(OS),LINUX)
all: echoarch ${LIBS}
else
all: echoarch $(OBJ)
endif

%.o: %.c
%.o: %.c $(DEPDIR)/%.d | $(DEPDIR)
	@echo " CC     $@"
	${Q}$(CC) $(DEPFLAGS) $(CFLAGS) $(INCS) -fPIC -c -o $@ $<

%.o: %.cpp
%.o: %.cpp $(DEPDIR)/%.d | $(DEPDIR)
	@echo " CXX    $@"
	${Q}$(CXX) $(DEPFLAGS) $(CFLAGS) -std=c++11 $(INCS) -fPIC -c -o $@ $<

%.so: $(OBJ)
	@echo " CC     $@"
	${Q}$(CC) -shared $(CFLAGS) $(INCS) -o $@ $(OBJ)

%.a: $(OBJ)
	@echo " AR     $@"
	${Q}$(AR) ru $@ $<
	@echo " RANLIB $@"
	${Q}$(RANLIB) $@

ifeq ($(OS),LINUX)
install: $(LIBS)
	@echo " CP     $<"
	${Q}cp $(PWD)/$< $(LINUXVME_LIB)/$<
	@echo " CP     $(<:%.a=%.so)"
	${Q}cp $(PWD)/$(<:%.a=%.so) $(LINUXVME_LIB)/$(<:%.a=%.so)
	@echo " CP     ${BASENAME}Lib.h"
	${Q}cp ${PWD}/${BASENAME}Lib.h $(LINUXVME_INC)
	@echo " CP     ${BASENAME}Config.h"
	${Q}cp ${PWD}/${BASENAME}Config.h $(LINUXVME_INC)

coda_install: $(LIBS)
	@echo " CODACP $<"
	${Q}cp $(PWD)/$< $(CODA_VME_LIB)/$<
	@echo " CODACP $(<:%.a=%.so)"
	${Q}cp $(PWD)/$(<:%.a=%.so) $(CODA_VME_LIB)/$(<:%.a=%.so)
	@echo " CODACP ${BASENAME}Lib.h"
	${Q}cp ${PWD}/${BASENAME}Lib.h $(CODA_VME)/include
	@echo " CODACP ${BASENAME}Config.h"
	${Q}cp ${PWD}/${BASENAME}Config.h $(CODA_VME)/include


endif

$(DEPDIR): ; @mkdir -p $@

$(DEPFILES):
include $(wildcard $(DEPFILES))
clean:
	@rm -vf ${OBJ}

echoarch:
	@echo "Make for $(OS)-$(ARCH)"

.PHONY: clean echoarch
