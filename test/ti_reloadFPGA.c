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
#include <unistd.h>
#include "jvme.h"
#include "tiLib.h"

static void EMFPGAreload();
static void Emergency(unsigned int jtagType, unsigned int numBits,
		      unsigned long *jtagData);
extern volatile struct TI_A24RegStruct *TIp;

int
main(int argc, char *argv[])
{

  int stat;
  int slot;
  /* unsigned int rval = 0; */
  unsigned long laddr = 0;

  if (argc > 1)
    {
      slot = atoi(argv[1]);
      if ((slot < 0) || (slot > 22))	// || (slot!=0))
	{
	  printf("invalid slot... will scan,");
	  slot = 0;
	}
    }
  else
    slot = 0;

  printf("\nReload FPGA Firmware on the TI in slot %d\n", slot);
  printf("----------------------------\n");

  vmeSetQuietFlag(1);

  vmeOpenDefaultWindows();

  stat =
    tiInit((slot << 19), 0, TI_INIT_SKIP_FIRMWARE_CHECK | TI_INIT_NO_INIT);
  if (stat < 0)
    {
      if (slot == 0)
	goto CLOSE;
      else
	{
	  stat =
	    vmeBusToLocalAdrs(0x39, (char *) (unsigned long) (slot << 19),
			      (char **) &laddr);
	  if (stat != 0)
	    {
	      printf("%s: ERROR: Error in vmeBusToLocalAdrs res=%d \n",
		     __FUNCTION__, stat);
	      goto CLOSE;
	    }
	  TIp = (struct TI_A24RegStruct *) laddr;

#ifdef CHECKREAD
	  stat = vmeMemProbe((char *) (&TIp->boardID), 4, (char *) &rval);
	  if (stat != 0)
	    {
	      printf("%s: ERROR: TI card not addressable\n", __FUNCTION__);
	      TIp = NULL;
	      // A24 address modifier reset
	      goto CLOSE;
	    }
#endif
	}
    }

  printf("... RELOAD!\n");
  EMFPGAreload();

  sleep(1);

CLOSE:


  vmeCloseDefaultWindows();

  exit(0);
}

void
EMFPGAreload()
{
  unsigned long Dataword[1];

  //A24 Address modifier redefined
#ifdef VXWORKS
#ifdef TEMPE
  sysTempeSetAM(2, 0x19);
#else /* Universe */
  sysUnivSetUserAM(0x19, 0);
  sysUnivSetLSI(2, 6);
#endif /*TEMPE*/
#else
  vmeBusLock();
  vmeSetA24AM(0x19);
#endif

  Dataword[0] = 0;
  Emergency(0, 0, Dataword);	// Reset the PROM JTAG engine
  Dataword[0] = 0xee;
  Emergency(1, 16, Dataword);	//pulse the CF pin on PROM by emergency logic;

  // A24 address modifier reset
#ifdef VXWORKS
#ifdef TEMPE
  sysTempeSetAM(2, 0);
#else
  sysUnivSetLSI(2, 1);
#endif /*TEMPE*/
#else
  vmeSetA24AM(0);
  vmeBusUnlock();
#endif

}

