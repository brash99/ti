/*
 * File:
 *    tiLibTest.c
 *
 * Description:
 *    Test Vme TI interrupts with GEFANUC Linux Driver
 *    and TI library
 *
 *
 */


#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "jvme.h"
#include "tiLib.h"
#include "sdLib.h"

int 
main(int argc, char *argv[]) {

    int stat;
    int slot;

    if(argc>1)
      {
	slot = atoi(argv[1]);
	if(slot<1 || slot>22)
	  {
	    printf("invalid slot... using 21");
	    slot=21;
	  }
      }
    else 
      slot=21;

    printf("\nJLAB TI Status... slot = %d\n",slot);
    printf("----------------------------\n");

    vmeOpenDefaultWindows();

    /* Set the TI structure pointer */
    tiInit((slot<<19),TI_READOUT_EXT_POLL,1);
    tiStatus();

    sdInit();
    sdStatus();

 CLOSE:

    vmeCloseDefaultWindows();

    exit(0);
}

