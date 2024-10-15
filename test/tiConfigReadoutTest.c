/*
 * File:
 *    tiConfigReadoutTest.c
 *
 * Description:
 *    Test TI readout with the TI library
 *
 *
 */


#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "jvme.h"
#include "tiLib.h"
#include "tiConfig.h"

DMA_MEM_ID vmeIN,vmeOUT;
extern DMANODE *the_event;
extern unsigned int *dma_dabufp;

extern int tiA32Base;
extern int tiNeedAck;

/* Interrupt Service routine */
void
mytiISR(int arg)
{
  int dCnt;
  int printout = 1;
  int dataCheck=0;
  DMANODE *outEvent;

  unsigned int tiIntCount = tiGetIntCount();

  GETEVENT(vmeIN,tiIntCount);

  /* dCnt = tiReadBlock(dma_dabufp,5*BLOCKLEVEL+3+4,1); */
  dCnt = tiReadTriggerBlock(dma_dabufp);

  if(dCnt<=0)
    {
      printf("No data or error.  dCnt = %d\n",dCnt);
      dataCheck=ERROR;
    }
  else
    {
      dma_dabufp += dCnt;
    }

  PUTEVENT(vmeOUT);

  unsigned long length = (((unsigned long)(dma_dabufp) - (unsigned long)(&the_event->length))) - 4;
  unsigned long size = the_event->part->size - sizeof(DMANODE);

  if(length>size)
    {
      printf("rocLib: ERROR: Event length > Buffer size (%ld > %ld).  Event %ld\n",
	     length, size, the_event->nevent);
      dataCheck=ERROR;
    }

  outEvent = dmaPGetItem(vmeOUT);

#ifdef SHOWDATA
  if(tiIntCount%printout==0)
    {
      printf("Received %d triggers...\n",
	     tiIntCount);

      int len, idata;
      len = outEvent->length;
      for(idata=0;idata<len;idata++)
	{
	  if((idata%5)==0) printf("\n\t");
	  printf("  0x%08x ",(unsigned int)LSWAP(outEvent->data[idata]));
	}
      printf("\n\n");
    }
#else
  if(tiIntCount%printout==0)
    {
      printf("intCount = %d\r",tiIntCount );
      fflush(stdout);
    }
#endif // SHOWDATA

  dmaPFreeItem(outEvent);

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

  printf("\nJLAB TI Readout Test\n");
  printf("----------------------------\n");

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


  tiConfigInitGlobals();



  tiA32Base=0x09000000;
  tiSetFiberLatencyOffset_preInit(0x20);
  tiInit(0,TI_READOUT_EXT_POLL,TI_INIT_SKIP_FIRMWARE_CHECK);
  tiCheckAddresses();

  if(argc == 2)
    tiConfig(argv[1]);
  tiConfigFree();

  char mySN[20];
  printf("0x%08x\n",tiGetSerialNumber((char **)&mySN));
  printf("mySN = %s\n",mySN);


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
#define SOFTTRIG
#ifdef SOFTTRIG
  tiSetRandomTrigger(1,0x7);
#endif

  printf("Hit any key to Disable TID and exit.\n");
  getchar();

#ifdef SOFTTRIG
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
  compile-command: "make -k tiConfigReadoutTest "
  End:
*/
