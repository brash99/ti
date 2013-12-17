/*----------------------------------------------------------------------------*
 *  Copyright (c) 2010        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    Authors: Bryan Moffit                                                   *
 *             moffit@jlab.org                   Jefferson Lab, MS-12B3       *
 *             Phone: (757) 269-5660             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-5800             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*
 *
 * Description:
 *     Status and Control library for the JLAB Signal Distribution
 *     (SD) module using an i2c interface from the JLAB Trigger
 *     Interface (TI) module.
 *
 *   This file is "included" in the tiLib.c
 *
 *----------------------------------------------------------------------------*/

#include <stdlib.h>

/* This is the SD base relative to the TI base VME address */
#define SDBASE 0x40000 

/* Global Variables */
volatile struct SDStruct  *SDp=NULL;    /* pointer to SD memory map */
static int sdTestMode=0;                /* 1 if SD Jumper set to "test" mode */

/* Firmware updating variables */
#ifndef VXWORKSPPC
static unsigned char *progFirmware=NULL;
static size_t progFirmwareSize=0;
/* Maximum firmware size = 1 MB */
#define SD_MAX_FIRMWARE_SIZE 1024*1024
#endif

/*
  sdInit
  - Initialize the Signal Distribution Module

  Arguments:
       flag  - Initialization flags
          bits : description
            0  : Ignore module version

*/
int
sdInit(int flag)
{
  unsigned long tiBase=0, sdBase=0;
  unsigned int version=0;
  int testStatus=0;
  int fIgnoreVersion=0;

  if(TIp==NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  if(flag & SD_INIT_IGNORE_VERSION)
    {
      fIgnoreVersion=1;
      printf("%s: INFO: Initialization without respecting Library-Firmware version\n",
	     __FUNCTION__);
    }

  /* Do something here to verify that we've got good i2c to the SD..
     - maybe just check the status of the clk A and B */
  /* Verify that the SD registers are in the correct space for the TI I2C */
  tiBase = (unsigned long)TIp;
  sdBase = (unsigned long)&(TIp->SWB[0]);
  
  if( (sdBase-tiBase) != SDBASE)
    {
      printf("%s: ERROR: SD memory structure not in correct VME Space!\n",
	     __FUNCTION__);
      printf("   current base = 0x%lx   expected base = 0x%lx\n",
	     sdBase-tiBase, (unsigned long)SDBASE);
      return ERROR;
    }
 
  SDp = (struct SDStruct *)(&TIp->SWB[0]);

  TILOCK;
  version = vmeRead32(&SDp->version);
  testStatus = (vmeRead32(&SDp->csrTest) & SD_CSRTEST_TEST_RESET)>>15;
  TIUNLOCK;

  if(version == 0xffff)
    {
      printf("%s: ERROR: Unable to read SD version (returned 0x%x)\n",
	     __FUNCTION__,version);
      SDp = NULL;
      return ERROR;
    }

  if(version < SD_SUPPORTED_FIRMWARE)
    {
      if(fIgnoreVersion)
	{
	  printf("%s: WARN: SD Firmware Version (0x%x) not supported by this driver.\n",
		 __FUNCTION__,version);
	  printf("           Firmware version 0x%x required. (Ignored)\n",SD_SUPPORTED_FIRMWARE);
	}
      else
	{
	  printf("%s: ERROR: SD Firmware Version (0x%x) not supported by this driver.\n",
		 __FUNCTION__,version);
	  printf("           Firmware version 0x%x required.\n",SD_SUPPORTED_FIRMWARE);
	  SDp = NULL;
	  return ERROR;
	}
    }

  printf("%s: SD (version 0x%x) initialized at Local Base address 0x%lx\n",
	 __FUNCTION__,version,(unsigned long) SDp);
  if(testStatus)
    {
      sdTestMode=1;
      printf("  INFO: **** TEST JUMPER IS INSTALLED ****\n");
    }
  else
    sdTestMode=0;

  return OK;
}

/*
  sdStatus
  - Display status of SD registers

  Arguments:
      rflag : if >0 Show raw register output

*/
int
sdStatus(int rflag)
{
  unsigned int system, status, payloadPorts, tokenPorts, 
    busyoutPorts, trigoutPorts,
    busyoutStatus, trigoutStatus;
  unsigned int version, csrTest;
  unsigned long tiBase, sdBase;
  int ii=0, ibegin=0, iend=0;
  int showVMEslots=1;
  unsigned int vmeslotmask;

  if(SDp==NULL)
    {
      printf("%s: ERROR: SD not initialized\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  system        = vmeRead32(&SDp->system);
  status        = vmeRead32(&SDp->status);
  payloadPorts  = vmeRead32(&SDp->payloadPorts);
  tokenPorts    = vmeRead32(&SDp->tokenPorts);
  busyoutPorts  = vmeRead32(&SDp->busyoutPorts);
  trigoutPorts  = vmeRead32(&SDp->trigoutPorts);
#ifdef OLDMAP
  status2       = vmeRead32(&SDp->status2);
#endif
  busyoutStatus = vmeRead32(&SDp->busyoutStatus);
  trigoutStatus = vmeRead32(&SDp->trigoutStatus);
  version       = vmeRead32(&SDp->version);
  csrTest       = vmeRead32(&SDp->csrTest);

  tiBase = (unsigned long)TIp;
  sdBase = (unsigned long)&(TIp->SWB[0]);

  TIUNLOCK;

  if(showVMEslots==1)
    {
      ibegin=3; iend=21;
    }
  else
    {
      ibegin=1; iend=17;
    }
  
  printf("\n");
  printf("STATUS for SD at TI (Local) base address 0x%08lx (0x%08lx) \n",sdBase-tiBase,sdBase);
  printf("--------------------------------------------------------------------------------\n");
  printf("  Firmware version = 0x%x\n",version);

  if(rflag)
    {
      printf("  System Register  = 0x%04x\n",system);
      printf("  Status Register  = 0x%04x\n",status);
      printf("  CSR Test Reg     = 0x%04x\n",csrTest);
      printf("\n");
  }

  if(system & SD_SYSTEM_TI_LINK_ENABLE)
    printf("  TI Fast Link INACTIVE\n");
  else
    printf("  TI Fast Link ACTIVE\n");
  if(version>=0xa5)
    {
      if( (status & SD_STATUS_POWER_FAULT)==0 ) 
	{
	  printf("  *** Power Fault Detected ***\n");
	}
    }
  else /* Version 0xA4 */
    {
      if( status & SD_STATUS_POWER_FAULT ) 
	{
	  printf("  *** Power Fault Detected ***\n");
	}
    }
  printf("\n");

  printf("  Clock settings:  \n");
  printf("    A: ");
  if(system & SD_SYSTEM_CLKA_BYPASS_MODE)
    printf("Bypass Mode\n");
  else
    {
      printf("PLL set for ");

      switch( (system&SD_SYSTEM_CLKA_FREQUENCY_MASK)>>2 )
	{
	case 0:
	  printf("Undefined");
	  break;
	case 1:
	  printf("31.25 MHz");
	  break;
	case 2:
	  printf("125.00 MHz");
	  break;
	case 3:
	  printf("250.00 MHz");
	  break;
	}
      printf("\n");
    }
  printf("    B: ");
  if(system & SD_SYSTEM_CLKB_BYPASS_MODE)
    printf("Bypass Mode\n");
  else
    {
      printf("PLL set for ");

      switch( (system&SD_SYSTEM_CLKB_FREQUENCY_MASK)>>6 )
	{
	case 0:
	  printf("Undefined");
	  break;
	case 1:
	  printf("31.25 MHz");
	  break;
	case 2:
	  printf("125.00 MHz");
	  break;
	case 3:
	  printf("250.00 MHz");
	  break;
	}
      printf("\n");
    }
  printf("\n");
      
  printf("  Detected Clock:  \n");
  printf("    A: ");
  switch( status&SD_STATUS_CLKA_DETECTED_MASK )
    {
    case SD_STATUS_CLKA_DETECTED_UNKNOWN:
      printf("UNKNOWN");
      break;
    case SD_STATUS_CLKA_DETECTED_31_25:
      printf("31.25 MHz");
      break;
    case SD_STATUS_CLKA_DETECTED_125:
      printf("125.00 MHz");
      break;
    case SD_STATUS_CLKA_DETECTED_250:
      printf("250.00 MHz");
      break;
    }
  printf("\n");
  printf("    B: ");
  switch( (status&SD_STATUS_CLKB_DETECTED_MASK) )
    {
    case SD_STATUS_CLKB_DETECTED_UNKNOWN:
      printf("UNKNOWN");
      break;
    case SD_STATUS_CLKB_DETECTED_31_25:
      printf("31.25 MHz");
      break;
    case SD_STATUS_CLKB_DETECTED_125:
      printf("125.00 MHz");
      break;
    case SD_STATUS_CLKB_DETECTED_250:
      printf("250.00 MHz");
      break;
    }
  printf("\n\n");

  printf("  Clock STATUS:  \n");
  printf("    A: ");
  switch( status& (SD_STATUS_CLKA_LOSS_OF_SIGNAL | SD_STATUS_CLKA_LOSS_OF_LOCK) )
    {
    case 0:
      printf("Normal");
      break;
    case SD_STATUS_CLKA_LOSS_OF_SIGNAL:
      printf("*** Loss of Signal ***");
      break;
    case SD_STATUS_CLKA_LOSS_OF_LOCK:
      printf("*** Loss of Lock ***");
      break;
    case (SD_STATUS_CLKA_LOSS_OF_SIGNAL|SD_STATUS_CLKA_LOSS_OF_LOCK):
      printf("*** Loss of Signal and Lock ***");
      break;
    }
  printf("\n");
  printf("    B: ");
  switch( status& (SD_STATUS_CLKB_LOSS_OF_SIGNAL | SD_STATUS_CLKB_LOSS_OF_LOCK) )
    {
    case 0:
      printf("Normal");
      break;
    case SD_STATUS_CLKB_LOSS_OF_SIGNAL:
      printf("*** Loss of Signal ***");
      break;
    case SD_STATUS_CLKB_LOSS_OF_LOCK:
      printf("*** Loss of Lock ***");
      break;
    case (SD_STATUS_CLKB_LOSS_OF_SIGNAL|SD_STATUS_CLKB_LOSS_OF_LOCK):
      printf("*** Loss of Signal and Lock ***");
      break;
    }
  printf("\n\n");
	 
  if(rflag)
    {
      printf("  Payload Boards Mask = 0x%04x   Token Passing Boards Mask = 0x%04x\n",
	     payloadPorts,tokenPorts);
      printf("  BusyOut Boards Mask = 0x%04x   TrigOut Boards Mask       = 0x%04x\n",
	     busyoutPorts,trigoutPorts);
      printf("\n");
    }

  if(showVMEslots==1)
    printf("  VME Slots Enabled: \n\t");
  else
    printf("  Payload ports Enabled: \n\t");

  vmeslotmask = tiPayloadPortMask2VMESlotMask(payloadPorts);
  printf("    ");
  for(ii=ibegin; ii<iend; ii++)
    {
      if(showVMEslots==1)
	{
	  if(vmeslotmask & (1<<ii))
	    printf("%2d ",ii);
	  else
	    printf("   ");
	}
      else
	{
	  if(payloadPorts & (1<<(ii-1)))
	    printf("%2d ",ii);
	  else
	    printf("   ");
	}
    }
  printf("\n");

  if(showVMEslots==1)
    printf("  Token VME Slots Enabled: \n\t");
  else
    printf("  Token Payload ports Enabled: \n\t");

  vmeslotmask = tiPayloadPortMask2VMESlotMask(tokenPorts);
  printf("    ");
  for(ii=ibegin; ii<iend; ii++)
    {
      if(showVMEslots==1)
	{
	  if(vmeslotmask & (1<<ii))
	    printf("%2d ",ii);
	  else
	    printf("   ");
	}
      else
	{
	  if(tokenPorts & (1<<(ii-1)))
	    printf("%2d ",ii);
	  else
	    printf("   ");
	}
    }
  printf("\n");

  if(showVMEslots==1)
    printf("  BusyOut VME Slots Enabled: \n\t");
  else
    printf("  BusyOut Payload ports Enabled: \n\t");

  vmeslotmask = tiPayloadPortMask2VMESlotMask(busyoutPorts);
  printf("    ");
  for(ii=ibegin; ii<iend; ii++)
    {
      if(showVMEslots==1)
	{
	  if(vmeslotmask & (1<<ii))
	    printf("%2d ",ii);
	  else
	    printf("   ");
	}
      else
	{
	  if(busyoutPorts & (1<<(ii-1)))
	    printf("%2d ",ii);
	  else
	    printf("   ");
	}
    }
  printf("\n");

  if(showVMEslots==1)
    printf("  Trigout VME Slots Enabled: \n\t");
  else
    printf("  Trigout Payload ports Enabled: \n\t");

  vmeslotmask = tiPayloadPortMask2VMESlotMask(trigoutPorts);
  printf("    ");
  for(ii=ibegin; ii<iend; ii++)
    {
      if(showVMEslots==1)
	{
	  if(vmeslotmask & (1<<ii))
	    printf("%2d ",ii);
	  else
	    printf("   ");
	}
      else
	{
	  if(trigoutPorts & (1<<(ii-1)))
	    printf("%2d ",ii);
	  else
	    printf("   ");
	}
    }
  printf("\n\n");

#ifdef OLDMAP
  if( status2 & SD_STATUS2_POWER_FAULT ) 
    {
      printf("  *** Power Fault Detected ***\n");
    }
  if( status2 & SD_STATUS2_TRIGOUT ) 
    {
      printf("  *** At least one board has asserted TrigOut since last read ***\n");
    }
  if( status2 & SD_STATUS2_BUSYOUT ) 
    {
      printf("  *** At least one board has asserted Busy since last read ***\n");
    }

  if ( status2 & SD_STATUS2_CLKA_LOSS_OF_LOCK )
    {
      printf("  *** Loss of Lock detected for Clock A PLL *** \n");
    }
  if ( status2 & SD_STATUS2_CLKA_LOSS_OF_SIGNAL )
    {
      printf("  *** Loss of Signal detected for Clock A PLL *** \n");
    }
  if ( status2 & SD_STATUS2_CLKB_LOSS_OF_LOCK )
    {
      printf("  *** Loss of Lock detected for Clock B PLL *** \n");
    }
  if ( status2 & SD_STATUS2_CLKB_LOSS_OF_SIGNAL )
    {
      printf("  *** Loss of Signal detected for Clock B PLL *** \n");
    }

  if(status2 & SD_STATUS2_LAST_TOKEN_ADDR_MASK) 
    {
      printf("  Token last received at payload slot %d\n", 
	     (status2 & SD_STATUS2_LAST_TOKEN_ADDR_MASK)>>8 );
    }
#endif

  if(rflag)
    {
      printf("  Busyout State Mask  = 0x%04x   TrigOut State Mask        = 0x%04x\n",
	     busyoutStatus,trigoutStatus);
      printf("\n");
    }

  if(status & SD_STATUS_BUSYOUT)
    {
      printf("  At least one module has asserted BUSY since last read\n");
    }
  if(showVMEslots==1)
    printf("  VME Slots Busy Status High: \n\t");
  else
    printf("  Payload ports Busy Status High: \n\t");

  vmeslotmask = tiPayloadPortMask2VMESlotMask(busyoutStatus);
  printf("    ");
  for(ii=ibegin; ii<iend; ii++)
    {
      if(showVMEslots==1)
	{
	  if(vmeslotmask & (1<<ii))
	    printf("%2d ",ii);
	  else
	    printf("   ");
	}
      else
	{
	  if(busyoutStatus & (1<<(ii-1)))
	    printf("%2d ",ii);
	  else
	    printf("   ");
	}
    }
  printf("\n\n");

  if(status & SD_STATUS_TRIGOUT)
    {
      printf("  At least one module has asserted TrigOut since last read\n");
    }
  if(showVMEslots==1)
    printf("  VME Slots TrigOut Status High : \n\t");
  else
    printf("  Payload ports TrigOut Status High: \n\t");

  vmeslotmask = tiPayloadPortMask2VMESlotMask(trigoutStatus);
  printf("    ");
  for(ii=ibegin; ii<iend; ii++)
    {
      if(showVMEslots==1)
	{
	  if(vmeslotmask & (1<<ii))
	    printf("%2d ",ii);
	  else
	    printf("   ");
	}
      else
	{
	  if(trigoutStatus & (1<<(ii-1)))
	    printf("%2d ",ii);
	  else
	    printf("   ");
	}
    }
  printf("\n\n");
  

  printf("--------------------------------------------------------------------------------\n");
  printf("\n\n");
  return OK;
}

int
sdGetFirmwareVersion(int pflag)
{
  int version=0;
  if(SDp==NULL)
    {
      printf("%s: ERROR: SD not initialized\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  version = vmeRead32(&SDp->version) & 0xFFFF;
  TIUNLOCK;

  if(pflag)
    {
      printf("%s: Firmware Version 0x%x\n",
	     __FUNCTION__,version);
    }

  return version;
}

/*
  sdSetPLLClockFrequency
  - Set the PLL Clock Frequency of A/B/Both
    iclk  : 0 for A
            1 for B
	    2 for Both
    ifreq : 1 for 31.25 MHz
	    2 for 125.00 MHz
	    3 for 250.00 MHz

*/

int
sdSetPLLClockFrequency(int iclk, int ifreq)
{
  if(SDp==NULL)
    {
      printf("%s: ERROR: SD not initialized\n",__FUNCTION__);
      return ERROR;
    }
  if(iclk<0 || iclk>2)
    {
      printf("%s: ERROR: Invalid value of iclk (%d).  Must be 0, 1, or 2.\n",
	     __FUNCTION__,iclk);
      return ERROR;
    }
  if(ifreq<1 || ifreq>3)
    {
      printf("%s: ERROR: Invalid value of ifreq (%d).  Must be 1, 2, or 3.\n",
	     __FUNCTION__,ifreq);
      return ERROR;
    }

  TILOCK;
  if(iclk==0 || iclk==2)
    vmeWrite32(&SDp->system,
	     (vmeRead32(&SDp->system) & ~(SD_SYSTEM_CLKA_FREQUENCY_MASK)) |
	     (ifreq<<2) );

  if(iclk==1 || iclk==2)
    vmeWrite32(&SDp->system,
	     (vmeRead32(&SDp->system) & ~(SD_SYSTEM_CLKB_FREQUENCY_MASK)) |
	     (ifreq<<6) );
  TIUNLOCK;

  return OK;
}

/*
  sdGetPLLClockFrequency
  - Return the PLL clock frequency for the selected iclk 
    iclk  : 0 for A
            1 for B

    returns : 1 for 31.25 MHz
	      2 for 125.00 MHz
	      3 for 250.00 MHz

*/
int
sdGetPLLClockFrequency(int iclk)
{
  int rval;
  if(SDp==NULL)
    {
      printf("%s: ERROR: SD not initialized\n",__FUNCTION__);
      return ERROR;
    }
  if(iclk<0 || iclk>1)
    {
      printf("%s: ERROR: Invalid value of iclk (%d).  Must be 0 or 1.\n",
	     __FUNCTION__,iclk);
      return ERROR;
    }

  TILOCK;
  if(iclk==0)
    rval = (vmeRead32(&SDp->system) & (SD_SYSTEM_CLKA_FREQUENCY_MASK))>>2;
  else
    rval = (vmeRead32(&SDp->system) & (SD_SYSTEM_CLKB_FREQUENCY_MASK))>>6;
  TIUNLOCK;

  return rval;
}

/*
  sdGetClockFrequency
  - Return the detected clock frequency for the selected iclk 
    iclk  : 0 for A
            1 for B

    returns : 0 for undefined
              1 for 31.25 MHz
	      2 for 125.00 MHz
	      3 for 250.00 MHz

*/
int
sdGetClockFrequency(int iclk, int pflag)
{
  int rval;
  if(SDp==NULL)
    {
      printf("%s: ERROR: SD not initialized\n",__FUNCTION__);
      return ERROR;
    }
  if(iclk<0 || iclk>1)
    {
      printf("%s: ERROR: Invalid value of iclk (%d).  Must be 0 or 1.\n",
	     __FUNCTION__,iclk);
      return ERROR;
    }

  TILOCK;
  if(iclk==0)
    rval = (vmeRead32(&SDp->status) & (SD_STATUS_CLKA_DETECTED_MASK))>>8;
  else
    rval = (vmeRead32(&SDp->status) & (SD_STATUS_CLKB_DETECTED_MASK))>>10;
  TIUNLOCK;

  if(pflag)
    {
      printf("%s: Detected Clock Frequency = ",__FUNCTION__);
      switch(rval)
	{
	case 1:
	  printf(" 31.25 MHz\n");
	  break;
	case 2:
	  printf(" 125 MHz\n");
	  break;
	case 3:
	  printf(" 250 MHz\n");
	  break;
	case 0:
	default:
	  printf(" UNKNOWN\n");
	}
    }

  return rval;
}

/*
  sdSetClockMode
  - Select whether the Clock fanned out will be jitter attenuated (PLL on) or
    as received from the TI
    iclk  : 0 for A
            1 for B

    imode : 0 for Bypass mode
            1 for PLL mode
*/
int
sdSetClockMode(int iclk, int imode)
{
  if(SDp==NULL)
    {
      printf("%s: ERROR: SD not initialized\n",__FUNCTION__);
      return ERROR;
    }
  if(iclk<0 || iclk>1)
    {
      printf("%s: ERROR: Invalid value of iclk (%d).  Must be 0 or 1.\n",
	     __FUNCTION__,iclk);
      return ERROR;
    }
  if(imode<0 || imode>1)
    {
      printf("%s: ERROR: Invalid value of imode (%d).  Must be 0 or 1.\n",
	     __FUNCTION__,imode);
      return ERROR;
    }

  TILOCK;
  if(iclk==0)
    vmeWrite32(&SDp->system,
	     (vmeRead32(&SDp->system) & ~(SD_SYSTEM_CLKA_BYPASS_MODE)) |
	     (imode<<0) );
  else
    vmeWrite32(&SDp->system,
	     (vmeRead32(&SDp->system) & ~(SD_SYSTEM_CLKB_BYPASS_MODE)) |
	     (imode<<4) );
  TIUNLOCK;

  return OK;
}

/*
  sdGetClockMode
  - Return whether the Clock fanned out will be jitter attenuated (PLL) or
    as received from the TI(D)
    iclk  : 0 for A
            1 for B

    return : 0 for Bypass mode
             1 for PLL mode    
*/
int
sdGetClockMode(int iclk)
{
  int rval;
  if(SDp==NULL)
    {
      printf("%s: ERROR: SD not initialized\n",__FUNCTION__);
      return ERROR;
    }
  if(iclk<0 || iclk>1)
    {
      printf("%s: ERROR: Invalid value of iclk (%d).  Must be 0 or 1.\n",
	     __FUNCTION__,iclk);
      return ERROR;
    }

  TILOCK;
  if(iclk==0)
    rval = (vmeRead32(&SDp->system) & (SD_SYSTEM_CLKA_BYPASS_MODE));
  else
    rval = (vmeRead32(&SDp->system) & (SD_SYSTEM_CLKB_BYPASS_MODE))>>4;
  TIUNLOCK;

  return rval;
}

/*
  sdReset
  - Reset the SD
*/
int
sdReset()
{
  if(SDp==NULL)
    {
      printf("%s: ERROR: SD not initialized\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  vmeWrite32(&SDp->system,
	     (vmeRead32(&SDp->system) & ~(SD_SYSTEM_TEST_RESET)) |
	     SD_SYSTEM_TEST_RESET );
  vmeWrite32(&SDp->system,
	     (vmeRead32(&SDp->system) & ~(SD_SYSTEM_TEST_RESET)));
  TIUNLOCK;

  return OK;
}

/*
  sdSetActivePayloadPorts
  - Routine for user to define the Payload Ports that participate in 
  Trigger Out, Busy Out, Token, and Status communication.

*/
int
sdSetActivePayloadPorts(unsigned int imask)
{
  if(SDp==NULL)
    {
      printf("%s: ERROR: SD not initialized\n",__FUNCTION__);
      return ERROR;
    }

  if(imask<0 || imask>0xffff)
    {
      printf("%s: ERROR: Invalid imask 0x%x\n",
	     __FUNCTION__,imask);
      return ERROR;
    }

  TILOCK;
  vmeWrite32(&SDp->payloadPorts, imask);
  vmeWrite32(&SDp->tokenPorts, imask);
  vmeWrite32(&SDp->busyoutPorts, imask);
  vmeWrite32(&SDp->trigoutPorts, imask);
  TIUNLOCK;

  return OK;
}

/*
  sdSetActiveVmeSlots
  - Routine for user to define the Vme Slots that participate in 
  Trigger Out, Busy Out, Token, and Status communication.
  - Mask Convention: 
    bit  0: Vme Slot 0 (non-existant)
    bit  1: Vme Slot 1 (controller slot)
    bit  2: Vme Slot 2 (not used by CTP)
    bit  3: Vme Slot 3 (First slot on the LHS of crate that is used by CTP)
    ..
    bit 20: Vme Slot 20 (Last slot that is used by the CTP)
    bit 21: Vme Slot 21 (Slot for the TID)

  RETURNS: OK if successful, otherwise ERROR.

*/
int
sdSetActiveVmeSlots(unsigned int vmemask)
{
  unsigned int payloadmask=0;
  unsigned int slot=0, payloadport=0, ibit=0;

  if(SDp==NULL)
    {
      printf("%s: ERROR: SD not initialized\n",__FUNCTION__);
      return ERROR;
    }
  
  /* Check the input mask */
  if( vmemask & 0xFFE00007 )
    {
      printf("%s: ERROR: Invalid vmemask (0x%08x)\n",
	     __FUNCTION__,vmemask);
      return ERROR;
    }

  /* Convert the vmemask to the payload port mask */
  for (ibit=0; ibit<32; ibit++)
    {
      if(vmemask & (1<<ibit))
	{
	  slot = ibit;
	  payloadport  = tiVMESlot2PayloadPort(slot);
	  payloadmask |= (1<<(payloadport-1));
	}
    }

  sdSetActivePayloadPorts(payloadmask);

  return OK;


}

/*
  sdGetActivePayloadPorts
  - Routine to return the currently defined Payload Ports that participate in 
    Trigger Out, Busy Out, Token, and Status communication.

*/
int
sdGetActivePayloadPorts()
{
  int rval;
  unsigned int payloadPorts, tokenPorts, busyoutPorts, trigoutPorts;
  if(SDp==NULL)
    {
      printf("%s: ERROR: SD not initialized\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  payloadPorts = vmeRead32(&SDp->payloadPorts);
  tokenPorts   = vmeRead32(&SDp->tokenPorts);
  busyoutPorts = vmeRead32(&SDp->busyoutPorts);
  trigoutPorts = vmeRead32(&SDp->trigoutPorts);
  TIUNLOCK;

  rval = payloadPorts;

  /* Simple check for consistency, warn if there's not */
  if((payloadPorts != tokenPorts) ||
     (payloadPorts != busyoutPorts) ||
     (payloadPorts != trigoutPorts) )
    {
      printf("%s: WARNING: Inconsistent payload slot masks..",__FUNCTION__);
      printf("    payloadPorts = 0x%08x\n",payloadPorts);
      printf("    tokenPorts   = 0x%08x\n",tokenPorts);
      printf("    busyoutPorts = 0x%08x\n",busyoutPorts);
      printf("    trigoutPorts = 0x%08x\n",trigoutPorts);
    }
  
  return rval;
}

/*
  sdGetBusyoutCounter
  - Return the value of the Busyout Counter for a specified payload board
    Value of the counter is reset after read

*/

int
sdGetBusyoutCounter(int ipayload)
{
  int rval;
  if(SDp==NULL)
    {
      printf("%s: ERROR: SD not initialized\n",__FUNCTION__);
      return ERROR;
    }

  if(ipayload<1 || ipayload>16)
    {
      printf("%s: ERROR: Invalid ipayload = %d.  Must be 1-16\n",
	     __FUNCTION__,ipayload);
      return ERROR;
    }

  TILOCK;
  rval = vmeRead32(&SDp->busyoutCounter[ipayload-1]);
  TIUNLOCK;

  return rval;

}

int
sdPrintBusyoutCounters()
{
  unsigned int counter=0;
  int islot;
  if(SDp==NULL)
    {
      printf("%s: ERROR: SD not initialized\n",__FUNCTION__);
      return ERROR;
    }
  
  printf("-----------------\n");
  printf("Slot:  Busy Count\n");
  for(islot=3; islot<21; islot++)
    {
      if((islot==11) || (islot==12)) /* Skip the Switch Slots */
	continue;
      
      counter = sdGetBusyoutCounter(tiVMESlot2PayloadPort(islot));
      printf("  %2d:  %d\n",islot,counter);
    }
  printf("-----------------\n");
  printf("\n");

  return OK;
}

/*
  sdGetBusyoutStatus
  - Return the mask value of payload ports that are currently BUSY

*/

int
sdGetBusyoutStatus(int pflag)
{
  int rval;
  if(SDp==NULL)
    {
      printf("%s: ERROR: SD not initialized\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  rval = vmeRead32(&SDp->trigoutStatus) & 0xFFFF;
  TIUNLOCK;

  if(pflag)
    {
      printf("%s: Busyout Status = 0x%04x\n",
	     __FUNCTION__,rval);
    }

  return rval;
}

/*
  sdGetTrigoutCounter
  - Return the value of the Trigout Counter for a specified payload board
    Value of the counter is reset after read

*/

int
sdGetTrigoutCounter(int ipayload)
{
  int rval;
  if(SDp==NULL)
    {
      printf("%s: ERROR: SD not initialized\n",__FUNCTION__);
      return ERROR;
    }

  if(ipayload<1 || ipayload>16)
    {
      printf("%s: ERROR: Invalid ipayload = %d.  Must be 1-16\n",
	     __FUNCTION__,ipayload);
      return ERROR;
    }

  TILOCK;
  rval = vmeRead32(&SDp->trigoutCounter[ipayload-1]);
  TIUNLOCK;

  return rval;

}

int
sdPrintTrigoutCounters()
{
  unsigned int counter=0;
  int islot;
  if(SDp==NULL)
    {
      printf("%s: ERROR: SD not initialized\n",__FUNCTION__);
      return ERROR;
    }
  
  printf("--------------------\n");
  printf("Slot:  Trigout Count\n");
  for(islot=3; islot<21; islot++)
    {
      if((islot==11) || (islot==12)) /* Skip the Switch Slots */
	continue;
      
      counter = sdGetTrigoutCounter(tiVMESlot2PayloadPort(islot));
      printf("  %2d:  %d\n",islot,counter);
    }
  printf("--------------------\n");
  printf("\n");

  return OK;
}



/*************************************************************
 *  SD FIRMWARE UPDATING ROUTINES
 *  Linux only supported
 ************************************************************/
#ifndef VXWORKSPPC
static int
sdFirmwareWaitCmdDone(int wait)
{
  int i;
  unsigned int data_out;
		
  if(SDp==NULL)
    {
      printf("%s: ERROR: SD not initialized\n",__FUNCTION__);
      return ERROR;
    }

  for(i = 0; i < wait*75; i++)
    {
      if((i%100)==0)
	{
	  printf(".");
	  fflush(stdout);
	}

      TILOCK;
      data_out = vmeRead32(&SDp->memCheckStatus);
      TIUNLOCK;

      fflush(stdout);
      if (!(data_out & 0x100)) 
	{
	  return data_out & 0xFF;
	}
    }

  printf("%s: ERROR: Timeout\n",__FUNCTION__);

  return 0;

}

int
sdFirmwareFlushFifo()
{
  int i;
  unsigned int data_out;

  if(SDp==NULL)
    {
      printf("%s: ERROR: SD not initialized\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  for(i = 0; i < 100; i++)
    {
      data_out = vmeRead32(&SDp->memReadCtrl);

      if(data_out & 0x200)
	break;
    }
  TIUNLOCK;

  if(i == 100)
    {
      printf("%s: ERROR: config read init buffer error\n",__FUNCTION__);
      return ERROR;
    }

  else
    printf("%s: INFO: i = %d   data_out = 0x%0x\n",__FUNCTION__,i, data_out);

  return OK;
}

int
sdFirmwareLoadFile(char *filename)
{
  FILE *progFile;
/* #define DEBUGFILE */
#ifdef DEBUGFILE
  int ibyte=0;
#endif

  if(filename==NULL)
    {
      printf("%s: Error: Invalid filename\n",__FUNCTION__);
      return ERROR;
    }

  /* Open the file containing the firmware */
  progFile=fopen(filename,"r");
  if(progFile==NULL)
    {
      printf("%s: ERROR opening file (%s) for reading\n",
	     __FUNCTION__,filename);
      return ERROR;
    }

  /* Allocate memory to store in locally */
  progFirmware = (unsigned char *)malloc(SD_MAX_FIRMWARE_SIZE*sizeof(unsigned char));
  if(progFirmware==NULL)
    {
      printf("%s: ERROR: Unable to allocate memory for firmware\n",__FUNCTION__);
      fclose(progFile);
      return ERROR;
    }
  
  /* Initialize this local memory with 0xff's */
  memset(progFirmware, 0xff, SD_MAX_FIRMWARE_SIZE);
  
  /* Read the file into memory */
  progFirmwareSize = fread(progFirmware, 1, SD_MAX_FIRMWARE_SIZE, progFile);
  printf("%s: Firmware Size = %d (0x%x)\n",
	 __FUNCTION__, progFirmwareSize, progFirmwareSize);

/*   progFirmwareSize=0x20000; */

  fclose(progFile);

#ifdef DEBUGFILE
  for(ibyte = 0; ibyte<progFirmwareSize; ibyte++)
    {
      printf("%02x ",progFirmware[ibyte]);
      if((ibyte+1)%64==0) printf("\n");
    }
  printf("\n");
#endif

  return OK;
}

void
sdFirmwareFreeMemory()
{
  if(progFirmware!=NULL)
    {
      free(progFirmware);
    }
}

int
sdFirmwareVerifyPage(unsigned int mem_addr)
{
  unsigned int data;
  unsigned int ibyte;
  int n_err=0;

  if(SDp==NULL)
    {
      printf("%s: ERROR: SD not initialized\n",__FUNCTION__);
      return ERROR;
    }

/*   printf("%s: Verifying loaded firmware with current firmware\n", */
/* 	 __FUNCTION__); */

  /* Loop over each byte in the firmware */
  for(ibyte = mem_addr; ibyte<mem_addr+256; ibyte++)
    {
      if((ibyte%0x10000) == 0) printf("Verifying firmware to memory address 0x%06x\n",ibyte);
      /* Write to set the memory address we're accessing */
      vmeWrite32(&SDp->memAddrLSB, (ibyte & 0xFFFF) );
      vmeWrite32(&SDp->memAddrMSB, (ibyte & 0xFF0000)>>16 );

      vmeWrite32(&SDp->memReadCtrl, 0xB00);

      data = vmeRead32(&SDp->memReadCtrl); 
      if(data < 0)
	{
	  printf("%s: ERROR: page program timeout error\n", __FUNCTION__);
	  TIUNLOCK;
	  return ERROR;
	}
      else
	{
	  if(progFirmware[ibyte] != (data & 0xFF))
	    {
	      n_err++;
	      if(n_err<400)
		{
		  printf("0x%06x (%8d): 0x%02x != 0x%02x    ***** \n",
			 ibyte, ibyte, progFirmware[ibyte], (data & 0xFF));
		}
	      
	    }
	}

    }

  if(n_err)
    {
      printf("%s: Total errors: %d\n",__FUNCTION__,n_err);
      return ERROR;
    }

  return OK;


}

int
sdFirmwareVerifyPageZero(unsigned int mem_addr)
{
  unsigned int data;
  unsigned int ibyte;
  int n_err=0;

  if(SDp==NULL)
    {
      printf("%s: ERROR: SD not initialized\n",__FUNCTION__);
      return ERROR;
    }

/*   printf("%s: Verifying loaded firmware with current firmware\n", */
/* 	 __FUNCTION__); */

  /* Loop over each byte in the firmware */
/* #define DEBUGERASE */
#ifdef DEBUGERASE
  printf("Verifying erase to memory address 0x%06x\n",mem_addr);
#endif
  for(ibyte = mem_addr; ibyte<mem_addr+256; ibyte++)
    {
      /* Write to set the memory address we're accessing */
      vmeWrite32(&SDp->memAddrLSB, (ibyte & 0xFFFF) );
      vmeWrite32(&SDp->memAddrMSB, (ibyte & 0xFF0000)>>16 );

      vmeWrite32(&SDp->memReadCtrl, 0xB00);

      data = vmeRead32(&SDp->memReadCtrl); 
      if(data < 0)
	{
	  printf("%s: ERROR: page program timeout error\n", __FUNCTION__);
	  TIUNLOCK;
	  return ERROR;
	}
      else
	{
	  if(0xff != (data & 0xFF))
	    {
	      n_err++;
#ifdef DEBUGERASE
	      if(n_err<400)
		{
		  printf("0x%06x (%8d): 0x%02x != 0x%02x    ***** \n",
			 ibyte, ibyte, 0xff, (data & 0xFF));
		}
#endif
	      
	    }
	}

    }

  if(n_err)
    {
      printf("%s: Total errors: %d\n",__FUNCTION__,n_err);
      return ERROR;
    }

  return OK;


}

void
sdFirmwareWritePage(unsigned int mem_addr)
{
  int ibyte=0;
  unsigned int prog=0;
  unsigned int memCommand=0, mem_write=0;

  vmeWrite32(&SDp->memAddrLSB, (mem_addr & 0xFFFF) );
  vmeWrite32(&SDp->memAddrMSB, (mem_addr & 0xFF0000)>>16 );

  memCommand=0x0600;
  for(ibyte = mem_addr; ibyte < mem_addr + 256; ibyte++)
    {

      prog = (progFirmware[ibyte]) & 0xFF;

      if(ibyte>=progFirmwareSize)
	mem_write = (memCommand | 0xFF);
      else
	mem_write = (memCommand | prog);

      vmeWrite32(&SDp->memWriteCtrl, mem_write );

      if(ibyte==(mem_addr+255))
	{
	  memCommand=0x300;
	  mem_write = (memCommand | prog);
	      
	  vmeWrite32(&SDp->memWriteCtrl, mem_write );
	}

    }
  vmeWrite32(&SDp->memWriteCtrl, 0x0300 | prog);
  
#ifdef VXWORKSPPC
  taskDelay(1);
#else
  usleep(7000);
#endif
}

int
sdFirmwareWriteToMemory()
{
  unsigned int mem_addr=0;
  int page_count=0;

  if(SDp==NULL)
    {
      printf("%s: ERROR: SD not initialized\n",__FUNCTION__);
      return ERROR;
    }

  if((progFirmware==NULL) || (progFirmwareSize==0))
    {
      printf("%s: Error: Firmware file not loaded into memory\n",
	     __FUNCTION__);
      return ERROR;
    }

  /* Loop over each byte in the firmware */
  for(mem_addr=0; mem_addr<progFirmwareSize; mem_addr+=256)
    {

      if( (mem_addr % 0x10000) == 0) /* Erase current sector */
	{
	  TILOCK;
	  vmeWrite32(&SDp->memAddrLSB, (mem_addr & 0xFFFF) );
	  vmeWrite32(&SDp->memAddrMSB, (mem_addr & 0xFF0000)>>16 );

	  vmeWrite32(&SDp->memWriteCtrl, (0x1200) );

	  sleep(3);
	  TIUNLOCK;
	  if(sdFirmwareWaitCmdDone(3300)<0)
	    {
	      printf("%s: ERROR: sector erase timeout error\n",__FUNCTION__);
	      return ERROR;
	    }

	}

      /* Write to set the memory address we're accessing */
      if((mem_addr%0x10000) == 0) printf("Writing firmware to memory address 0x%06x\n",mem_addr);
      TILOCK;

      if(sdFirmwareVerifyPageZero(mem_addr)==ERROR)
	{
	  TIUNLOCK;
	  printf("%s: Too many errors in current page (%d)\n",__FUNCTION__,page_count);
	  return ERROR;
	}

      sdFirmwareWritePage(mem_addr);

      if(sdFirmwareVerifyPage(mem_addr)==ERROR)
	{
	    TIUNLOCK;
	    printf("%s: Too many errors in current page (%d)\n",__FUNCTION__,page_count);
	    return ERROR;
	}

      page_count++;
      
#ifdef VXWORKSPPC
      taskDelay(1);
#else
      usleep(5000);
#endif
      TIUNLOCK;

    }
  TIUNLOCK;

  printf("%s: pages written = %d\n",__FUNCTION__,page_count);
  return OK;
}


int
sdFirmwareVerifyMemory()
{
  unsigned int mem_addr=0, data;
  int n_err=0;

  if(SDp==NULL)
    {
      printf("%s: ERROR: SD not initialized\n",__FUNCTION__);
      return ERROR;
    }

  printf("%s: Verifying loaded firmware with current firmware\n",
	 __FUNCTION__);

  /* Loop over each byte in the firmware */
  TILOCK;
  for(mem_addr=0; mem_addr<progFirmwareSize; mem_addr++)
    {
      if((mem_addr%0x10000) == 0) printf("Verifying firmware to memory address 0x%06x\n",mem_addr);
      /* Write to set the memory address we're accessing */
      vmeWrite32(&SDp->memAddrLSB, (mem_addr & 0xFFFF) );
      vmeWrite32(&SDp->memAddrMSB, (mem_addr & 0xFF0000)>>16 );

      vmeWrite32(&SDp->memReadCtrl, 0xB00);

      data = vmeRead32(&SDp->memReadCtrl); 
      if(data < 0)
	{
	  printf("%s: ERROR: page program timeout error\n", __FUNCTION__);
	  TIUNLOCK;
	  return ERROR;
	}
      else
	{
	  if(progFirmware[mem_addr] != (data & 0xFF))
	    {
	      n_err++;
	      if(n_err<400)
		{
		  printf("0x%06x (%8d): 0x%02x != 0x%02x    ***** \n",
			 mem_addr, mem_addr, progFirmware[mem_addr], (data & 0xFF));
		}
	      
	    }
	}

    }
  TIUNLOCK;

  printf("%s: Total errors: %d\n",__FUNCTION__,n_err);

  if(n_err)
    return ERROR;

  return OK;

}

int 
sdFirmwareReadStatus()
{
  unsigned int status_out;
  int i;
	
  TILOCK;
  for(i = 0; i < 3; i++)
    {
      vmeWrite32(&SDp->memReadCtrl, 0x0400);
/*       write_i2c (0x0400, 0x48); */
      status_out = vmeRead32(&SDp->memCheckStatus);
/*       status_out = read_i2c(0x49); */
/*       if ((i % 2) == 0) */
/* 	{	printf("{%04X}", status_out);} */
		
      if ((status_out & 0x4))
	{
/* 	  printf("%s: INFO: read status complete\n",__FUNCTION__); */
/* 	  printf("{%04X}", status_out); */
/* 	  printf("\n"); */
	  TIUNLOCK;
	  return status_out & 0xFF;
	}
      taskDelay(1);
    }
  TIUNLOCK;
  printf("%s: ERROR: Timeout\n",__FUNCTION__);
  return -1;
}

void
sdFirmwareWriteSpecs(unsigned int addr, unsigned int serial_number,
		     unsigned int hall_board_version, unsigned int firmware_version)
{
  int i;

  TILOCK;
  vmeWrite32(&SDp->memAddrLSB, (addr & 0xFFFF) );
  vmeWrite32(&SDp->memAddrMSB, (addr & 0xFF0000)>>16 );

  vmeWrite32(&SDp->memWriteCtrl, (0x2200) );

  sleep(3);
  TIUNLOCK;
  if(sdFirmwareWaitCmdDone(3300)<0)
    {
      printf("%s: ERROR: sector erase timeout error\n",__FUNCTION__);
      return;
    }

  TILOCK;
  vmeWrite32(&SDp->memAddrLSB, (addr & 0xFFFF) );
  vmeWrite32(&SDp->memAddrMSB, (addr & 0xFF0000)>>16 );

  vmeWrite32(&SDp->memWriteCtrl, (0x1200) );

  sleep(3);
  TIUNLOCK;
  if(sdFirmwareWaitCmdDone(3300)<0)
    {
      printf("%s: ERROR: sector erase timeout error\n",__FUNCTION__);
      return;
    }

  TILOCK;

  sdFirmwareVerifyPageZero(addr);

  vmeWrite32(&SDp->memAddrLSB, (addr & 0xFFFF) );
  vmeWrite32(&SDp->memAddrMSB, (addr & 0xFF0000)>>16 );

  /* Write the board specific stuff here */
  vmeWrite32(&SDp->memWriteCtrl, (0x600) | (serial_number&0xFF) ); 
  vmeWrite32(&SDp->memWriteCtrl, (0x600) | (hall_board_version&0xFF) );
  vmeWrite32(&SDp->memWriteCtrl, (0x600) | (firmware_version&0xFF) );

  for(i = 0; i < 253; i++)
    {
      vmeWrite32(&SDp->memWriteCtrl, 0x6EE);
		
      if (i == 252)
	{
	  vmeWrite32(&SDp->memWriteCtrl, 0x3EE);
	  break;
	}
		
    }

  vmeWrite32(&SDp->memWriteCtrl, (0x2220) );

  sleep(2);
  TIUNLOCK;
  if(sdFirmwareWaitCmdDone(3300)<0)
    {
      printf("%s: ERROR: sector protect timeout error\n",__FUNCTION__);
      return;
    }

  printf("%s: INFO: Complete\n",__FUNCTION__);

}
#endif /* !VXWORKSPPC */

int
sdFirmwareReadAddr(unsigned int addr)
{
  unsigned int data_out;
	
  TILOCK;
  vmeWrite32(&SDp->memAddrLSB, (addr & 0xFFFF) );
  vmeWrite32(&SDp->memAddrMSB, (addr & 0xFF0000)>>16 );
  
  vmeWrite32(&SDp->memReadCtrl, 0xB00);

  taskDelay(1);
	
  data_out = vmeRead32(&SDp->memReadCtrl);
  TIUNLOCK;

  return data_out & 0xFF;

}

void
sdFirmwarePrintSpecs()
{
  printf("%s:\n",__FUNCTION__);
  printf("\tSerial Number            = %4d\n", sdFirmwareReadAddr(0x7F0000));
  printf("\tAssigned Hall & Version  = 0x%02X\n", sdFirmwareReadAddr(0x7F0001));
  printf("\tInitial Firmware Version = 0x%02X\n", sdFirmwareReadAddr(0x7F0002));
}

unsigned int
sdGetSerialNumber(char *rSN)
{
  unsigned int sn;
  char retSN[10];
  if(SDp==NULL)
    {
      printf("%s: ERROR: SD not initialized\n",__FUNCTION__);
      return ERROR;
    }

  sn = sdFirmwareReadAddr(0x7F0000);

  if(rSN!=NULL)
    {
      sprintf(retSN,"SD-%03d",sn&0xffff);
      strcpy((char *)rSN,retSN);
    }


  printf("%s: SD Serial Number is %s (0x%08x)\n", 
	 __FUNCTION__,retSN,sn);

  return sn;

}

int
sdTestGetBusyout()
{
  int rval;
  if(SDp==NULL)
    {
      printf("%s: ERROR: SD not initialized\n",__FUNCTION__);
      return ERROR;
    }
  if(!sdTestMode)
    {
      printf("%s: ERROR: SD Test Mode Jumper not installed\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  rval = vmeRead32(&SDp->busyoutTest);
  TIUNLOCK;

  return rval;
}

int
sdTestGetSdLink()
{
  int rval;
  if(SDp==NULL)
    {
      printf("%s: ERROR: SD not initialized\n",__FUNCTION__);
      return ERROR;
    }
  if(!sdTestMode)
    {
      printf("%s: ERROR: SD Test Mode Jumper not installed\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  rval = vmeRead32(&SDp->sdLinkTest);
  TIUNLOCK;

  return rval;
}

int
sdTestGetTokenIn()
{
  int rval;
  if(SDp==NULL)
    {
      printf("%s: ERROR: SD not initialized\n",__FUNCTION__);
      return ERROR;
    }
  if(!sdTestMode)
    {
      printf("%s: ERROR: SD Test Mode Jumper not installed\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  rval = vmeRead32(&SDp->tokenInTest);
  TIUNLOCK;

  return rval;
}

int
sdTestGetTrigOut()
{
  int rval;
  if(SDp==NULL)
    {
      printf("%s: ERROR: SD not initialized\n",__FUNCTION__);
      return ERROR;
    }
  if(!sdTestMode)
    {
      printf("%s: ERROR: SD Test Mode Jumper not installed\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  rval = vmeRead32(&SDp->trigOutTest);
  TIUNLOCK;

  return rval;
}

void
sdTestSetTokenOutMask(int mask)
{
  if(SDp==NULL)
    {
      printf("%s: ERROR: SD not initialized\n",__FUNCTION__);
      return;
    }
  if(mask>0xffff)
    {
      printf("%s: ERROR: Mask out of range (0x%x)\n",__FUNCTION__,mask);
      return;
    }
  if(!sdTestMode)
    {
      printf("%s: ERROR: SD Test Mode Jumper not installed\n",__FUNCTION__);
      return;
    }

  TILOCK;
  vmeWrite32(&SDp->tokenOutTest,mask);
  TIUNLOCK;

}

void
sdTestSetStatBitBMask(int mask)
{
  if(SDp==NULL)
    {
      printf("%s: ERROR: SD not initialized\n",__FUNCTION__);
      return;
    }
  if(mask>0xffff)
    {
      printf("%s: ERROR: Mask out of range (0x%x)\n",__FUNCTION__,mask);
      return;
    }
  if(!sdTestMode)
    {
      printf("%s: ERROR: SD Test Mode Jumper not installed\n",__FUNCTION__);
      return;
    }

  TILOCK;
  vmeWrite32(&SDp->statBitBTest,mask);
  TIUNLOCK;

}

void
sdTestSetClkAPLL(int mode)
{
  if(SDp==NULL)
    {
      printf("%s: ERROR: SD not initialized\n",__FUNCTION__);
      return;
    }
  if(!sdTestMode)
    {
      printf("%s: ERROR: SD Test Mode Jumper not installed\n",__FUNCTION__);
      return;
    }

  if(mode>=1) mode=1;
  else mode=0;

  TILOCK;
  vmeWrite32(&SDp->csrTest,mode | 
	     SD_CSRTEST_CLKA_FREQ |
	     SD_CSRTEST_CLKB_FREQ |
	     SD_CSRTEST_TEST_RESET);
  TIUNLOCK;

}

int
sdTestGetClockAStatus()
{
  int rval;
  if(SDp==NULL)
    {
      printf("%s: ERROR: SD not initialized\n",__FUNCTION__);
      return ERROR;
    }
  if(!sdTestMode)
    {
      printf("%s: ERROR: SD Test Mode Jumper not installed\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  rval = (vmeRead32(&SDp->csrTest) & SD_CSRTEST_CLKA_TEST_STATUS)>>1;
  TIUNLOCK;

  return rval;
}

int
sdTestGetClockAFreq()
{
  int rval;
  if(SDp==NULL)
    {
      printf("%s: ERROR: SD not initialized\n",__FUNCTION__);
      return ERROR;
    }
  if(!sdTestMode)
    {
      printf("%s: ERROR: SD Test Mode Jumper not installed\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  rval = (vmeRead32(&SDp->csrTest) & SD_CSRTEST_CLKA_FREQ)>>2;
  TIUNLOCK;

  return rval;
}

void
sdTestSetClkBPLL(int mode)
{
  if(SDp==NULL)
    {
      printf("%s: ERROR: SD not initialized\n",__FUNCTION__);
      return;
    }
  if(!sdTestMode)
    {
      printf("%s: ERROR: SD Test Mode Jumper not installed\n",__FUNCTION__);
      return;
    }

  if(mode>=1) mode=1;
  else mode=0;

  TILOCK;
  vmeWrite32(&SDp->csrTest,(mode<<4) | SD_CSRTEST_TEST_RESET);
  TIUNLOCK;

}

int
sdTestGetClockBStatus()
{
  int rval;
  if(SDp==NULL)
    {
      printf("%s: ERROR: SD not initialized\n",__FUNCTION__);
      return ERROR;
    }
  if(!sdTestMode)
    {
      printf("%s: ERROR: SD Test Mode Jumper not installed\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  rval = (vmeRead32(&SDp->csrTest) & SD_CSRTEST_CLKB_TEST_STATUS)>>5;
  TIUNLOCK;

  return rval;
}

int
sdTestGetClockBFreq()
{
  int rval;
  if(SDp==NULL)
    {
      printf("%s: ERROR: SD not initialized\n",__FUNCTION__);
      return ERROR;
    }
  if(!sdTestMode)
    {
      printf("%s: ERROR: SD Test Mode Jumper not installed\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  rval = (vmeRead32(&SDp->csrTest) & SD_CSRTEST_CLKB_FREQ)>>6;
  TIUNLOCK;

  return rval;
}

void
sdTestSetTIBusyOut(int level)
{
  if(SDp==NULL)
    {
      printf("%s: ERROR: SD not initialized\n",__FUNCTION__);
      return;
    }
  if(!sdTestMode)
    {
      printf("%s: ERROR: SD Test Mode Jumper not installed\n",__FUNCTION__);
      return;
    }

  if(level>=1) level=1;
  else level=0;

  TILOCK;
  if(level)
    vmeWrite32(&SDp->csrTest,SD_CSRTEST_TI_BUSYOUT | SD_CSRTEST_TEST_RESET);
  else
    vmeWrite32(&SDp->csrTest,0 | SD_CSRTEST_TEST_RESET);
  TIUNLOCK;
}

int
sdTestGetTITokenIn()
{
  int rval;
  if(SDp==NULL)
    {
      printf("%s: ERROR: SD not initialized\n",__FUNCTION__);
      return ERROR;
    }
  if(!sdTestMode)
    {
      printf("%s: ERROR: SD Test Mode Jumper not installed\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  rval = (vmeRead32(&SDp->csrTest) & SD_CSRTEST_TI_TOKENIN)>>9;
  printf("%s: csrTest = 0x%08x\n", __FUNCTION__, vmeRead32(&SDp->csrTest));
  TIUNLOCK;

  return rval;

}

void
sdTestSetTIGTPLink(int level)
{
  if(SDp==NULL)
    {
      printf("%s: ERROR: SD not initialized\n",__FUNCTION__);
      return;
    }
  if(!sdTestMode)
    {
      printf("%s: ERROR: SD Test Mode Jumper not installed\n",__FUNCTION__);
      return;
    }

  if(level>=1) level=1;
  else level=0;

  TILOCK;
  if(level)
    vmeWrite32(&SDp->csrTest,SD_CSRTEST_TI_GTPLINK | SD_CSRTEST_TEST_RESET);
  else
    vmeWrite32(&SDp->csrTest,0 | SD_CSRTEST_TEST_RESET);
  TIUNLOCK;
}

unsigned int
sdTestGetClkACounter()
{
  unsigned int rval=0;
  if(SDp==NULL)
    {
      printf("%s: ERROR: SD not initialized\n",__FUNCTION__);
      return 0;
    }
  if(!sdTestMode)
    {
      printf("%s: ERROR: SD Test Mode Jumper not installed\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  rval = vmeRead32(&SDp->clkACounterTest);
  TIUNLOCK;

  return rval;
}

unsigned int
sdTestGetClkBCounter()
{
  unsigned int rval=0;
  if(SDp==NULL)
    {
      printf("%s: ERROR: SD not initialized\n",__FUNCTION__);
      return 0;
    }
  if(!sdTestMode)
    {
      printf("%s: ERROR: SD Test Mode Jumper not installed\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  rval = vmeRead32(&SDp->clkBCounterTest);
  TIUNLOCK;

  return rval;
}

unsigned int 
sdTestGetSWALoopback()
{
  unsigned int rval=0;
  if(SDp==NULL)
    {
      printf("%s: ERROR: SD not initialized\n",__FUNCTION__);
      return 0;
    }
  if(!sdTestMode)
    {
      printf("%s: ERROR: SD Test Mode Jumper not installed\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  rval = (vmeRead32(&SDp->csrTest) & SD_CSRTEST_SWA_LOOPBACK_MASK);
  TIUNLOCK;

  return rval;
}

