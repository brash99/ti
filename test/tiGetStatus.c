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

int
main(int argc, char *argv[])
{

  int stat;
  int slot;

  if (argc > 1)
    {
      slot = atoi(argv[1]);
      if (slot < 0 || slot > 22)
	{
	  printf("invalid slot... will scan");
	  slot = 0;
	}
    }
  else
    slot = 0;

  printf("\nJLAB TI Status... slot = %d\n", slot);
  printf("----------------------------\n");

  vmeSetQuietFlag(1);
  stat = vmeOpenDefaultWindows();

  if(stat != OK)
    goto CLOSE;

  vmeBusLock();
  /* Set the TI structure pointer */
  stat = tiInit(slot << 19, 0, TI_INIT_SKIP_FIRMWARE_CHECK | TI_INIT_NO_INIT);
  if(stat != OK)
    goto CLOSE;

  stat = tiCheckAddresses();
  if(stat != OK)
    goto CLOSE;

  printf("Firmware version = 0x%x\n", tiGetFirmwareVersion());

  tiStatus(1);

CLOSE:
  vmeBusUnlock();

  vmeCloseDefaultWindows();

  exit(0);
}
