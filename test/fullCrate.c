/*
 * File:
 *    fullCrate.c
 *
 * Description:
 *    Vme TI interrupts with GEFANUC Linux Driver
 *    and TI, FADC library
 *
 *
 */


#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "jvme.h"
#include "fadcLib.h"
#include "tiLib.h"

#define BLOCKLEVEL 1
#define USETOKEN

/* access tirLib global variables */
extern unsigned int tiIntCount;
extern unsigned int tiDaqCount;
DMA_MEM_ID vmeIN,vmeOUT;
extern volatile struct TI_A24RegStruct *TIp;

/*! Buffer node pointer */
extern DMANODE *the_event;
/*! Data pointer */
extern unsigned int *dma_dabufp;

/* FADC Defaults/Globals */
#define FADC_ADDR (9<<19)
#define FADC_THRESHOLD     1
#define FADC_WINDOW_LAT   400
#define FADC_WINDOW_WIDTH 25
#define FADC_DAC_LEVEL 3250
extern int fadcA32Base;
extern volatile struct fadc_struct *FAp[(FA_MAX_BOARDS+1)];
extern int fadcA24Offset;
extern int nfadc;
extern int fadcID[FA_MAX_BOARDS];
extern unsigned int fadcAddrList[FA_MAX_BOARDS];
/* extern int fadcBlockError; */
int FA_SLOT, FA_PP;
int NFADC=1; /* May change, depending on crate */
unsigned int fadcSlotMask=0;

/* for the calculation of maximum data words in the block transfer */
unsigned int MAXFADCWORDS=0;

unsigned int fadc_threshold=FADC_THRESHOLD;
unsigned int fadc_window_lat=FADC_WINDOW_LAT, fadc_window_width=FADC_WINDOW_WIDTH;

/* Interrupt Service routine */
void
mytiISR(int arg)
{
  unsigned int gready=0;
  int dCnt, len=0,idata,islot,stat;
  DMANODE *outEvent;

/*   if(tiIntCount==100) */
/*     tiTrigLinkReset(); */


  GETEVENT(vmeIN,tiIntCount);

  dCnt = tiReadBlock(dma_dabufp,900>>2,1);
  if(dCnt<=0)
    {
      printf("No data or error.  dCnt = %d\n",dCnt);
    }
  else
    {
      dma_dabufp += dCnt;
/*       printf("dCnt = %d\n",dCnt); */
    
    }

  /* Readout FADC */
  if(NFADC!=0)
    {
      int itime, roflag=1;
#ifdef USETOKEN
      if(NFADC>1) roflag=2;
#else
      if(NFADC>1) roflag=1;
#endif

#ifndef USETOKEN
      for(islot=0; islot<NFADC; islot++)
#else
      islot=0;
#endif
      {
	for(itime=0;itime<100;itime++) 
	  {
#ifdef USETOKEN
	    FA_SLOT = fadcID[0];
	    gready = faGBready();
	    stat = (gready == fadcSlotMask);
#else
	    FA_SLOT = fadcID[islot];
	    stat = faBready(FA_SLOT);
#endif
	    if (stat>0) 
	      {
		break;
	      }
	  }
	if(stat>0) 
	  {
	    dCnt = faReadBlock(FA_SLOT,dma_dabufp,MAXFADCWORDS,roflag);
	    if(dCnt<=0)
	      {
		printf("FADC%2d: No data or error.  dCnt = %d\n",FA_SLOT,dCnt);
	      }
	    else
	      {
/* 		printf("FADC%2d: dCnt = %d\n",FA_SLOT,dCnt); */
		dma_dabufp += dCnt;
	      }
	  } 
	else 
	  {
	    printf ("FADC%d: no events   stat=%d  intcount = %d   gready = 0x%08x  fadcSlotMask = 0x%08x\n",
		    FA_SLOT,stat,tiGetIntCount(),gready,fadcSlotMask);
	  }
      }
      if(roflag==2)
	{
	  for(islot=0; islot<NFADC; islot++)
	    {
	      FA_SLOT = fadcID[islot];
	      faResetToken(FA_SLOT);
	    }
	}
    }


  PUTEVENT(vmeOUT);
  
  outEvent = dmaPGetItem(vmeOUT);
/* #define READOUT */
#ifdef READOUT
  if(tiIntCount%10000==0)
    {
      printf("Received %d triggers... tiDaqCount = %d\n",
	     tiIntCount,tiDaqCount);

      len = outEvent->length;
      
      for(idata=0;idata<len;idata++)
	{
	  if((idata%5)==0) printf("\n\t");
	  printf("  0x%08x ",(unsigned int)LSWAP(outEvent->data[idata]));
	}
      printf("\n\n");
    }
#else
  if(tiIntCount%10000==0)
    {
      printf("Received %d triggers... tiDaqCount = %d\n",
	     tiIntCount,tiDaqCount);
    }

#endif
  dmaPFreeItem(outEvent);
/*   sleep(1); */
}


