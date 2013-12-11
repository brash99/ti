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
 *     Status and Control library for the JLAB Crate Trigger Processor
 *     (CTP) module using an i2c interface from the JLAB Trigger
 *     Interface (TI) module.
 *
 *   This file is "included" in the tiLib.c
 *
 *----------------------------------------------------------------------------*/

#include <ctype.h>

/* This is the CTP base relative to the TI base VME address */
#define CTPBASE 0x30000 

/* Global Variables */
volatile struct CTPStruct  *CTPp=NULL;    /* pointer to CTP memory map */

/* FPGA Channel number to Payload Port Map */
#define NUM_CTP_FPGA 3
#define NUM_FADC_CHANNELS 6 /* 5 for VLX50, 6 for VLX110 */
unsigned int ctpPayloadPort[NUM_CTP_FPGA][NUM_FADC_CHANNELS] =
  {
    /* U1 */
    {  7,  9, 11, 13, 15,  0},  
    /* U3 */
    {  8, 10, 12, 14, 16,  0},
    /* U24 */
    {  3,  1,  5,  2,  4,  6}
  };
enum ifpga {U1, U3, U24, NFPGA};

/* Firmware updating variables/functions */
#define MAX_FIRMWARE_SIZE 8000000
static unsigned char fw_data[MAX_FIRMWARE_SIZE];
static unsigned int fw_data_size=0;
static unsigned int my_fw_data_size=0;
static int my_erase_n_sectors=0;
static int fw_file_loaded=0;


