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
  int onoff = 0;

  if (argc > 1)
    {
      slot = atoi(argv[1]);

      if (argc == 3)
	onoff = atoi(argv[2]);
      else
	onoff = 0;

      if ((slot < 0) || (slot > 22))
	{
	  printf("invalid slot... using 21");
	  slot = 21;
	}
    }
  else
    slot = 21;

  printf("\nJLAB TI Set Output Port... slot = %d  port = %d\n",
	 slot, onoff);
  printf("------------------------------------------------------\n");

  vmeOpenDefaultWindows();
  vmeBusLock();

  /* Set the TI structure pointer */
  stat = tiInit((slot << 19), TI_READOUT_EXT_POLL, TI_INIT_NO_INIT);
  if (stat != OK)
    goto CLOSE;

  switch (onoff)
    {
    case 1:
      printf("  Out 1\n");
      tiSetOutputPort(1, 0, 0, 0);
      break;

    case 2:
      printf("  Out 2\n");
      tiSetOutputPort(0, 1, 0, 0);
      break;

    case 3:
      printf("  Out 3\n");
      tiSetOutputPort(0, 0, 1, 0);
      break;

    case 4:
      printf("  Out 4\n");
      tiSetOutputPort(0, 0, 0, 1);
      break;

    default:
      printf("  All Off\n");
      tiSetOutputPort(0, 0, 0, 0);
      break;

    }
  tiSetOutputPort(0, 0, 0, 0);


CLOSE:
  vmeBusUnlock();

  vmeCloseDefaultWindows();

  exit(0);
}

/*
  Local Variables:
  compile-command: "make -k -B tiSetOutput"
  End:
 */
