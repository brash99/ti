/*************************************************************************
 *
 *  tiConfig_slave_list.c -
 *
 *          Library of routines for readout and buffering of events
 *          using a JLAB Trigger Interface V3 (TI) with a Linux VME
 *          controller in CODA 3.0.
 *
 *          This for a TI in Slave Mode configured with file:
 *             rol/ti-slave.ini
 *
 */

/* Event Buffer definitions */
#define MAX_EVENT_POOL     10
#define MAX_EVENT_LENGTH   1024*64      /* Size in Bytes */

/* Define TI Type (TI_MASTER or TI_SLAVE) */
#define TI_SLAVE
/* TS Fiber Link trigger source (from TI Master, TD, or TS), POLL for available data */
#define TI_READOUT TI_READOUT_TS_POLL
/* TI VME address, or 0 for Auto Initialize (search for TI by slot) */
#define TI_ADDR  0

/* Measured longest fiber length in system */
#define FIBER_LATENCY_OFFSET 0x10

#include "dmaBankTools.h"   /* Macros for handling CODA banks */
#include "tiprimary_list.c" /* Source required for CODA readout lists using the TI */
#include "tiConfig.h"       /* TI configuration using a config file */
#include "sdLib.h"

/****************************************
 *  DOWNLOAD
 ****************************************/
void
rocDownload()
{
  int stat;

  /* Setup Address and data modes for DMA transfers
   *
   *  vmeDmaConfig(addrType, dataType, sstMode);
   *
   *  addrType = 0 (A16)    1 (A24)    2 (A32)
   *  dataType = 0 (D16)    1 (D32)    2 (BLK32) 3 (MBLK) 4 (2eVME) 5 (2eSST)
   *  sstMode  = 0 (SST160) 1 (SST267) 2 (SST320)
   */

  vmeDmaConfig(2,5,1);

  /*****************
   *   TI SETUP
   *****************/

  /* Load up the config file */
  char tiConfigFilename[256];

  strcpy(tiConfigFilename, "/daqfs/daq_setups/faV3/ti/rol/ti-slave.ini");

  if(tiConfig(tiConfigFilename) != OK)
    {
      daLogMsg("ERROR", "Unable to load TI Config file %s\n", tiConfigFilename);
    }
  else
    {
      daLogMsg("INFO", "Loaded config file %s\n", tiConfigFilename);
    }
  tiConfigFree();

  /* Init the SD library so we can get status info */
  stat = sdInit(0);
  if(stat==0)
    {
      tiSetBusySource(TI_BUSY_SWB, 0);
      sdSetActiveVmeSlots(0);
      sdStatus(0);
    }

  tiStatus(1);


  printf("rocDownload: User Download Executed\n");

}

/****************************************
 *  PRESTART
 ****************************************/
void
rocPrestart()
{
  unsigned short iflag;
  int stat;
  int islot;

  tiStatus(1);


  UEOPEN(500,BT_BANK,0);

  CBOPEN(1,BT_UI4,0);
  *rol->dabufp++ = 0x11112222;
  *rol->dabufp++ = 0x55556666;
  *rol->dabufp++ = 0xaabbccdd;
  CBCLOSE;

  UECLOSE;


  printf("rocPrestart: User Prestart Executed\n");

}

/****************************************
 *  GO
 ****************************************/
void
rocGo()
{
  int islot;

  /* Print out the Run Number and Run Type (config id) */
  printf("rocGo: Activating Run Number %d, Config id = %d\n",rol->runNumber,rol->runType);

  /* Get the broadcasted Block and Buffer Levels from TS or TI Master */
  blockLevel = tiGetCurrentBlockLevel();
  bufferLevel = tiGetBroadcastBlockBufferLevel();
  printf("rocGo: Block Level set to %d  Buffer Level set to %d\n",blockLevel,bufferLevel);

  /* Enable/Set Block Level on modules, if needed, here */

}

/****************************************
 *  END
 ****************************************/
void
rocEnd()
{
  int islot;

  tiStatus(1);

  printf("rocEnd: Ended after %d blocks\n",tiGetIntCount());

}

/****************************************
 *  TRIGGER
 ****************************************/
void
rocTrigger(int evntno)
{
  int ii, islot;
  int stat, dCnt, len=0, idata;
  unsigned int val;
  unsigned int *start;

  /* Set TI output 1 high for diagnostics */
  tiSetOutputPort(1,0,0,0);

  /* Check if this is a Sync Event */
  stat = tiGetSyncEventFlag();
  if(stat) {
    printf("rocTrigger: Got Sync Event!! Block # = %d\n",evntno);
  }

  /* Readout the trigger block from the TI
     Trigger Block MUST be reaodut first */
  dCnt = tiReadTriggerBlock(dma_dabufp);
  if(dCnt<=0)
    {
      printf("No data or error.  dCnt = %d\n",dCnt);
    }
  else
    { /* TI Data is already in a bank structure.  Bump the pointer */
      dma_dabufp += dCnt;
    }


  /* EXAMPLE: How to open a bank (type=5) and add data words by hand */
  BANKOPEN(5,BT_UI4,blockLevel);
  *dma_dabufp++ = tiGetIntCount();
  *dma_dabufp++ = 0xdead;
  *dma_dabufp++ = 0xcebaf111;
  *dma_dabufp++ = 0xcebaf222;

  BANKCLOSE;


  /* Set TI output 0 low */
  tiSetOutputPort(0,0,0,0);

}

void
rocCleanup()
{
  int islot=0;

  printf("%s: Reset all Modules\n",__FUNCTION__);

}

/*
  Local Variables:
  compile-command: "make -k tiConfig_slave_list.so "
  End:
*/
