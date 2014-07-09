#
# File:
#    Makefile
#
# Description:
#    Makefile for the coda primary and secondary readout lists 
#    running on an PPC controller running vxWorks
#
#
# $Date$
# $Rev$
#
# Uncomment DEBUG line for debugging info ( -g and -Wall )
DEBUG=1
#

# DEFs for compiling CODA readout lists
CC			= ccppc
CCRL			= ${CODA_BIN}/ccrl
CFLAGS			= -fno-builtin -D_GNU_TOOL -DVXWORKS -DVXWORKSPPC \
				-mcpu=604 -nostdinc -mlongcall\
				-fno-for-scope \
				-fno-writable-strings \
				-fvolatile -DDAYTIME=\""`date`"\" \
				-O -w -Wall -c -DCPU=PPC604 \
				-I. -I../ -I${CODA}/common/include
ifdef DEBUG
CFLAGS		+= -Wall -g
endif

CRLFILES		= $(wildcard *.crl)
CFILES			= $(CRLFILES:.crl=.c)
OBJS			= $(CRLFILES:.crl=.o)


all: $(OBJS) 

%.c: %.crl
	@echo
	@echo "Converting $< to $@"
	@${CCRL} $<

%.o: %.c
	@echo
	@echo "Building $@ from $<"
	@echo
	${CC} ${CFLAGS} -o $@ $<

clean distclean:
	@rm -f $(OBJS) $(CFILES) *~ 

.PHONY: all
