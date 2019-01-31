/*************************************************************************
 *
 *  vme_list.c - Library of routines for readout and buffering of
 *                events using a JLAB Trigger Interface V3 (TI) with
 *                a Linux VME controller.
 *
 */

/* Event Buffer definitions */
#define MAX_EVENT_POOL     10
#define MAX_EVENT_LENGTH   1024*60	/* Size in Bytes */

/* Define Interrupt source and address */
#define TI_MASTER
#define TI_READOUT TI_READOUT_EXT_POLL	/* Poll for available data, external triggers */
#define TI_ADDR    (21<<19)	/* GEO slot 21 */

#define FIBER_LATENCY_OFFSET 0x4A	/* measured longest fiber length */

#include "dmaBankTools.h"
#include "tiprimary_list.c"	/* source required for CODA */

/* Default block level */
unsigned int BLOCKLEVEL = 1;
#define BUFFERLEVEL 3

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

#ifdef TI_MASTER
  tiSetTriggerSource(TI_TRIGGER_TSINPUTS);

  /* Set needed TS input bits */
  tiEnableTSInput(TI_TSINPUT_1);

  /* Load the trigger table that associates
     pins 21/22 | 23/24 | 25/26 : trig1 (physics trigger)
     pins 29/30 | 31/32 | 33/34 : trig2 (playback/simulation trigger)
   */
  tiLoadTriggerTable(0);

  tiSetTriggerHoldoff(1, 10, 0);
  tiSetTriggerHoldoff(2, 10, 0);

  /* Set number of events per block */
  tiSetBlockLevel(BLOCKLEVEL);

  /* Maximum blocks in the system that need read out */
  tiSetBlockBufferLevel(BUFFERLEVEL);
#endif

  tiStatus(0);


  printf("rocDownload: User Download Executed\n");

}

/****************************************
 *  PRESTART
 ****************************************/
void
rocPrestart()
{

#ifdef TI_MASTER
  /* Set number of events per block (broadcasted to all connected TI Slaves) */
  tiSetBlockLevel(BLOCKLEVEL);
  printf("rocPrestart: Block Level set to %d\n", BLOCKLEVEL);

  /* Reset Active ROC Masks */
  tiTriggerReadyReset();
#endif

  tiStatus(0);

  printf("rocPrestart: User Prestart Executed\n");

}

/****************************************
 *  GO
 ****************************************/
void
rocGo()
{
  unsigned int tmask = 0;
  /* Enable modules, if needed, here */

  /* Get the current block level */
  /* Use this info to change block level is all modules */
  BLOCKLEVEL = tiGetCurrentBlockLevel();
  printf("%s: Current Block Level = %d\n", __func__, BLOCKLEVEL);

  /* Enable Slave Ports that have indicated they are active */
  tiResetSlaveConfig();
  tmask = tiGetTrigSrcEnabledFiberMask();
  printf("%s: TI Source Enable Mask = 0x%x\n", __func__, tmask);
  if (tmask != 0)
    tiAddSlaveMask(tmask);

  tiStatus(0);
}

/****************************************
 *  END
 ****************************************/
void
rocEnd()
{
  tiStatus(0);

  printf("rocEnd: Ended after %d blocks\n", tiGetIntCount());

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

  /* Open an event of event_type = 1 (replaced later).
     Data contained in banks (BT_BANK) */
  EVENTOPEN(1, BT_BANK);

  /* EXAMPLE:
     Open a bank of tag 5 (tag range: 1-65279)
     Each data word is a 4-byte unsigned integer (BT_UI4)
  */
  BANKOPEN(5, BT_UI4, 0);
  *dma_dabufp++ = LSWAP(tiGetIntCount());
  *dma_dabufp++ = LSWAP(0xdead);
  *dma_dabufp++ = LSWAP(0xcebaf111);
  BANKCLOSE;


  /* Open a bank of tag (for example) 4
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
      if (ev_type <= 0)
	{
	  /* Could not find trigger type */
	  ev_type = 1;
	}

      /* CODA 2.x only allows for 4 bits of trigger type */
      ev_type &= 0xF;

      the_event->type = ev_type;

      dma_dabufp += dCnt;
    }

  BANKCLOSE;

  EVENTCLOSE;

  tiSetOutputPort(0, 0, 0, 0);

}

extern int tsLiveCalc;
extern FUNCPTR tsLiveFunc;
/*
   tiLive() wrapper allows the Live Time display in rcGUI to work
*/
int
tsLive(int sflag)
{
  unsigned int retval = 0;

  vmeBusLock();
  if(tsLiveFunc != NULL)
    {
      retval = tiLive(sflag);
    }
  vmeBusUnlock();

  return retval;
}

void
rocCleanup()
{

  printf("%s: Reset/cleanup modules and libraries\n", __func__);


  /* Disable tiLive() wrapper function */
  vmeBusLock();
  tsLiveCalc = 0;
  tsLiveFunc = NULL;

  /* Disable TI library */
  tiUnload(1);
  vmeBusUnlock();

}
