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
/* #include "remexLib.h" */

DMA_MEM_ID vmeIN,vmeOUT;
extern DMANODE *the_event;
extern unsigned int *dma_dabufp;

#define DO_READOUT

/* Interrupt Service routine */
void
mytiISR(int arg)
{
  volatile unsigned short reg;
  int dCnt, len=0,idata;
  DMANODE *outEvent;
  int tibready=0, timeout=0;

  unsigned int tiIntCount = tiGetIntCount();

#ifdef DO_READOUT
  GETEVENT(vmeIN,tiIntCount);

#ifdef DOINT
  tibready = tiBReady();
  if(tibready==ERROR)
    {
      printf("%s: ERROR: tiIntPoll returned ERROR.\n",__FUNCTION__);
      return;
    }

  if(tibready==0 && timeout<100)
    {
      printf("NOT READY!\n");
      tibready=tiBReady();
      timeout++;
    }

  if(timeout>=100)
    {
      printf("TIMEOUT!\n");
      return;
    }
#endif

  *dma_dabufp++;

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
  PUTEVENT(vmeOUT);
  
  outEvent = dmaPGetItem(vmeOUT);
#define READOUT
#ifdef READOUT
  if(tiIntCount%1==0)
    {
      printf("Received %d triggers...\n",
	     tiIntCount);

      len = outEvent->length;
      
      for(idata=0;idata<len;idata++)
	{
	  if((idata%5)==0) printf("\n\t");
	  printf("  0x%08x ",(unsigned int)LSWAP(outEvent->data[idata]));
	}
      printf("\n\n");
    }
#endif
  dmaPFreeItem(outEvent);
#else /* DO_READOUT */
  /*   tiResetBlockReadout(); */

#endif /* DO_READOUT */
  if(tiIntCount%1==0)
    printf("intCount = %d\n",tiIntCount );
/*     sleep(1); */
}


int 
main(int argc, char *argv[]) {

  int stat;
  int partchoice=1;

  printf("\nJLAB TI Tests\n");
  printf("----------------------------\n");

  if(argc>1)
    {
      partchoice = atoi(argv[1]);
      if((partchoice<1) || (partchoice>4))
	{
	  printf(" ERROR: Invalid partition choice (%d)\n",partchoice);
	  exit(0);
	}
      printf("Partition Choice = %d\n",partchoice);
      exit(0);
    }

/*   remexSetCmsgServer("dafarm28"); */
/*   remexInit(NULL,1); */

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
  vmeIN  = dmaPCreate("vmeIN",1024,500,0);
  vmeOUT = dmaPCreate("vmeOUT",0,0,0);
    
  dmaPStatsAll();

  dmaPReInitAll();

  /*     gefVmeSetDebugFlags(vmeHdl,0x0); */
  /* Set the TI structure pointer */
  /*     tiInit((2<<19),TI_READOUT_EXT_POLL,0); */
  tiInit(0,TI_READOUT_TS_POLL,0);
  tiCheckAddresses();

  char mySN[20];
  printf("0x%08x\n",tiGetSerialNumber((char **)&mySN));
  printf("mySN = %s\n",mySN);

#ifndef DO_READOUT
  tiDisableDataReadout();
  tiDisableA32();
#endif

  tiLoadTriggerTable(0);
    
  tiSetTriggerHoldoff(1,4,0);
  tiSetTriggerHoldoff(2,4,0);

  tiSetPrescale(0);
  tiSetBlockLevel(1);

  stat = tiIntConnect(TI_INT_VEC, mytiISR, 0);
  if (stat != OK) 
    {
      printf("ERROR: tiIntConnect failed \n");
      goto CLOSE;
    } 
  else 
    {
      printf("INFO: Attached TI Interrupt\n");
    }

  switch(partchoice)
    {
    case 1:
      tiSetTriggerSource(TI_TRIGGER_PART_1);
      break;

    case 2:
      tiSetTriggerSource(TI_TRIGGER_PART_2);
      break;

    case 3:
      tiSetTriggerSource(TI_TRIGGER_PART_3);
      break;

    case 4:
    default:
      tiSetTriggerSource(TI_TRIGGER_PART_4);
      break;

    }
  tiEnableTSInput(0x1);

  /*     tiSetFPInput(0x0); */
  /*     tiSetGenInput(0xffff); */
  /*     tiSetGTPInput(0x0); */

  tiSetBusySource(TI_BUSY_LOOPBACK,1);

  tiSetBlockBufferLevel(1);

  tiSetFiberDelay(1,2);
  tiSetSyncDelayWidth(1,0x3f,1);
    
  printf("Hit enter to reset stuff\n");
  getchar();

  tiClockReset();
  taskDelay(1);
  tiTrigLinkReset();
  taskDelay(1);
  tiEnableVXSSignals();
  taskDelay(1);
  tiSyncReset();

  taskDelay(1);
    
  tiStatus();

  printf("Hit enter to start triggers\n");
  getchar();

  tiIntEnable(0);
  tiStatus();
#define SOFTTRIG
#ifdef SOFTTRIG
  tiSetRandomTrigger(1,0x7);
  taskDelay(10);
  tiSoftTrig(1,0x1,0x700,0);
#endif

  printf("Hit any key to Disable TID and exit.\n");
  getchar();
  tiStatus();

#ifdef SOFTTRIG
  /* No more soft triggers */
  /*     tidSoftTrig(0x0,0x8888,0); */
  tiSoftTrig(1,0,0x700,0);
  tiDisableRandomTrigger();
#endif

  tiIntDisable();

  tiIntDisconnect();


 CLOSE:

  vmeCloseDefaultWindows();

  exit(0);
}

