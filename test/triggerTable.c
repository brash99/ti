/*
 * File:
 *    triggerTable
 *
 * Description:
 *    Test trigger table creation.
 *
 *
 */


#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include "jvme.h"
#include "tiLib.h"

#define TS(x) (1 << (x-1))

int
main(int argc, char *argv[])
{
  uint32_t ielement = 0;
  uint32_t imask = 0, testmask = 0, testmask2 = 0;
  uint32_t table[16];

  for (imask = 1; imask <= 0x3F; imask++)
    {
      /* evtype = 1:  (TS1 || TS2 || TS3) && nothing else */
      testmask = TS(1) | TS(2) | TS(3);
      if( ((imask & testmask) != 0) &&
	  ((imask & ~testmask) == 0))
	{
	  tiDefineEventType(imask, 1, 1);
	  continue;
	}
      /* evtype = 2:  (TS4 || TS5) && nothing else */
      testmask = TS(4) | TS(5);
      if( ((imask & testmask) != 0) &&
	  ((imask & ~testmask) == 0))
	{
	  tiDefineEventType(imask, 1, 2);
	  continue;
	}
      /* evtype = 3:  [(TS1 || TS2 || TS3 ) && (TS4 || TS5)]  && nothing else as  */
      testmask  = TS(1) | TS(2) | TS(3);
      testmask2 = TS(4) | TS(5);
      if( ((imask & testmask) != 0) &&
	  ((imask & testmask2) != 0) &&
	  ((imask & ~(testmask | testmask2)) == 0))
	{
	  tiDefineEventType(imask, 1, 3);
	  continue;
	}

      /* evtype = 4:  (TS6 || anything) */
      testmask = TS(6);
      if( (imask & testmask) != 0)
	{
	  tiDefineEventType(imask, 1, 4);
	  continue;
	}

      /* Everything else? */
      tiDefineEventType(imask, 1, 255);
    }


  tiPrintTriggerTable(1);

  tiGetTriggerTable((uint32_t *)&table);

  for (ielement = 0; ielement < 16; ielement++)
    printf("%2d: 0x%08x\n", ielement, table[ielement]);

  exit(0);
}

/*
  Local Variables:
  compile-command: "make -k -B triggerTable"
  End:
 */
