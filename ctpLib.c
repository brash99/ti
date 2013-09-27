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

#define DEBUG

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

/* Static function prototypes */
static int ctpSROMRead(int addr, int ntries);
static int ctpCROMErase(int fpga);
static int ctpWaitForCommandDone(int ntries);

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


/*
  ctpStatus
  - Display the status of the CTP registers 
*/
int
ctpStatus(int pflag)
{
  struct CTP_FPGA_U1_Struct fpga[NFPGA]; // Array to handle the "common" registers
  char sfpga[NFPGA][4] = {"U1", "U3", "U24"};
  int ichan, ifpga, payloadport, ipport;
  int lane0_up[16+1], lane1_up[16+1];    /* Stored payload port that has it's "lane up" */
  int channel_up[16+1]; /* Stored payload port that has it's "channel up" */
  int firmware_version[NFPGA];
  unsigned int threshold_lsb, threshold_msb;

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

  printf("  Payload port lanes up: \n\t");
  printf(" 0: ");
  for(ipport=1; ipport<17; ipport++)
    {
      if(lane0_up[ipport])
	printf("%2d ",ipport);
      else
	printf("   ");
    }
  printf("\n");
  printf(" 1: ");
  for(ipport=1; ipport<17; ipport++)
    {
      if(lane1_up[ipport])
	printf("%2d ",ipport);
      else
	printf("   ");
    }
  printf("\n");

  printf("  Payload port lanes down: \n\t");
  printf(" 0: ");
  for(ipport=1; ipport<17; ipport++)
    {
      if(!lane0_up[ipport])
	printf("%2d ",ipport);
      else
	printf("   ");
    }
  printf("\n");
  printf(" 1: ");
  for(ipport=1; ipport<17; ipport++)
    {
      if(!lane1_up[ipport])
	printf("%2d ",ipport);
      else
	printf("   ");
    }

  printf("\n");


  printf("  Payload port Channels up: \n\t");
  for(ipport=1; ipport<17; ipport++)
    {
      if(channel_up[ipport])
	printf("%2d ",ipport);
      else
	printf("   ");
    }
  printf("\n");

  printf("  Payload port Channels down: \n\t");
  for(ipport=1; ipport<17; ipport++)
    {
      if(!channel_up[ipport])
	printf("%2d ",ipport);
      else
	printf("   ");
    }
  printf("\n");

  printf("  Payload ports Enabled: \n\t");
  for(ipport=1; ipport<17; ipport++)
    {
      if(fpga[U1].config0 & (1<<(ipport-1)))
	printf("%2d ",ipport);
      else
	printf("   ");
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
  
  for(iaddr=0; iaddr<8; iaddr++)
    {
      byte = ctpSROMRead(iaddr,100);
      if(byte==-1)
	{
	  printf("%s: ERROR Reading SROM\n",__FUNCTION__);
	  return ERROR;
	}
      sn[iaddr] = byte;
    }
  
  sprintf(sn_str,"%c%c%c%c%c-%c%c%c",sn[0],sn[1],sn[2],sn[3],sn[4],sn[5],sn[6],sn[7]);

  if(*rval != NULL)
    {
      strcpy((char *)rval,sn_str);
      ret_len = (int)strlen(sn_str);
    }
  else
    ret_len = 0;
  
  return ret_len;
}


static int
ctpSROMRead(int addr, int ntries)
{
  int itry, rval, dataValid=0;
  int maxAddr=CTP_FPGA3_CONFIG2_SROM_ADDR_MASK;

  if(addr>maxAddr)
    {
      printf("%s: ERROR: addr (0x%x) > maxAddr (0x%x)\n",
	     __FUNCTION__,addr,maxAddr);
      return ERROR;
    }

  TILOCK;
  vmeWrite32(&CTPp->fpga3.config2, 0);
  vmeWrite32(&CTPp->fpga3.config2, addr | CTP_FPGA3_CONFIG2_SROM_READ);

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

static int
ctpCROMErase(int fpga)
{
  int iblock=0, stat=0;
  unsigned int eraseCommand=0;

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

  TILOCK;
  for(iblock=0; iblock<1024; iblock++)
    {
      /* Write block number to erase */
      vmeWrite32(&CTPp->fpga1.config3, iblock);

      /* Beginning of opCode */
      eraseCommand |= CTP_FPGA1_CONFIG2_EXEC_OPCODE;
      vmeWrite32(&CTPp->fpga1.config2,eraseCommand);
      stat = ctpWaitForCommandDone(1000);
      if(stat==ERROR)
	{
	  printf("%s: ERROR Sending Opcode when erasing fpga (%d)\n",__FUNCTION__,fpga);
	  TIUNLOCK;
	  return ERROR;
	}

      /* End of opCode */
      eraseCommand &= ~CTP_FPGA1_CONFIG2_EXEC_OPCODE;
      vmeWrite32(&CTPp->fpga1.config2,eraseCommand);
      
      /* Wait for at least 220 milliseconds */
      taskDelay(1);
    }
  TIUNLOCK;

  return OK;
}

static int
ctpWaitForCommandDone(int ntries)
{
  int itries=0, done=0;
  unsigned int rval=0;
  
  for(itries=0; itries<ntries; itries++)
    {
#ifdef NOTDEFINEDYET
      rval = vmeRead32(&CTPp->fpga1.register12);
#endif
      if(rval & (1<<15))
	{
	  done=1;
	  break;
	}
      /* May want to put delay in here... and some output to show progress */
    }

  if(!done)
    rval = ERROR;
  else
    rval = OK;

  return rval;
}