static void
Emergency(unsigned int jtagType, unsigned int numBits,
	  unsigned long *jtagData)
{
/*   unsigned long *laddr; */
  unsigned int iloop, iword, ibit;
  unsigned long shData;

#ifdef DEBUG
  int numWord, i;
  printf("type: %x, num of Bits: %x, data: \n", jtagType, numBits);
  numWord = (numBits - 1) / 32 + 1;
  for (i = 0; i < numWord; i++)
    {
      printf("%08x", jtagData[numWord - i - 1]);
    }
  printf("\n");
#endif

  if (jtagType == 0)		//JTAG reset, TMS high for 5 clcoks, and low for 1 clock;
    {
      for (iloop = 0; iloop < 5; iloop++)
	{
	  vmeWrite32(&TIp->eJTAGLoad, 1);
	}

      vmeWrite32(&TIp->eJTAGLoad, 0);
    }
  else if (jtagType == 1)	// JTAG instruction shift
    {
      // Shift_IR header:
      vmeWrite32(&TIp->eJTAGLoad, 0);
      vmeWrite32(&TIp->eJTAGLoad, 1);
      vmeWrite32(&TIp->eJTAGLoad, 1);
      vmeWrite32(&TIp->eJTAGLoad, 0);
      vmeWrite32(&TIp->eJTAGLoad, 0);

      for (iloop = 0; iloop < numBits; iloop++)
	{
	  iword = iloop / 32;
	  ibit = iloop % 32;
	  shData = ((jtagData[iword] >> ibit) << 1) & 0x2;
	  if (iloop == numBits - 1)
	    shData = shData + 1;	//set the TMS high for last bit to exit Shift_IR
	  vmeWrite32(&TIp->eJTAGLoad, shData);
	}

      // shift _IR tail
      vmeWrite32(&TIp->eJTAGLoad, 1);
      vmeWrite32(&TIp->eJTAGLoad, 0);
    }
  else if (jtagType == 2)	// JTAG data shift
    {
      //shift_DR header
      vmeWrite32(&TIp->eJTAGLoad, 0);
      vmeWrite32(&TIp->eJTAGLoad, 1);
      vmeWrite32(&TIp->eJTAGLoad, 0);
      vmeWrite32(&TIp->eJTAGLoad, 0);

      for (iloop = 0; iloop < numBits; iloop++)
	{
	  iword = iloop / 32;
	  ibit = iloop % 32;
	  shData = ((jtagData[iword] >> ibit) << 1) & 0x2;
	  if (iloop == numBits - 1)
	    shData = shData + 1;	//set the TMS high for last bit to exit Shift_DR
	  vmeWrite32(&TIp->eJTAGLoad, shData);
	}

      // shift _DR tail
      vmeWrite32(&TIp->eJTAGLoad, 1);	// update Data_Register
      vmeWrite32(&TIp->eJTAGLoad, 0);	// back to the Run_test/Idle
    }
  else if (jtagType == 3)	// JTAG instruction shift, stop at IR-PAUSE state, though, it started from IDLE
    {
      // Shift_IR header:
      vmeWrite32(&TIp->eJTAGLoad, 0);
      vmeWrite32(&TIp->eJTAGLoad, 1);
      vmeWrite32(&TIp->eJTAGLoad, 1);
      vmeWrite32(&TIp->eJTAGLoad, 0);
      vmeWrite32(&TIp->eJTAGLoad, 0);

      for (iloop = 0; iloop < numBits; iloop++)
	{
	  iword = iloop / 32;
	  ibit = iloop % 32;
	  shData = ((jtagData[iword] >> ibit) << 1) & 0x2;
	  if (iloop == numBits - 1)
	    shData = shData + 1;	//set the TMS high for last bit to exit Shift_IR
	  vmeWrite32(&TIp->eJTAGLoad, shData);
	}

      // shift _IR tail
      vmeWrite32(&TIp->eJTAGLoad, 0);	// update instruction register
      vmeWrite32(&TIp->eJTAGLoad, 0);	// back to the Run_test/Idle
    }
  else if (jtagType == 4)	// JTAG data shift, start from IR-PAUSE, end at IDLE
    {
      //shift_DR header
      vmeWrite32(&TIp->eJTAGLoad, 1);	//to EXIT2_IR
      vmeWrite32(&TIp->eJTAGLoad, 1);	//to UPDATE_IR
      vmeWrite32(&TIp->eJTAGLoad, 1);	//to SELECT-DR_SCAN
      vmeWrite32(&TIp->eJTAGLoad, 0);
      vmeWrite32(&TIp->eJTAGLoad, 0);

      for (iloop = 0; iloop < numBits; iloop++)
	{
	  iword = iloop / 32;
	  ibit = iloop % 32;
	  shData = ((jtagData[iword] >> ibit) << 1) & 0x2;
	  if (iloop == numBits - 1)
	    shData = shData + 1;	//set the TMS high for last bit to exit Shift_DR
	  vmeWrite32(&TIp->eJTAGLoad, shData);
	}

      // shift _DR tail
      vmeWrite32(&TIp->eJTAGLoad, 1);	// update Data_Register
      vmeWrite32(&TIp->eJTAGLoad, 0);	// back to the Run_test/Idle
    }
  else
    {
      printf("\n JTAG type %d unrecognized \n", jtagType);
    }

}
