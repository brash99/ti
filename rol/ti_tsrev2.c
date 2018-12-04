/*************************************************************************
 *
 *  ti_tsrev2.c - Library of routines for readout and buffering of
 *                events using a JLAB Pipeline Trigger Interface with
 *                the TS rev2 34-pin branch connection and a Linux VME
 *                controller.
 *
 */

/* Event Buffer definitions */
#define MAX_EVENT_POOL     10
#define MAX_EVENT_LENGTH   1024*60	/* Size in Bytes */

/* Define Interrupt source and address */
#define TI_MASTER
#define TI_READOUT TI_READOUT_TSREV2_POLL	/* Poll for available data, tsrev2 for evtype */
#define TI_ADDR    0	                        /* 0 = probe for TI */
#define FIBER_LATENCY_OFFSET 0                  /* Not needed */

#include "dmaBankTools.h"
#include "tiprimary_list.c"	/* source required for CODA */

/* Default block level MUST be 1, for use with TSRev2 */
unsigned int BLOCKLEVEL = 1;
#define BUFFERLEVEL 1

/* function prototype */
void rocTrigger(int arg);

/****************************************
 *  DOWNLOAD
 ****************************************/
void
rocDownload()
{

  /* Setup Address and data modes for DMA transfers
   *
   *  vmeDmaConfig(addrType, dataType, sstMode);
   *
   *  addrType = 0 (A16)    1 (A24)    2 (A32)
   *  dataType = 0 (D16)    1 (D32)    2 (BLK32) 3 (MBLK) 4 (2eVME) 5 (2eSST)
   *  sstMode  = 0 (SST160) 1 (SST267) 2 (SST320)
   */
  vmeDmaConfig(2, 5, 1);

  /*****************
   *   TI SETUP
   *****************/
  /* Set crate ID */
  tiSetCrateID(0x01);		/* e.g. ROC 1 */

  /* Use TSRev2 34pin-connector for event type and readout acknowledge
     Use Input TRG for Accepted trigger (e.g. L1A) from TSRev2
  */
  tiSetTriggerSource(TI_TRIGGER_TSREV2);

  /* Set number of events per block */
  if(BLOCKLEVEL != 1)
    {
      printf("%s: ERROR: Invalid BLOCKLEVEL (%d) for TI_TRIGGER_TSREV2.\n Setting to 1.\n",
	     __func__, BLOCKLEVEL);
      BLOCKLEVEL = 1;
    }
  tiSetBlockLevel(BLOCKLEVEL);

  /* Maximum blocks in the system that need read out */
  tiSetBlockBufferLevel(BUFFERLEVEL);

  tiStatus(0);


  printf("%s: User Download Executed\n",
	 __func__);

}

/****************************************
 *  PRESTART
 ****************************************/
void
rocPrestart()
{

  /* Set number of events per block (broadcasted to all connected TI Slaves) */
  tiSetBlockLevel(BLOCKLEVEL);
  printf("%s: Block Level set to %d\n",
	 __func__, BLOCKLEVEL);

  tiStatus(0);

  printf("%s: User Prestart Executed\n",
	 __func__);

}

/****************************************
 *  GO
 ****************************************/
void
rocGo()
{
  /* Enable modules, if needed, here */

  /* Get the current block level */
  /* Use this info to change block level is all modules */
  BLOCKLEVEL = tiGetCurrentBlockLevel();
  printf("%s: Current Block Level = %d\n", __func__, BLOCKLEVEL);

  tiStatus(0);
}

/****************************************
 *  END
 ****************************************/
void
rocEnd()
{
  tiStatus(0);

  printf("%s: Ended after %d blocks\n",
	 __func__, tiGetIntCount());

}

/****************************************
 *  TRIGGER
 ****************************************/
void
rocTrigger(int arg)
{
  int dCnt;
  int ev_type = 0;

  tiSetOutputPort(1, 0, 0, 0);

  /* Open an event of event_type = 1 (** redefined later **).
     Data contained in banks (BT_BANK) */
  EVENTOPEN(1, BT_BANK);

  /* Open a bank of type (for example) 4
     Each data word is a 4-byte unsigned integer */
  BANKOPEN(4, BT_UI4, 0);

  vmeDmaConfig(2, 5, 1);
  dCnt = tiReadBlock(dma_dabufp, 8 + (5 * BLOCKLEVEL), 1);
  if (dCnt <= 0)
    {
      printf("No data or error.  dCnt = %d\n", dCnt);
    }
  else
    {
      ev_type = tiDecodeTriggerType(dma_dabufp, dCnt, 1);

      if ((ev_type <= 0) || (ev_type > 0xF))
	{
	  printf("%s: ERROR: Invalid event type (%d)\n",
		 __func__, ev_type);
	  ev_type = 1;
	}

      /* ** Redefine event type in event buffer ** */
      the_event->type = ev_type;

      dma_dabufp += dCnt;
    }

  BANKCLOSE;

  EVENTCLOSE;

  if(tiGetSyncEventFlag() == 1)
    {
      /* Flush out TI data, if it's there (out of sync) */
      int davail = tiBReady();
      if(davail > 0)
	{
	  printf("%s: ERROR: TI Data available (%d) after readout in SYNC event \n",
		 __func__, davail);

	  while(tiBReady())
	    {
	      vmeDmaFlush(tiGetAdr32());
	    }
	}

      /* Flush out other modules too, if necessary */

    }

  tiSetOutputPort(0, 0, 0, 0);
}

void
rocCleanup()
{

  printf("%s: Reset/cleanup modules and libraries\n", __func__);

}


/*
  Local Variables:
  compile-command: "make -B ti_tsrev2.so"
  End:
 */
