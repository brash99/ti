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

extern int tiA32Base;
extern int tiNeedAck;

#define BLOCKLEVEL 1
#define SOFTTRIG
#define DO_READOUT

/* Interrupt Service routine */
void
mytiISR(int arg)
{
  volatile unsigned short reg;
  int dCnt, len=0,idata;
  DMANODE *outEvent;
  int tibready=0, timeout=0;
  int printout = 1;
  int dataCheck=0;

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
  /* *dma_dabufp++; */

  dCnt = tiReadBlock(dma_dabufp,5*BLOCKLEVEL+3+4,1);
  /* dCnt = tiReadTriggerBlock(dma_dabufp); */

  if(dCnt<=0)
    {
      printf("No data or error.  dCnt = %d\n",dCnt);
      dataCheck=ERROR;
    }
  else
    {
      /* dataCheck = tiCheckTriggerBlock(dma_dabufp); */
      dma_dabufp += dCnt;
    }

  PUTEVENT(vmeOUT);

#ifdef CHECKEVENT
  int length = (((int)(dma_dabufp) - (int)(&the_event->length))) - 4;
  int size = the_event->part->size - sizeof(DMANODE);

  if(length>size)
    {
      printf("rocLib: ERROR: Event length > Buffer size (%d > %d).  Event %d\n",
	     length,size,the_event->nevent);
      dataCheck=ERROR;
    }
#endif // CHECKEVENT

  /* if(dmaPEmpty(vmeIN)) */
    {
      /* while(dmaPNodeCount(vmeOUT) > 0) */
	{
	  outEvent = dmaPGetItem(vmeOUT);
/* #define READOUT */
#ifdef READOUT
	  if(tiIntCount%printout==0)
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
#endif // READOUT
	  // Check if event type is 0
	  int evType = ((LSWAP(outEvent->data[2]) & 0xFF000000) >> 24);
	  if((evType == 0) || (evType == 0x40) || (evType == 0x80))
	    {
	      printf("Event Type = 0!!!!\n");
	      len = outEvent->length;

	      for(idata=0;idata<len;idata++)
		{
		  if((idata%5)==0) printf("\n\t");
		  printf("  0x%08x ",(unsigned int)LSWAP(outEvent->data[idata]));
		}
	      printf("\n\n");
	      /* dataCheck=ERROR; */
	    }
	  dmaPFreeItem(outEvent);
#else /* DO_READOUT */
	  /*   tiResetBlockReadout(); */

#endif /* DO_READOUT */
	}
    }

  if(tiIntCount%printout==0)
    {
      printf("intCount = %d\r",tiIntCount );
      fflush(stdout);
    }
  /*     sleep(1); */

  /*   static int bl = BLOCKLEVEL; */
  if(tiGetSyncEventFlag())
    {
      printf("SYNC EVENT\n");

      /* Check for data available */
      int davail = tiBReady();
      if(davail > 0)
	{
	  printf("%s: ERROR: Data available (%d) after readout in SYNC event \n",
		 __func__, davail);
	  dataCheck = ERROR;

	  printf("A32 = 0x%08x\n",
		 tiGetAdr32());
	  printf("tiBReady() = %d  ... Call vmeDmaFlush\n",
		 tiBReady());
	  vmeDmaFlush(tiGetAdr32());
	  printf("tiBReady() = %d\n",
		 tiBReady());

	}
    }

  if(dataCheck!=OK)
    {
      tiSetBlockLimit(1);
      tiNeedAck = 1;
    }
  else
    tiNeedAck = 0;

}


int
main(int argc, char *argv[]) {

  int stat;

  printf("\nJLAB TI Tests\n");
  printf("----------------------------\n");

  /* remexSetCmsgServer("dafarm28"); */
  /* remexInit(NULL,1); */

  printf("Size of DMANODE    = %d (0x%x)\n",sizeof(DMANODE),sizeof(DMANODE));
  printf("Size of DMA_MEM_ID = %d (0x%x)\n",sizeof(DMA_MEM_ID),sizeof(DMA_MEM_ID));

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
  vmeIN  = dmaPCreate("vmeIN",1024,4,0);
  vmeOUT = dmaPCreate("vmeOUT",0,0,0);

  dmaPStatsAll();

  dmaPReInitAll();

  /*     gefVmeSetDebugFlags(vmeHdl,0x0); */
  /* Set the TI structure pointer */
  /*     tiInit((2<<19),TI_READOUT_EXT_POLL,0); */
  tiA32Base=0x09000000;
  tiSetFiberLatencyOffset_preInit(0x20);
  tiInit(0,TI_READOUT_EXT_POLL,TI_INIT_SKIP_FIRMWARE_CHECK);
  tiCheckAddresses();

  tiSetEvTypeScalers(1);



  /* tiDefinePulserEventType(0xAA,0xCD); */

  tiSetSyncEventInterval(0);

  tiSetEventFormat(3);

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
  tiSetBlockLevel(BLOCKLEVEL);
  /* tiSetTriggerWindow(8); */
  /* tiSetTriggerLatchOnLevel(1); */

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

#ifdef SOFTTRIG
  tiSetTriggerSource(TI_TRIGGER_PULSER);
#else
  tiSetTriggerSource(TI_TRIGGER_TSINPUTS);
  tiEnableTSInput(0x3f);
#endif


  /*     tiSetFPInput(0x0); */
  /*     tiSetGenInput(0xffff); */
  /*     tiSetGTPInput(0x0); */

  tiSetFPInputReadout(1);

  tiSetBusySource(TI_BUSY_LOOPBACK,1);

  tiSetBlockBufferLevel(1);

/*   tiSetFiberDelay(1,2); */
/*   tiSetSyncDelayWidth(1,0x3f,1); */

  tiSetBlockLimit(0);
  tiSetScalerMode(1, 1);


  /* printf("Hit enter to reset stuff\n"); */
  /* getchar(); */

  tiClockReset();
  taskDelay(1);
  tiTrigLinkReset();
  taskDelay(1);
  tiEnableVXSSignals();

  tiSyncReset(1);

  taskDelay(1);

  tiStatus(1);

  printf("Hit enter to start triggers\n");
  getchar();

  tiIntEnable(0);
  tiStatus(1);
#ifdef SOFTTRIG
  tiSetRandomTrigger(1,0x7);
/*   taskDelay(10); */
/*   tiSoftTrig(1,0x1000,0x700,0); */
#endif

  printf("Hit any key to Disable TID and exit.\n");
  getchar();

#ifdef SOFTTRIG
  /* No more soft triggers */
  /*     tidSoftTrig(0x0,0x8888,0); */
  tiSoftTrig(1,0,0x700,0);
  tiDisableRandomTrigger();
#endif

  tiIntDisable();

  tiIntDisconnect();

  tiStatus(1);
  tiPrintEvTypeScalers();

 CLOSE:

  dmaPFreeAll();
  vmeCloseDefaultWindows();

  exit(0);
}

/*
  Local Variables:
  compile-command: "make -k -B tiLibTest"
  End:
 */
