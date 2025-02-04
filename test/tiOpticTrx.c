/*
 * File:
 *    tiOpticTrx.c
 *
 * Description:
 *    Print the optical transceiver status to standard out.
 *
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "jvme.h"
#include "tiLib.h"

extern volatile struct TI_A24RegStruct *TIp;
void I2CopticTrx();

int
main(int argc, char *argv[])
{
  int slot;
  uint32_t sn = 0;

  if(argc > 1)
    {
      slot = atoi(argv[1]);
      if((slot < 0) || (slot > 22))
	{
	  printf("invalid slot... using 21");
	  slot=21;
	}
    }
  else
    slot=21;

  printf("\n");
  vmeSetQuietFlag(1);;
  vmeOpenDefaultWindows();

  if(tiInit(slot, TI_READOUT_EXT_POLL, TI_INIT_NO_INIT) == ERROR)
    goto CLOSE;

  /* Check for proper serial number ... 152 - 175 */
  sn = tiGetSerialNumber(NULL) & 0xffff;
  if((sn >= 152) && (sn <= 175))
    {
      I2CopticTrx();
    }
  else
    {
      printf("ERROR: TI-%d is not hardware compatible to use this routine\n",
	     sn);
    }

 CLOSE:

  vmeCloseDefaultWindows();

  printf("\n");
  exit(0);
}

void
cpuDelay(int delay)
{
  usleep((int)(delay/90));
}

void
I2CopticTrx()
{
  int ibyte, itr, Tempt = 0, Volt = 0;
  int rxPower[4] = {0,0,0,0}, txBias[4] = {0,0,0,0};
  int txDisable = 0;
  int nreadbytes = 21;
  uint16_t readbytes[21] =
    {
      22, 23, /* Temp */
      26, 27, /* Voltage */
      34, 35, 36, 37, 38, 39, 40, 41, /* rxPower */
      42, 43, 44, 45, 46, 47, 48, 49, /* txBias */
      86 /* Tx? Disable */
    };
  uint32_t ReadVal;
  int i, maxtr = 8;
  uint32_t *i2cOptp = (uint32_t *)((u_long)TIp + 0x50000);
  uint32_t vmeControl_old = 0;

  vmeBusLock();

  /* set the device address to 0xA0# */
  vmeControl_old = vmeRead32(&TIp->vmeControl);
  vmeWrite32(&TIp->vmeControl, 0x00000111);

  for (itr = 0; itr < maxtr; itr++) // loop over the eight transceivers
    {
      vmeWrite32(&TIp->fiber, 0x1ff - ((1 << itr) & 0xff));

      for (ibyte = 0; ibyte < nreadbytes; ibyte++) // loop over bytes
	{

	  ReadVal = vmeRead32(&i2cOptp[(0xC00 + readbytes[ibyte]*4)>>2]);
	  cpuDelay(99100);

	  if (readbytes[ibyte] == 22) Tempt = (ReadVal&0xff) << 8;
	  if (readbytes[ibyte] == 23) Tempt |= (ReadVal&0xff);

	  if (readbytes[ibyte] == 26) Volt = (ReadVal & 0xff) << 8;
	  if (readbytes[ibyte] == 27) Volt |= ReadVal&0xff;

	  if (readbytes[ibyte] == 34) rxPower[0] = (ReadVal & 0xff) << 8;
	  if (readbytes[ibyte] == 35) rxPower[0] |= (ReadVal & 0xff);

	  if (readbytes[ibyte] == 36) rxPower[1] = (ReadVal & 0xff) << 8;
	  if (readbytes[ibyte] == 37) rxPower[1] |= (ReadVal & 0xff);

	  if (readbytes[ibyte] == 38) rxPower[2] = (ReadVal & 0xff) << 8;
	  if (readbytes[ibyte] == 39) rxPower[2] |= (ReadVal & 0xff);

	  if (readbytes[ibyte] == 40) rxPower[3] = (ReadVal & 0xff) << 8;
	  if (readbytes[ibyte] == 41) rxPower[3] |= (ReadVal & 0xff);

	  if (readbytes[ibyte] == 42) txBias[0] = (ReadVal & 0xff) << 8;
	  if (readbytes[ibyte] == 43) txBias[0] |= (ReadVal & 0xff);

	  if (readbytes[ibyte] == 44) txBias[1] = (ReadVal & 0xff) << 8;
	  if (readbytes[ibyte] == 45) txBias[1] |= (ReadVal & 0xff);

	  if (readbytes[ibyte] == 46) txBias[2] = (ReadVal & 0xff) << 8;
	  if (readbytes[ibyte] == 47) txBias[2] |= (ReadVal & 0xff);

	  if (readbytes[ibyte] == 48) txBias[3] = (ReadVal & 0xff) << 8;
	  if (readbytes[ibyte] == 49) txBias[3] |= (ReadVal & 0xff);

	  if (readbytes[ibyte] == 86) txDisable = (ReadVal & 0xff);
	}

      printf("\n Optic Transceiver #%d ", itr+1);
      if(Volt == rxPower[0]) /* Reads back 0xffffffff */
	{
	  printf(" - N/A\n");
	  continue;
	}


      /* Convert register values to physical units */
      Volt /= 10; /* mV */
      Tempt /= 256;
      for(i = 0; i < 4; i++)
	{
	  rxPower[i] /= 10;
	  txBias[i] *= 2;
	}

      printf("\n   Module Temp : %7d C    Supply Volt : %6d mV \n", Tempt, Volt);
      printf("   tx0 Disable : %7d      tx1 Disable : %6d\n",
	     txDisable & (1<<0),
	     txDisable & (1<<1));
      printf("   tx2 Disable : %7d      tx3 Disable : %6d\n",
	     txDisable & (1<<2),
	     txDisable & (1<<3));

      for(i = 0; i < 4; i++)
	printf("   rxPower[%d]  : %7d uW   txBias[%d]   : %6d uA\n",
	       i, rxPower[i], i, txBias[i]);

      fflush(stdout);
      sleep(1);
    }
  vmeWrite32(&TIp->vmeControl, vmeControl_old);

  vmeBusUnlock();
}
