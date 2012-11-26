/*
 * File:
 *    tid_reloadFPGA.c
 *
 * Description:
 *    Reload FPGA Firmware on the TID
 *
 *
 */


#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "jvme.h"
#include "tiLib.h"

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

    printf("\nReload FPGA Firmware on the TI in slot %d\n",slot);
    printf("----------------------------\n");

    vmeOpenDefaultWindows();

    stat = tiInit((slot<<19),0,0);
    if(stat<0)
      goto CLOSE;


    tiReload();

    sleep(1);

    tiStatus();
 CLOSE:


    vmeCloseDefaultWindows();

    exit(0);
}