int 
main(int argc, char *argv[]) 
{

  int stat;
  int useMask=0;
  unsigned int usermask=0x0; /* in PP */
  unsigned int slot=0, vmeslot=0, vmeslotmask=0x0;
  int ibit=0, ilist=0, nFADC_from_mask=0;

  printf("\nJLAB Full Crate Block Read Test\n");
  printf("----------------------------\n");

  /* Handle command line arguments */
  if(argc==3) /* Hope we're using a usermask here */
    {
      if(strcmp(argv[1],"-m") == 0)
	{
	  usermask = (unsigned int) strtoll(argv[2],NULL,16)&0xffffffff;
	  useMask=1;
	}
      else
	{
	  printf("ERROR: unrecognized option %s\n",argv[1]);
	  return -1;
	}
    }
  else if(argc==2 || argc>3)
    {
      printf("ERROR: This program uses no options, or -m [MASK]\n");
      return -1;
    }

  if(useMask)
    {
      if(usermask>0xffff)
	{
	  printf("ERROR: Invalid payload port mask value (0x%X).  Must be <= 0xFFFF\n",usermask);
	  return -1;
	}
      printf("INFO: Will use payload port mask = 0x%04X\n",usermask);
      /* Build the address list from the usermask (in payload ports) */
      /* Convert the vmemask to the payload port mask */
      for (ibit=0; ibit<16; ibit++)
	{
	  if(usermask & (1<<ibit))
	    {
	      slot = ibit+1;
#ifdef DEBUGMASK
	      printf("payloadport = %d\n",slot);
#endif
	      vmeslot  = tiPayloadPort2VMESlot(slot);
	      vmeslotmask |= (1<<(vmeslot-1));
	    }
	}
      /* Convert the vmeslotmask into the vme address list */
      for (ibit=0; ibit<32; ibit++)
	{
	  if(vmeslotmask & (1<<ibit))
	    {
	      slot = ibit+1;
#ifdef DEBUGMASK
	      printf("vmeslot = %d\n",slot);
#endif
	      fadcAddrList[ilist++] = (slot<<19);
	    }
	}
      nFADC_from_mask=ilist;
#ifdef DEBUGMASK      
      printf("VME addresses to use:\n");
      for(ilist=0; ilist<nFADC_from_mask; ilist++)
	{
	  printf("  0x%08x\n",fadcAddrList[ilist]);
	}
      printf("nFADC_from_mask = %d\n",nFADC_from_mask);
#endif
    }

  vmeOpenDefaultWindows();

  /* Setup Address and data modes for DMA transfers
   *   
   *  vmeDmaConfig(addrType, dataType, sstMode);
   *
   *  addrType = 0 (A16)    1 (A24)    2 (A32)
   *  dataType = 0 (D16)    1 (D32)    2 (BLK32) 3 (MBLK) 4 (2eVME) 5 (2eSST)
   *  sstMode  = 0 (SST160) 1 (SST267) 2 (SST320)
   */
  vmeDmaConfig(2,5,1);

  /* INIT dmaPList */
  dmaPFreeAll();
  vmeIN  = dmaPCreate("vmeIN",10*10240,100,0);
  vmeOUT = dmaPCreate("vmeOUT",0,0,0);
    
  dmaPStatsAll();

  dmaPReInitAll();

  /************************************************** 
   * TI
   **************************************************/
  /* Set the TI structure pointer */
  tiInit((21<<19),TI_READOUT_EXT_POLL,0);

  stat = tiIntConnect(TI_INT_VEC, mytiISR, 0);
  if (stat != OK) {
    printf("ERROR: tiIntConnect failed \n");
    goto CLOSE;
  } else {
    printf("INFO: Attached TI Interrupt\n");
  }

  tiSetBusySource(TI_BUSY_LOOPBACK | TI_BUSY_SWB,1);

  /* TI Setup */
  /* Set the trigger source to the Internal Pulser */
  tiSetTriggerSource(TI_TRIGGER_PULSER);

  /* Set number of events per block */
  tiSetBlockLevel(1);

  /* Enable TS Input #3 and #4 */
  /*   tiEnableTSInput(0 ); */
  tiDisableTSInput(0x3f);

  /* Load the trigger table that associates 
   *     TS#1 | TS#2 | TS#3 : trigger1
   *     TS#4 | TS#5 | TS#6 : trigger2 (playback trigger)
   */
  tiLoadTriggerTable();

  /* Set the Block Acknowledge threshold 
   *               0:  No threshold  -  Pipeline mode
   *               1:  One Acknowdge for each Block - "ROC LOCK" mode
   *           2-255:  "Buffered" mode.
   */
  tiSetBlockBufferLevel(1);

  tiSetTriggerHoldoff(1,12,0);
  tiSetTriggerHoldoff(2,12,0);

  /**************************************************
   * FADC
   **************************************************/
  if(useMask)
    NFADC = nFADC_from_mask;
  else
    NFADC = 16+2;

  fadcA32Base=0x09000000;
  /* Set the FADC structure pointer */
  int iFlag = 0;
  /* Sync Source */
  iFlag |= (1<<0); /* VXS */
  /* Trigger Source */
  iFlag |= (1<<2); // VXS Input Trigger
  /* Clock Source */
/*   iFlag |= (1<<5); // VXS Clock Source */
  iFlag |= (0<<5); // Internal Clock Source

  if(useMask)
    iFlag |= (1<<17);

  if(NFADC!=0)
    {
      vmeSetQuietFlag(1);
      faInit((unsigned int)(3<<19),(1<<19),NFADC,iFlag);
      NFADC=nfadc;
      vmeSetQuietFlag(0);

      /* Calculate the maximum number of words per block transfer */
      /*   MAX = NFADC * BLOCKLEVEL * (EvHeader + TrigTime*2 + Pulse*2*chan) 
	   + 2*32 (words for byte alignment) */
/*       MAXFADCWORDS = NFADC * BLOCKLEVEL * (1+2+32) + 2*32; */
      MAXFADCWORDS = NFADC * BLOCKLEVEL * (1+2+FADC_WINDOW_WIDTH*16) + 2*32;
	   
      printf("****************************************\n");
      printf("* Calculated MAX FADC words = %d\n",MAXFADCWORDS);
      printf("****************************************\n");
      
    }

#ifdef USETOKEN
  if(NFADC>1)
    faEnableMultiBlock(1);
#else
  faDisableMultiBlock();
#endif

  /* Extra setups */
  int islot;

  fadcSlotMask=0;
  for(islot=0;islot<NFADC;islot++) 
    {
      FA_SLOT = fadcID[islot];
      fadcSlotMask |= (1<<FA_SLOT);

      /* Set the internal DAC level */
      faSetDAC(FA_SLOT,FADC_DAC_LEVEL,0);
      faPrintDAC(FA_SLOT);
      /* Set the threshold for data readout */
      faSetThreshold(FA_SLOT,fadc_threshold,0);
	
      int ichan;
      for(ichan=0; ichan<16; ichan++)
	{
	  faSetChannelPedestal(FA_SLOT,ichan,0);
	  faSetThreshold(FA_SLOT,ichan+100,1<<ichan);
	}

      /*  Setup option 1 processing - RAW Window Data     <-- */
      /*        option 2            - RAW Pulse Data */
      /*        option 3            - Integral Pulse Data */
      /*  Setup 200 nsec latency (PL  = 50)  */
      /*  Setup  80 nsec Window  (PTW = 20) */
      /*  Setup Pulse widths of 36ns (NSB(3)+NSA(6) = 9)  */
      /*  Setup up to 1 pulse processed */
      /*  Setup for both ADC banks(0 - all channels 0-15) */
      /* Raw Window Data */
      faSetProcMode(FA_SLOT,1,fadc_window_lat,fadc_window_width,3,6,3,0);
	
      faEnableBusError(FA_SLOT);
      faSetBlockLevel(FA_SLOT,BLOCKLEVEL);
      /*     faPrintDAC(FA_SLOT); */
      /*     faPrintThreshold(FA_SLOT); */


      faStatus(FA_SLOT,1);
    }	

  /**************************************************
   *   SD SETUP
   *************************************************/
  sdInit();
  sdSetActiveVmeSlots(fadcSlotMask);
  sdStatus();

  tiTrigLinkReset();
  taskDelay(1);
  tiSyncReset();

  /* Program/Init VME Modules Here */
  for(islot=0;islot<NFADC;islot++) 
    {
      FA_SLOT = fadcID[islot];
      faSetClockSource(FA_SLOT,2);
      faSoftReset(FA_SLOT);
      faResetToken(FA_SLOT);
      faResetTriggerCount(FA_SLOT);
#ifdef USE_PLAYBACK_MODE
      faPPGEnable(FA_SLOT);
#endif
      faStatus(FA_SLOT,0);
    }

  taskDelay(1);
  tiStatus();

  for(islot=0;islot<NFADC;islot++) 
    {
      FA_SLOT = fadcID[islot];
      /*  Enable FADC */
      faSetMGTTestMode(FA_SLOT,0);
      faChanDisable(FA_SLOT,0xffff);
      faEnable(FA_SLOT,0,0);
      printf("Sync counter = %d\n",faTestGetSyncCounter(FA_SLOT));
    }

  printf("Hit enter to start triggers\n");
  getchar();

  for(islot=0;islot<NFADC;islot++)
    {
      FA_SLOT = fadcID[islot];
      printf("Sync counter = %d\n",faTestGetSyncCounter(FA_SLOT));
      faChanDisable(FA_SLOT,0x0);
      faSetMGTTestMode(FA_SLOT,1);
    }

  tiIntEnable(0);
  tiStatus();
#define SOFTTRIG
#ifdef SOFTTRIG
  tiSetRandomTrigger(1,0x7);
  /*     tiSoftTrig(0xffff,0x700,0); */
#endif

  printf("Hit any key to Disable TI and exit.\n");
  getchar();

#ifdef SOFTTRIG
  /* No more soft triggers */
  /*     tiSoftTrig(0x0,0x8888,0); */
  tiDisableRandomTrigger();
#endif

  tiIntDisable();

  tiIntDisconnect();

  for(islot=0;islot<NFADC;islot++) 
    {
      FA_SLOT = fadcID[islot];
      /* FADC Disable */
#ifdef USE_PLAYBACK_MODE
      faPPGDisable(FA_SLOT);      
#endif
      faDisable(FA_SLOT,0);
      faStatus(FA_SLOT,0);
      faReset(FA_SLOT,1); /* Dont restore the adr32 settings */
    }

  tiStatus();
  sdStatus();

 CLOSE:

  vmeCloseDefaultWindows();

  exit(0);
}