/* Static function prototypes */
static int ctpSROMRead(int addr, int ntries);
static int hex2num(char c);
int ctpReadFirmwareFile(char *fw_filename);
static int ctpCROMErase(int fpga);
static int ctpWaitForCommandDone(int ntries);
static int ctpWriteFirmwareToSRAM();
static int ctpVerifySRAMData();
static int ctpProgramCROMfromSRAM(int ifpga);
static int ctpWriteCROMToSRAM(int ifpga);
static int ctpRebootAllFPGA();
/*
  ctpInit
  - Initialize the Crate Trigger Processor

*/
int
ctpInit()
{
  unsigned long tiBase=0, ctpBase=0;
  if(TIp==NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  /* Do something here to verify that we've got good i2c to the CTP */
  /* Verify that the ctp registers are in the correct space for the TID I2C */
  tiBase = (unsigned long)TIp;
  ctpBase = (unsigned long)&(TIp->SWA[0]);
  
  if(ctpBase-tiBase != CTPBASE)
    {
      printf("%s: ERROR: CTP memory structure not in correct VME Space!\n",
	     __FUNCTION__);
      printf("   current base = 0x%lx   expected base = 0x%lx\n",
	     ctpBase-tiBase, (unsigned long)CTPBASE);
      return ERROR;
    }

  CTPp = (struct CTPStruct *)(&TIp->SWA[0]);

  printf("%s: CTP initialized at Local Base address 0x%lx\n",
	 __FUNCTION__,(unsigned long) CTPp);

  /* Reset the fiber links... this needs to be done after the TI clock switchover, 
     So do it here */
/*   ctpPayloadReset(); */
  ctpFiberReset();

  return OK;

}

static
int
ppArray2vsArray(int *inarray)
{
  int iport=0, vmeslot=0;
  int tempArray[22];
  if(inarray==NULL)
    {
      printf("%s: ERROR: Invalid Input Array\n",__FUNCTION__);
      return ERROR;
    }

  /* Fill in the blank slots with zeros */
  tempArray[0] = tempArray[1] = tempArray[2] = tempArray[11] = tempArray[12] = 0;

  for(iport=1; iport<=16; iport++)
    {
      vmeslot = tiPayloadPort2VMESlot(iport);
      tempArray[vmeslot] = inarray[iport];
    }

  for(vmeslot=0; vmeslot<22; vmeslot++)
    {
      inarray[vmeslot] = tempArray[vmeslot];
    }

  return OK;
}
/*
  ctpStatus
  - Display the status of the CTP registers 
*/
int
ctpStatus(int pflag)
{
  struct CTP_FPGA_U1_Struct fpga[NFPGA]; // Array to handle the "common" registers
  char sfpga[NFPGA][4] = {"U1", "U3", "U24"};
  int ichan, ifpga, payloadport, ii, ibegin=0, iend=0;
  int lane0_up[22], lane1_up[22];    /* Stored payload port that has it's "lane up" */
  int channel_up[22]; /* Stored payload port that has it's "channel up" */
  int firmware_version[NFPGA];
  unsigned int threshold_lsb, threshold_msb;
  unsigned int vmeslotmask=0;

  if(CTPp==NULL)
    {
      printf("%s: ERROR: CTP not initialized\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  fpga[U1].status0 = vmeRead32(&CTPp->fpga1.status0);
  fpga[U1].status1 = vmeRead32(&CTPp->fpga1.status1);
  fpga[U3].status0 = vmeRead32(&CTPp->fpga3.status0);
  fpga[U3].status1 = vmeRead32(&CTPp->fpga3.status1);
  fpga[U24].status0 = vmeRead32(&CTPp->fpga24.status0);
  fpga[U24].status1 = vmeRead32(&CTPp->fpga24.status1);

  fpga[U1].temp    = vmeRead32(&CTPp->fpga1.temp);
  fpga[U3].temp    = vmeRead32(&CTPp->fpga3.temp);
  fpga[U24].temp    = vmeRead32(&CTPp->fpga24.temp);

  fpga[U1].vint    = vmeRead32(&CTPp->fpga1.vint);
  fpga[U3].vint    = vmeRead32(&CTPp->fpga3.vint);
  fpga[U24].vint    = vmeRead32(&CTPp->fpga24.vint);

  fpga[U1].config0 = vmeRead32(&CTPp->fpga1.config0);
  fpga[U3].config0 = vmeRead32(&CTPp->fpga3.config0);
  fpga[U24].config0 = vmeRead32(&CTPp->fpga24.config0);

  threshold_lsb = vmeRead32(&CTPp->fpga24.sum_threshold_lsb);
  threshold_msb = vmeRead32(&CTPp->fpga24.sum_threshold_msb);
  TIUNLOCK;

  /* Loop over FPGAs and Channels to get the detailed status info. */
  for(ichan=0; ichan<6; ichan++)
    {
      for(ifpga=U1; ifpga<NFPGA; ifpga++)
	{
	  payloadport = ctpPayloadPort[ifpga][ichan];
	  if(payloadport==0)
	    continue;
	  
	  /* Get MGT Channel Up Status */
	  switch(payloadport)
	    {
	    case 15:
	    case 16:
	    case 4:
	      channel_up[payloadport] = fpga[ifpga].status1 & CTP_FPGA_STATUS1_CHANUP_EXTRA1;
	      break;
	      
	    case 6:
	      channel_up[payloadport] = fpga[ifpga].status1 & CTP_FPGA_STATUS1_CHANUP_EXTRA2;
	      break;
	      
	    default:
	      channel_up[payloadport] = fpga[ifpga].status0 & CTP_FPGA_STATUS0_CHAN_UP(ichan);
	      
	    }

	  /* Get MGT Lane0/1 Up Status */
	  lane0_up[payloadport] = fpga[ifpga].status0 & CTP_FPGA_STATUS0_LANE0_UP(ichan);
	  lane1_up[payloadport] = fpga[ifpga].status0 & CTP_FPGA_STATUS0_LANE1_UP(ichan);
	}
    }

  if(pflag==1)
    {
      /* Convert the payload port arrays into vme slot arrays */
      ibegin=3; iend=21;
      ppArray2vsArray((int *)&lane0_up);
      ppArray2vsArray((int *)&lane1_up);
      ppArray2vsArray((int *)&channel_up);
    }
  else
    {
      ibegin=1; iend=17;
    }

  /* Get the firmware versions */
  for(ifpga=U1; ifpga<NFPGA; ifpga++)
    firmware_version[ifpga] = 
      fpga[ifpga].status2 & CTP_FPGA_STATUS2_FIRMWARE_VERSION_MASK;
  
  /* Now printout what we've got */
  printf("STATUS for Crate Trigger Processor (CTP)\n");
  printf("--------------------------------------------------------------------------------\n");

  printf("  FPGA firmware versions:\n");
  for(ifpga=U1;ifpga<NFPGA;ifpga++)
    {
      printf("  %s: 0x%x\n",sfpga[ifpga],firmware_version[ifpga]);
    }

  if(pflag) 
    {
      printf("   Raw Regs:\n");
      for(ifpga=0;ifpga<NFPGA;ifpga++)
	{
	  printf("  %s: status0 0x%04x    status1 0x%04x\n",
		 sfpga[ifpga],fpga[ifpga].status0,fpga[ifpga].status1);
	  printf("  %s: temp    0x%04x    vint    0x%04x\n",
		 sfpga[ifpga],fpga[ifpga].temp,fpga[ifpga].vint);
	  printf("  %s: config0 0x%04x\n",
		 sfpga[ifpga],fpga[ifpga].config0);
	  if(ifpga==U24)
	    {
	      printf("  %s: thr_lsb 0x%04x    thr_msb 0x%04x\n",
		     sfpga[ifpga],threshold_lsb,threshold_msb);
	    }
	  printf("\n");
	}
    }

  if(pflag==1)
    printf("  VME Slots lanes up: \n\t");
  else
    printf("  Payload port lanes up: \n\t");

  printf(" 0: ");
  for(ii=ibegin; ii<iend; ii++)
    {
      if(lane0_up[ii])
	printf("%2d ",ii);
      else
	printf("   ");
    }
  printf("\n\t");
  printf(" 1: ");
  for(ii=ibegin; ii<iend; ii++)
    {
      if(lane1_up[ii])
	printf("%2d ",ii);
      else
	printf("   ");
    }
  printf("\n");

  if(pflag==1)
    printf("  VME Slots lanes down: \n\t");
  else
    printf("  Payload port lanes down: \n\t");

  printf(" 0: ");
  for(ii=ibegin; ii<iend; ii++)
    {
      if(!lane0_up[ii])
	printf("%2d ",ii);
      else
	printf("   ");
    }
  printf("\n\t");
  printf(" 1: ");
  for(ii=ibegin; ii<iend; ii++)
    {
      if(!lane1_up[ii])
	printf("%2d ",ii);
      else
	printf("   ");
    }

  printf("\n");


  if(pflag==1)
    printf("  VME Slots Channels up: \n\t");
  else
    printf("  Payload port Channels up: \n\t");

  printf("    ");
  for(ii=ibegin; ii<iend; ii++)
    {
      if(channel_up[ii])
	printf("%2d ",ii);
      else
	printf("   ");
    }
  printf("\n");

  if(pflag==1)
    printf("  VME Slots Channels down: \n\t");
  else
    printf("  Payload port Channels down: \n\t");

  printf("    ");
  for(ii=ibegin; ii<iend; ii++)
    {
      if(!channel_up[ii])
	printf("%2d ",ii);
      else
	printf("   ");
    }
  printf("\n");

  if(pflag==1)
    printf("  VME Slots Enabled: \n\t");
  else
    printf("  Payload ports Enabled: \n\t");

  vmeslotmask = tiPayloadPortMask2VMESlotMask(fpga[U1].config0);
  printf("    ");
  for(ii=ibegin; ii<iend; ii++)
    {
      if(pflag==1)
	{
	  if(fpga[U1].config0 & (1<<(ii-1)))
	    printf("%2d ",ii);
	  else
	    printf("   ");
	}
      else
	{
	  if(vmeslotmask & (1<<ii))
	    printf("%2d ",ii);
	  else
	    printf("   ");
	}
    }

  printf("\n\n");

  printf("  Threshold lsb = %d (0x%04x)\n",threshold_lsb,threshold_lsb);
  printf("  Threshold msb = %d (0x%04x)\n",threshold_msb,threshold_msb);

  printf("--------------------------------------------------------------------------------\n");
  printf("\n\n");

  return OK;
}

/*
  ctpSetFinalSumThreshold
  - Set the threshold for the Final Sum
  - Arm the trigger, if specified

*/
int
ctpSetFinalSumThreshold(unsigned int threshold, int arm)
{
  unsigned int threshold_lsb, threshold_msb;
  if(CTPp==NULL)
    {
      printf("%s: ERROR: CTP not initialized\n",__FUNCTION__);
      return ERROR;
    }
  if(arm < 0 || arm > 1)
    {
      printf("%s: Invalid value for arm (%d).  Must be 0 or 1.\n",
	     __FUNCTION__,arm);
      return ERROR;
    }


  threshold_lsb = threshold&0xffff;
  threshold_msb = threshold>>16;


  TILOCK;
  vmeWrite32(&CTPp->fpga24.sum_threshold_lsb, threshold_lsb);
  vmeWrite32(&CTPp->fpga24.sum_threshold_msb, threshold_msb);

  threshold_lsb = vmeRead32(&CTPp->fpga24.sum_threshold_lsb);
  threshold_msb = vmeRead32(&CTPp->fpga24.sum_threshold_msb);

  TIUNLOCK;

  printf("%s: Set to %d (0x%x)\n",
	 __FUNCTION__,threshold_lsb | (threshold_msb<<16), 
	 threshold_lsb | (threshold_msb<<16));

  return OK;
}

/*
  ctpGetFinalSumThreshold
  - Return the value set for the Final Sum threshold

*/
int
ctpGetFinalSumThreshold(int pflag)
{
  unsigned int rval;
  if(CTPp==NULL)
    {
      printf("%s: ERROR: CTP not initialized\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  rval = vmeRead32(&CTPp->fpga24.sum_threshold_lsb);
  rval |= (vmeRead32(&CTPp->fpga24.sum_threshold_msb)<<16);
  TIUNLOCK;

  if(pflag)
    {
      printf("%s: Set to %d (0x%x)\n",
	     __FUNCTION__,rval, rval);
    }
  return rval;
}

/*
  ctpSetPayloadEnableMask
  - Set the payload ports from the input MASK to be enabled.
  RETURNS: OK if successful, otherwise ERROR.
  - Mask Convention: 
    bit 0: Port 1
    bit 1: Port 2
    ...
    bit 5: Port 6
    .etc.

*/
int
ctpSetPayloadEnableMask(int enableMask)
{
  unsigned int chipMask=0;
  if(CTPp==NULL)
    {
      printf("%s: ERROR: CTP not initialized\n",__FUNCTION__);
      return ERROR;
    }

  if( enableMask >= (1<<(17-1)) ) /* 16 = Maximum Payload port number */
    {
      printf("%s: ERROR: Invalid enableMask (0x%x).  Includes payload port > 16.\n",
	     __FUNCTION__,enableMask);
      return ERROR;
    }

  TILOCK;
  chipMask = enableMask;
  vmeWrite32(&CTPp->fpga1.config0,chipMask);
  vmeWrite32(&CTPp->fpga3.config0,chipMask);
  vmeWrite32(&CTPp->fpga24.config0,chipMask);
  TIUNLOCK;

  printf("%s: Set enable mask to 0x%08x\n",__FUNCTION__,chipMask);

  return OK;
}

/*
  ctpSetVmeSlotEnableMask
  - Set the VME slots from the input MASK to be enabled.
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
ctpSetVmeSlotEnableMask(unsigned int vmemask)
{
  unsigned int payloadmask=0;
  unsigned int slot=0, payloadport=0;
  unsigned int ibit;

  if(CTPp==NULL)
    {
      printf("%s: ERROR: CTP not initialized\n",__FUNCTION__);
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

  ctpSetPayloadEnableMask(payloadmask);

  return OK;

}


/*
  ctpGetAllChanUp 
  - Returns the status of all configured channels up, from each chip where
    bit0: U1
    bit1: U3
    bit2: U24
*/

int
ctpGetAllChanUp(int pflag)
{
  int chip1, chip3, chip24;

  if(CTPp==NULL)
    {
      printf("%s: ERROR: CTP not initialized\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  chip1  = vmeRead32(&CTPp->fpga1.status1) & (CTP_FGPA_STATUS1_ALLCHANUP);
  chip3  = vmeRead32(&CTPp->fpga3.status1) & (CTP_FGPA_STATUS1_ALLCHANUP);
  chip24 = vmeRead32(&CTPp->fpga24.status1) & (CTP_FGPA_STATUS1_ALLCHANUP);
  TIUNLOCK;

  if(pflag)
    {
      printf("%s: chip1 = %d, chip3 = %d, chip24 = %d\n",
	     __FUNCTION__,chip1,chip3,chip24);
    }

  return (chip1>>1) | chip3 | (chip24<<1);

}

int
ctpGetErrorLatchFS(int pflag)
{
  int rval=0;

  if(CTPp==NULL)
    {
      printf("%s: ERROR: CTP not initialized\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  rval = vmeRead32(&CTPp->fpga24.status1) & (CTP_FPGA24_STATUS1_ERROR_LATCH_FS);
  TIUNLOCK;

  if(rval)
    rval=1;

  if(pflag)
    {
      if(rval)
	printf("%s: ERROR: Bad summing sequence!\n",__FUNCTION__);
      else
	printf("%s: Summing sequence is OK.\n",__FUNCTION__);
    }

  return (rval);

}

int
ctpGetAlignmentStatus(int pflag, int ntries)
{
  int itry=0, rval=0;

  if(CTPp==NULL)
    {
      printf("%s: ERROR: CTP not initialized\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  for(itry=0; itry<ntries; itry++)
    {
      rval = vmeRead32(&CTPp->fpga24.status1) & CTP_FPGA24_STATUS1_ALIGNMENT_SUCCESS;
      
      if(rval)
	{
	  rval=1;
	  break;
	}
    }

  TIUNLOCK;

  if(pflag)
    {
      if(rval==0)
	printf("%s: ERROR: Bad Alignment Status!\n",__FUNCTION__);
      else
	printf("%s: Alignment Status is OK.\n",__FUNCTION__);
    }

  return (rval);

}


int
ctpAlignAtSyncReset(int enable)
{
  unsigned int reg1=0, reg3=0, reg24=0;
  if(CTPp==NULL)
    {
      printf("%s: ERROR: CTP not initialized\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  reg1  = vmeRead32(&CTPp->fpga1.config1) & 0xFFFF;
  reg3  = vmeRead32(&CTPp->fpga3.config1) & 0xFFFF;
  reg24 = vmeRead32(&CTPp->fpga24.config1) & 0xFFFF;

  if(enable)
    {
      vmeWrite32(&CTPp->fpga1.config1, reg1  | CTP_FPGA_CONFIG1_ALIGN_AT_SYNCRESET);
      vmeWrite32(&CTPp->fpga3.config1, reg3  | CTP_FPGA_CONFIG1_ALIGN_AT_SYNCRESET);
      vmeWrite32(&CTPp->fpga24.config1,reg24 | CTP_FPGA_CONFIG1_ALIGN_AT_SYNCRESET);
    }
  else
    {
      vmeWrite32(&CTPp->fpga1.config1, reg1  & ~CTP_FPGA_CONFIG1_ALIGN_AT_SYNCRESET);
      vmeWrite32(&CTPp->fpga3.config1, reg3  & ~CTP_FPGA_CONFIG1_ALIGN_AT_SYNCRESET);
      vmeWrite32(&CTPp->fpga24.config1,reg24 & ~CTP_FPGA_CONFIG1_ALIGN_AT_SYNCRESET);
    }
  TIUNLOCK;

  return OK;


}

int
ctpArmHistoryBuffer()
{
  if(CTPp==NULL)
    {
      printf("%s: ERROR: CTP not initialized\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  vmeWrite32(&CTPp->fpga24.config1,CTP_FPGA24_CONFIG1_ARM_HISTORY_BUFFER);
  vmeWrite32(&CTPp->fpga24.config1,0);
  TIUNLOCK;

  return OK;
}

int
ctpDReady()
{
  int rval=0;
  if(CTPp==NULL)
    {
      printf("%s: ERROR: CTP not initialized\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  rval = vmeRead32(&CTPp->fpga24.status1) & CTP_FPGA24_STATUS1_HISTORY_BUFFER_READY;
  TIUNLOCK;
  
  if(rval)
    rval=1;

  return rval;
}

int
ctpReadEvent(volatile unsigned int *data, int nwrds)
{
  int ii=0, dCnt=0;
  if(CTPp==NULL)
    {
      logMsg("\nctpReadEvent: ERROR: CTP not initialized\n",0,0,0,0,0,0);
      return ERROR;
    }
  if(data==NULL) 
    {
      logMsg("\nctpReadEvent: ERROR: Invalid Destination address\n",0,0,0,0,0,0);
      return(ERROR);
    }
  if(nwrds>512)
    {
      logMsg("\nctpReadEvent: ERROR: Invalid nwrds (%d).  Must be less than 512.\n",
	     nwrds,0,0,0,0,0);
      return ERROR;
    }

  TILOCK;
  while(ii<nwrds)
    {
      data[ii] = (vmeRead32(&CTPp->fpga24.history_buffer_lsb) 
		  | (vmeRead32(&CTPp->fpga24.history_buffer_msb)<<16)) & CTP_DATA_MASK;
#ifndef VXWORKS
      data[ii] = LSWAP(data[ii]);
#endif
      ii++;
    }
  ii++;

  /* Use this to clear the data ready bit (dont set back to zero) */
  vmeWrite32(&CTPp->fpga24.config1,CTP_FPGA24_CONFIG1_ARM_HISTORY_BUFFER);
  TIUNLOCK;

  dCnt += ii;
  return dCnt;

}

void
ctpFiberReset()
{
  if(CTPp==NULL)
    {
      printf("%s: ERROR: CTP not initialized\n",__FUNCTION__);
      return;
    }

  TILOCK;
  vmeWrite32(&CTPp->fpga24.config1, CTP_FPGA24_CONFIG1_RESET_FIBER_MGT);
  vmeWrite32(&CTPp->fpga24.config1, 0);
  TIUNLOCK;

}

void
ctpPayloadReset()
{
  if(CTPp==NULL)
    {
      printf("%s: ERROR: CTP not initialized\n",__FUNCTION__);
      return;
    }

  TILOCK;
  vmeWrite32(&CTPp->fpga1.config1, CTP_FGPA_CONFIG1_INIT_ALL_MGT);
  vmeWrite32(&CTPp->fpga1.config1, 0);

  vmeWrite32(&CTPp->fpga3.config1, CTP_FGPA_CONFIG1_INIT_ALL_MGT);
  vmeWrite32(&CTPp->fpga3.config1, 0);

  vmeWrite32(&CTPp->fpga24.config1, CTP_FGPA_CONFIG1_INIT_ALL_MGT);
  vmeWrite32(&CTPp->fpga24.config1, 0);
  TIUNLOCK;

}

int
ctpResetScalers()
{
  if(CTPp==NULL)
    {
      printf("%s: ERROR: CTP not initialized\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  vmeWrite32(&CTPp->fpga24.scaler_ctrl,
	     CTP_SCALER_CTRL_RESET_SYNC |
	     CTP_SCALER_CTRL_RESET_TRIG1 |
	     CTP_SCALER_CTRL_RESET_TRIG2);
  vmeWrite32(&CTPp->fpga24.scaler_ctrl,0);
  TIUNLOCK;

  return OK;
}

int
ctpResetSyncScaler()
{
  if(CTPp==NULL)
    {
      printf("%s: ERROR: CTP not initialized\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  vmeWrite32(&CTPp->fpga24.scaler_ctrl, CTP_SCALER_CTRL_RESET_SYNC);
  vmeWrite32(&CTPp->fpga24.scaler_ctrl, 0);
  TIUNLOCK;

  return ERROR;
}

int
ctpResetTrig1Scaler()
{
  if(CTPp==NULL)
    {
      printf("%s: ERROR: CTP not initialized\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  vmeWrite32(&CTPp->fpga24.scaler_ctrl, CTP_SCALER_CTRL_RESET_TRIG1);
  vmeWrite32(&CTPp->fpga24.scaler_ctrl, 0);
  TIUNLOCK;

  return ERROR;
}

int
ctpResetTrig2Scaler()
{
  if(CTPp==NULL)
    {
      printf("%s: ERROR: CTP not initialized\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  vmeWrite32(&CTPp->fpga24.scaler_ctrl, CTP_SCALER_CTRL_RESET_TRIG2);
  vmeWrite32(&CTPp->fpga24.scaler_ctrl, 0);
  TIUNLOCK;

  return ERROR;
}

int
ctpGetClockScaler()
{
  int rval=0;
  if(CTPp==NULL)
    {
      printf("%s: ERROR: CTP not initialized\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  rval = vmeRead32(&CTPp->fpga24.clock_scaler) & CTP_CLOCK_SCALER_COUNT_MASK;
  TIUNLOCK;

  return rval;
}

int
ctpGetSyncScaler()
{
  int rval=0;
  if(CTPp==NULL)
    {
      printf("%s: ERROR: CTP not initialized\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  rval = vmeRead32(&CTPp->fpga24.sync_scaler) & CTP_SCALER_COUNT_MASK;
  TIUNLOCK;

  return rval;

}

int
ctpGetTrig1Scaler()
{
  int rval=0;
  if(CTPp==NULL)
    {
      printf("%s: ERROR: CTP not initialized\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  rval = vmeRead32(&CTPp->fpga24.trig1_scaler) & CTP_SCALER_COUNT_MASK;
  TIUNLOCK;

  return rval;

}

int
ctpGetTrig2Scaler()
{
  int rval=0;
  if(CTPp==NULL)
    {
      printf("%s: ERROR: CTP not initialized\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  rval = vmeRead32(&CTPp->fpga24.trig2_scaler) & CTP_SCALER_COUNT_MASK;
  TIUNLOCK;

  return rval;

}

int
ctpGetSerialNumber(char **rval)
{
  int iaddr=0, byte=0;
  int sn[8], ret_len;
  char sn_str[20];
  if(CTPp==NULL)
    {
      printf("%s: ERROR: CTP not initialized\n",__FUNCTION__);
      return ERROR;
    }
  
  for(iaddr=0; iaddr<8; iaddr++)
    {
      byte = ctpSROMRead(iaddr,1000);
      if(byte==-1)
	{
	  printf("%s: ERROR Reading SROM (addr = %d)\n",__FUNCTION__,iaddr);
	  return ERROR;
	}
      sn[iaddr] = byte;
    }
  printf("\n");

  sprintf(sn_str,"%c%c%c%c%c-%c%c%c",sn[0],sn[1],sn[2],sn[3],sn[4],sn[5],sn[6],sn[7]);

  if(*rval != NULL)
    {
      strcpy((char *)rval,sn_str);
      ret_len = (int)strlen(sn_str);
    }
  else
    ret_len = 0;
  
  printf("%s: CTP Serial Number: %s\n",__FUNCTION__,sn_str);

  return ret_len;
}

static int
ctpSROMRead(int addr, int ntries)
{
  int itry, rval, dataValid=0;
  int maxAddr=CTP_FPGA3_CONFIG2_SROM_ADDR_MASK;
  if(CTPp==NULL)
    {
      printf("%s: ERROR: CTP not initialized\n",__FUNCTION__);
      return ERROR;
    }

  if(addr>maxAddr)
    {
      printf("%s: ERROR: addr (0x%x) > maxAddr (0x%x)\n",
	     __FUNCTION__,addr,maxAddr);
      return ERROR;
    }

  TILOCK;
  vmeWrite32(&CTPp->fpga3.config2, 0);
  vmeWrite32(&CTPp->fpga3.config2, addr | CTP_FPGA3_CONFIG2_SROM_READ);
  vmeWrite32(&CTPp->fpga3.config2, addr);

  for(itry=0; itry<ntries; itry++)
    {
      rval = vmeRead32(&CTPp->fpga3.status3);
      if(rval & CTP_FPGA3_STATUS3_SROM_DATA_VALID)
	{
	  rval &= CTP_FPGA3_STATUS3_SROM_DATA_MASK;
	  dataValid=1;
	  break;
	}
    }

  vmeWrite32(&CTPp->fpga3.config2, 0);
  TIUNLOCK;

  if(!dataValid)
    {
      printf("%s: Timeout on SROM Read\n",__FUNCTION__);
      rval = ERROR;
    }

  return rval;
}

/*************************************************************
 * Firmware Updating Tools
 *************************************************************/
int
ctpSetFWSize(int size)
{
  if(size<=MAX_FIRMWARE_SIZE)
    {
      printf("%s: INFO: Setting override firmware size to %d\n",
	     __FUNCTION__,size);
      my_fw_data_size=size;
    }
  else
    {
      printf("%s: ERROR: Override size (%d) greater than maximum size (%d)\n",
	     __FUNCTION__,size,MAX_FIRMWARE_SIZE);
      return ERROR;
    }
  return OK;
}

int
ctpSetNSectorErase(int nsectors)
{
  if(nsectors<=1024)
    {
      printf("%s: INFO: Setting override erase sectors to %d\n",
	     __FUNCTION__,nsectors);
      my_erase_n_sectors=nsectors;
    }
  else
    {
      printf("%s: ERROR: Override erase # sectors (%d) greater than 1024\n",
	     __FUNCTION__,nsectors);
      return ERROR;
    }
  return OK;
}

int
ctpFirmwareUpload(char *fw_filename, int ifpga, int reboot)
{
  int stat;
  if(CTPp==NULL)
    {
      printf("%s: ERROR: CTP not initialized\n",__FUNCTION__);
      return ERROR;
    }

  if((ifpga<U1) | (ifpga>U24))
    {
      printf("%s: Invalid FPGA choice (%d)\n",__FUNCTION__,ifpga);
      return ERROR;
    }

  stat = ctpReadFirmwareFile(fw_filename);
  if(stat==ERROR)
    return ERROR;

  if(my_fw_data_size!=0)
    {
      if(my_fw_data_size>fw_data_size)
	{
	  printf("%s: ERROR: Override firmware size (%d) > original size (%d)\n",
		 __FUNCTION__,
		 fw_data_size, my_fw_data_size);
	  return ERROR;
	}
      printf("%s: INFO: Overriding firmware size (%d) with %d\n",
	     __FUNCTION__,
	     fw_data_size, my_fw_data_size);
      fw_data_size = my_fw_data_size;
    }
 
  /* Erase CROM */
  printf("\n%s: Erasing CROM \n",__FUNCTION__);
  stat = ctpCROMErase(ifpga);
  if(stat==ERROR)
    return ERROR;
  
  /* Data to SRAM */
  printf("\n%s: Loading SRAM with data \n",__FUNCTION__);
  stat = ctpWriteFirmwareToSRAM();
  if(stat==ERROR)
    return ERROR;

  /* Compare SRAM to Data Array */
  printf("\n%s: Verifying data \n",__FUNCTION__);
  stat = ctpVerifySRAMData();
  if(stat==ERROR)
    return ERROR;

  /* SRAM TO CROM */
  printf("\n%s: Loading CROM with SRAM data \n",__FUNCTION__);
  stat = ctpProgramCROMfromSRAM(ifpga);
  if(stat==ERROR)
    return ERROR;

  /* CROM TO SRAM (For verification) */
  printf("\n%s: Loading SRAM with CROM data \n",__FUNCTION__);
  stat = ctpWriteCROMToSRAM(ifpga);
  if(stat==ERROR)
    return ERROR;

  /* Compare SRAM to Data Array */
  printf("\n%s: Verifying data \n",__FUNCTION__);
  stat = ctpVerifySRAMData();
  if(stat==ERROR)
    return ERROR;

  if(reboot)
    {
      /* CROM to FPGA (Reboot FPGA) */
      printf("\n%s: Rebooting FPGAs \n",__FUNCTION__);
      stat = ctpRebootAllFPGA();
      if(stat==ERROR)
	return ERROR;
    }

  printf("\n%s: Done programming CTP FPGA %d\n",
	 __FUNCTION__,ifpga);

  return OK;
}

static int
hex2num(char c)
{
  c = toupper(c);

  if(c > 'F')
    return 0;

  if(c >= 'A')
    return 10 + c - 'A';

  if((c > '9') || (c < '0') )
    return 0;

  return c - '0';
}


int
ctpReadFirmwareFile(char *fw_filename)
{
  FILE *fwFile=NULL;
  char ihexLine[200], *pData;
  int len=0, datalen=0;
  unsigned int line=0, nbytes=0, hiChar=0, loChar=0;
  unsigned int readFWfile=0;

  /* Initialize global variables */
  memset((char *)fw_data,0,sizeof(fw_data));
  fw_data_size=0;
  fw_file_loaded=0;

  memset((char *)ihexLine,0,sizeof(ihexLine));

  fwFile = fopen(fw_filename,"r");
  if(fwFile==NULL)
    {
      perror("fopen");
      printf("%s: ERROR opening file (%s) for reading\n",
	     __FUNCTION__,fw_filename);
      return ERROR;
    }

  while(!feof(fwFile))
    {
      /* Get the current line */
      if(!fgets(ihexLine, sizeof(ihexLine), fwFile))
	break;
      
      /* Get the the length of this line */
      len = strlen(ihexLine);

      if(len >= 5)
	{
	  /* Check for the start code */
	  if(ihexLine[0] != ':')
	    {
	      printf("%s: ERROR parsing file at line %d\n",
		     __FUNCTION__,line);
	      return ERROR;
	    }

	  /* Get the byte count */
	  hiChar = hex2num(ihexLine[1]);
	  loChar = hex2num(ihexLine[2]);
	  datalen = (hiChar)<<4 | loChar;

	  if(strncmp("00",&ihexLine[7], 2) == 0) /* Data Record */
	    {
	      pData = &ihexLine[9]; /* point to the beginning of the data */
	      while(datalen--)
		{
		  hiChar = hex2num(*pData++);
		  loChar = hex2num(*pData++);
		  fw_data[readFWfile] = 
		    ((hiChar)<<4) | (loChar);
		  if(readFWfile>=MAX_FIRMWARE_SIZE)
		    {
		      printf("%s: ERROR: TOO BIG!\n",__FUNCTION__);
		      return ERROR;
		    }
		  readFWfile++;
		  nbytes++;
		}
	    }

	}
      line++;
    }

  fw_data_size = readFWfile;
  
#define DEBUGFILE
#ifdef DEBUGFILE
  printf("fw_data_size = %d (0x%x)\n",fw_data_size, fw_data_size);

  int ichar=0;
  for(ichar=0; ichar<16*10; ichar++)
    {
      if((ichar%16) == 0)
	printf("\n");
      printf("0x%02x ",fw_data[ichar]);
    }
  printf("\n\n");
#endif
  fw_file_loaded = 1;

  fclose(fwFile);
  return OK;


}


static int
ctpCROMErase(int fpga)
{
  int iblock=0, stat=0;
  int nsectors = 1024; /* NOTE U1: U3: 400, U24: 583 */
  unsigned int eraseCommand=0;
  if(CTPp==NULL)
    {
      printf("%s: ERROR: CTP not initialized\n",__FUNCTION__);
      return ERROR;
    }

  switch(fpga)
    {
    case U1: 
      eraseCommand=CTP_FPGA1_CONFIG2_U1_CONFIG | CTP_FPGA1_CONFIG2_ERASE_EPROM_U1;
      break;

    case U3:
      eraseCommand=CTP_FPGA1_CONFIG2_U3_CONFIG | CTP_FPGA1_CONFIG2_ERASE_EPROM_U3;
      break;

    case U24:
      eraseCommand=CTP_FPGA1_CONFIG2_U24_CONFIG | CTP_FPGA1_CONFIG2_ERASE_EPROM_U24;
      break;

    default:
      printf("%s: ERROR: Invalid fpga (%d)\n",__FUNCTION__,fpga);
      return ERROR;
    }

  if(my_erase_n_sectors!=0)
    {
      printf("%s: INFO: Overriding Erase sectors to %d\n",
	     __FUNCTION__,
	     my_erase_n_sectors);
      nsectors = my_erase_n_sectors;
    }

  TILOCK;
  for(iblock=0; iblock<nsectors; iblock++)
    {
      /* Write block number to erase */
      vmeWrite32(&CTPp->fpga1.config3, iblock);

      /* Beginning of opCode */
      vmeWrite32(&CTPp->fpga1.config2,eraseCommand);
      vmeWrite32(&CTPp->fpga1.config2,eraseCommand | CTP_FPGA1_CONFIG2_EXEC_OPCODE);
      stat = ctpWaitForCommandDone(1000);
      if(stat==ERROR)
	{
	  printf("%s: ERROR Sending Opcode when erasing fpga (%d)\n",__FUNCTION__,fpga);
	  TIUNLOCK;
	  return ERROR;
	}

      /* End of opCode */
      vmeWrite32(&CTPp->fpga1.config2,eraseCommand);

      if((iblock%10)==0)
	{
	  printf("*"); fflush(stdout);
	}
      
      /* Wait for at least 220 milliseconds */
      taskDelay(13);
    }
  TIUNLOCK;

  return OK;
}

static int
ctpWaitForCommandDone(int ntries)
{
  int itries=0, done=0;
  unsigned int rval=0;
  if(CTPp==NULL)
    {
      printf("%s: ERROR: CTP not initialized\n",__FUNCTION__);
      return ERROR;
    }
  
  for(itries=0; itries<ntries; itries++)
    {
      rval = vmeRead32(&CTPp->fpga1.status2);
/*       printf("%s: rval = 0x%04x\n",__FUNCTION__,rval); */
      if(rval & CTP_FPGA1_STATUS2_READY_FOR_OPCODE)
	{
	  done=1;
	  break;
	}
      taskDelay(1);
      if((itries%200)==0)
	{
	  printf("."); fflush(stdout);
	}
    }

#ifdef DEBUGCOMMAND
  printf("\n");
  printf("%s: done = %d  itries = %d  ntries = %d\n",__FUNCTION__,
	 done, itries, ntries);
#endif

  if(!done)
    rval = ERROR;
  else
    rval = OK;

  return rval;
}


static int
ctpWriteFirmwareToSRAM()
{
  unsigned int stat=0;
  int ibyte=0, iaddr=0;
  unsigned short data=0;
  if(CTPp==NULL)
    {
      printf("%s: ERROR: CTP not initialized\n",__FUNCTION__);
      return ERROR;
    }

  if(!fw_file_loaded | (fw_data_size==0))
    {
      printf("%s: ERROR: Firmware file not loaded.\n",
	     __FUNCTION__);
      return ERROR;
    }
  
  TILOCK;
  /* Make sure opCode ready */
  stat = ctpWaitForCommandDone(100);
  if(stat!=OK)
    {
      printf("%s: ERROR: OPCode wait timeout.\n",__FUNCTION__);
      TIUNLOCK;
      return ERROR;
    }
  
  /* Enter in the Download opCode */
  vmeWrite32(&CTPp->fpga1.config2, CTP_FPGA1_CONFIG2_SRAM_WRITE);

  for(ibyte = 0; ibyte<fw_data_size; ibyte+=2)
    {
      data = (fw_data[ibyte]<<8) | fw_data[ibyte+1];
      vmeWrite32(&CTPp->fpga1.config4, data);
      vmeWrite32(&CTPp->fpga1.config5, iaddr & CTP_FPGA1_CONFIG5_SRAM_ADDR_MASK);
      vmeWrite32(&CTPp->fpga1.config6, CTP_FPGA1_CONFIG6_SRAM_WRITE | 
		 ((iaddr>>16) & CTP_FPGA1_CONFIG6_SRAM_ADDR_MASK));

      stat = ctpWaitForCommandDone(1000);
      if(stat!=OK)
	{
	  printf("%s: ERROR: OPCode wait timeout.\n",__FUNCTION__);
	  TIUNLOCK;
	  return ERROR;
	}

      vmeWrite32(&CTPp->fpga1.config6, 
		 ((iaddr>>16) & CTP_FPGA1_CONFIG6_SRAM_ADDR_MASK));
      iaddr++;

      if((ibyte%10000)==0)
	printf("*"); fflush(stdout);

    }
  TIUNLOCK;

  return OK;
}

static int
ctpVerifySRAMData()
{
  int ibyte=0, iaddr=0, stat=0;
  unsigned int data=0, rdata=0;
  int errorCount=0, zeroCount=0;
  if(CTPp==NULL)
    {
      printf("%s: ERROR: CTP not initialized\n",__FUNCTION__);
      return ERROR;
    }

  if(!fw_file_loaded | (fw_data_size==0))
    {
      printf("%s: ERROR: Firmware file not loaded.\n",
	     __FUNCTION__);
      return ERROR;
    }

  TILOCK;
  /* Select SRAM to READ */
  vmeWrite32(&CTPp->fpga1.config2, CTP_FPGA1_CONFIG2_SRAM_WRITE);

  for(ibyte=0; ibyte<fw_data_size; ibyte+=2)
    {
      data = (fw_data[ibyte]<<8) | fw_data[ibyte+1];
      vmeWrite32(&CTPp->fpga1.config5, iaddr & CTP_FPGA1_CONFIG5_SRAM_ADDR_MASK);
      vmeWrite32(&CTPp->fpga1.config6, CTP_FPGA1_CONFIG6_SRAM_READ |
		 ((iaddr>>16) & CTP_FPGA1_CONFIG6_SRAM_ADDR_MASK));
		 
      stat = ctpWaitForCommandDone(1000);
      if(stat!=OK)
	{
	  printf("%s: ERROR: OPCode wait timeout.\n",__FUNCTION__);
	  TIUNLOCK;
	  return ERROR;
	}
      
      vmeWrite32(&CTPp->fpga1.config6, 
		 ((iaddr>>16) & CTP_FPGA1_CONFIG6_SRAM_ADDR_MASK));

      rdata = vmeRead32(&CTPp->fpga1.status3) & CTP_FPGA1_STATUS3_SRAM_DATA_MASK;
      if(rdata==0)
	zeroCount++;

      if(rdata != data)
	{
	  errorCount++;
	  if(errorCount<=20)
	    {
	      printf("%s: ERROR: Invalid data read from SRAM (iaddr = 0x%x).\n\tExpected (0x%x) != Readback (0x%x)\n",
		     __FUNCTION__,iaddr,data,rdata);
	      if(errorCount==20)
		printf("%s: ERROR: Further errors are suppressed\n",
		       __FUNCTION__);
	    }
/* 	  TIUNLOCK; */
/* 	  return ERROR; */
	}
      iaddr++;

      if((ibyte%10000)==0)
	{
	  printf("."); fflush(stdout);
	}

    }

  TIUNLOCK;

  if(errorCount>0)
    {
      printf("%s: ERROR: Total data read errors = %d (out of %d)\n",
	     __FUNCTION__,errorCount,fw_data_size/2);
      return ERROR;
    }

  printf("%s: INFO: Zero Count = %d\n",__FUNCTION__,zeroCount);

  return OK;
}


static int
ctpProgramCROMfromSRAM(int ifpga)
{
  unsigned int opCode=0;
  int stat=0;
  if(CTPp==NULL)
    {
      printf("%s: ERROR: CTP not initialized\n",__FUNCTION__);
      return ERROR;
    }

  switch(ifpga)
    {
    case U1:
      opCode = CTP_FPGA1_CONFIG2_U1_CONFIG | CTP_FPGA1_CONFIG2_PROG_DATA_U1;
      break;

    case U3:
      opCode = CTP_FPGA1_CONFIG2_U3_CONFIG | CTP_FPGA1_CONFIG2_PROG_DATA_U3;
      break;

    case U24:
      opCode = CTP_FPGA1_CONFIG2_U24_CONFIG | CTP_FPGA1_CONFIG2_PROG_DATA_U24;
      break;

    default:
      printf("%s: Invalid FPGA choice (%d).\n",__FUNCTION__,ifpga);
      return ERROR;
    }

  TILOCK;
  vmeWrite32(&CTPp->fpga1.config2, opCode);
  vmeWrite32(&CTPp->fpga1.config2, opCode | CTP_FPGA1_CONFIG2_EXEC_OPCODE);

  /* Wait for "13 minutes"... 50000 = ~13.9minutes */
  stat = ctpWaitForCommandDone(100000);
  if(stat!=OK)
    {
      printf("%s: ERROR: OPCode (0x%x) wait timeout.\n",__FUNCTION__,opCode);
      TIUNLOCK;
      return ERROR;
    }

  vmeWrite32(&CTPp->fpga1.config2, opCode);

  TIUNLOCK;

  return OK;
}

static int
ctpWriteCROMToSRAM(int ifpga)
{
  unsigned int opCode=0;
  int stat=0;
  if(CTPp==NULL)
    {
      printf("%s: ERROR: CTP not initialized\n",__FUNCTION__);
      return ERROR;
    }

  switch(ifpga)
    {
    case U1:
      opCode = CTP_FPGA1_CONFIG2_U1_CONFIG | CTP_FPGA1_CONFIG2_READ_DATA_U1;
      break;

    case U3:
      opCode = CTP_FPGA1_CONFIG2_U3_CONFIG | CTP_FPGA1_CONFIG2_READ_DATA_U3;
      break;

    case U24:
      opCode = CTP_FPGA1_CONFIG2_U24_CONFIG | CTP_FPGA1_CONFIG2_READ_DATA_U24;
      break;

    default:
      printf("%s: Invalid FPGA choice (%d).\n",__FUNCTION__,ifpga);
      return ERROR;
    }

  TILOCK;
  vmeWrite32(&CTPp->fpga1.config2, opCode);
  vmeWrite32(&CTPp->fpga1.config2, opCode | CTP_FPGA1_CONFIG2_EXEC_OPCODE);

  stat = ctpWaitForCommandDone(100000);
  if(stat!=OK)
    {
      printf("%s: ERROR: OPCode (0x%x) wait timeout.\n",__FUNCTION__,opCode);
      TIUNLOCK;
      return ERROR;
    }

  vmeWrite32(&CTPp->fpga1.config2, opCode);
  TIUNLOCK;

  return OK;
}

static int
ctpRebootAllFPGA()
{
  if(CTPp==NULL)
    {
      printf("%s: ERROR: CTP not initialized\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  vmeWrite32(&CTPp->fpga3.config3,CTP_FPGA3_CONFIG3_REBOOT_ALL_FPGA);
  vmeWrite32(&CTPp->fpga3.config3,CTP_FPGA3_CONFIG3_REBOOT_SAFETY);
  vmeWrite32(&CTPp->fpga3.config3,CTP_FPGA3_CONFIG3_REBOOT_ALL_FPGA);
  TIUNLOCK;

  return OK;
}
