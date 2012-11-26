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

LINUXVME_LIB	?= ${CODA}/extensions/linuxvme/libs
LINUXVME_INC	?= ${CODA}/extensions/linuxvme/include

CROSS_COMPILE		=
CC			= $(CROSS_COMPILE)gcc
AR                      = ar
RANLIB                  = ranlib
CFLAGS			= -Wall -O2 -I${LINUXVME_INC} -I. -I/usr/include \
			  -L${LINUXVME_LIB} -L.

PROGS			= tiLibTest

all: $(PROGS)

clean distclean:
	@rm -f $(PROGS) *~ *.so

%: %.c
	echo "Making $@"
	$(CC) $(CFLAGS) -o $@ $(@:%=%.c) $(LIBS_$@) -lrt -ljvme -lti

.PHONY: all clean distclean
