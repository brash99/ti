/*************************************************************************
 *
 *  tiConfig_master_list.c -
 *
 *          Library of routines for readout and buffering of events
 *          using a JLAB Trigger Interface V3 (TI) with a Linux VME
 *          controller in CODA 3.0.
 *
 *          This for a TI in Master Mode configured with file:
 *             rol/ti-master.ini
 */

/* Event Buffer definitions */
#define MAX_EVENT_POOL     10
#define MAX_EVENT_LENGTH   1024*64      /* Size in Bytes */

/* Define TI Type (TI_MASTER or TI_SLAVE) */
#define TI_MASTER
/* EXTernal trigger source (e.g. front panel ECL input), POLL for available data */
#define TI_READOUT TI_READOUT_EXT_POLL
/* TI VME address, or 0 for Auto Initialize (search for TI by slot) */
#define TI_ADDR  0

/* Measured longest fiber length in system */
#define FIBER_LATENCY_OFFSET 0x10

#include "dmaBankTools.h"   /* Macros for handling CODA banks */
#include "tiprimary_list.c" /* Source required for CODA readout lists using the TI */
#include "tiConfig.h"       /* TI configuration using a config file */
#include "sdLib.h"

/*
  Global to configure the trigger source
      0 : tsinputs
      1 : TI pulser (fixed/random parameters in config file)

  Set with rocSetTriggerSource(int source);
*/
int rocTriggerSource = 1; // initial value is overridden by config file
void rocSetTriggerSource(int source); // routine prototype

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

  strcpy(tiConfigFilename, "/daqfs/daq_setups/faV3/ti/rol/ti-master.ini");

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

  /* Set blocklevel from that set in config file */
  blockLevel = tiGetCurrentBlockLevel();

  tiStatus(1);

  printf("rocDownload: User Download Executed\n");

}

/****************************************
 *  PRESTART
 ****************************************/
void
rocPrestart()
{

  /* Set number of events per block (broadcasted to all connected TI Slaves)*/
  tiSetBlockLevel(blockLevel);
  printf("rocPrestart: Block Level set to %d\n",blockLevel);

  tiStatus(1);

  printf("rocPrestart: User Prestart Executed\n");

}

/****************************************
 *  GO
 ****************************************/
void
rocGo()
{
  int islot;

  /* Get the current Block Level */
  blockLevel = tiGetCurrentBlockLevel();
  printf("rocGo: Block Level set to %d\n",blockLevel);

  /* Enable/Set Block Level on modules, if needed, here */


#ifdef TI_MASTER
  if(rocTriggerSource != 0)
    {
      printf("************************************************************\n");
      daLogMsg("INFO","TI Configured for Internal Pulser Triggers");
      printf("************************************************************\n");

      tiConfigEnablePulser();

    }
#endif
}

/****************************************
 *  END
 ****************************************/
void
rocEnd()
{

#ifdef TI_MASTER
  if(rocTriggerSource != 0)
    {
      tiConfigDisablePulser();
    }
#endif

  tiStatus(1);

  printf("rocEnd: Ended after %d blocks\n",tiGetIntCount());

}

/****************************************
 *  TRIGGER
 ****************************************/
void
rocTrigger(int arg)
{
  int ii, islot;
  int stat, dCnt, len=0, idata;
  unsigned int val;
  unsigned int *start;

  /* Set TI output 1 high for diagnostics */
  tiSetOutputPort(1,0,0,0);

  /* Readout the trigger block from the TI
     Trigger Block MUST be reaodut first */
  dCnt = tiReadTriggerBlock(dma_dabufp);

  if(dCnt<=0)
    {
      printf("No TI Trigger data or error.  dCnt = %d\n",dCnt);
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

void
rocSetTriggerSource(int source)
{
#ifdef TI_MASTER
  if(TIPRIMARYflag == 1)
    {
      printf("%s: ERROR: Trigger Source already enabled.  Ignoring change to %d.\n",
	     __func__, source);
    }
  else
    {
      rocTriggerSource = source;

      if(rocTriggerSource == 0)
	{
	  tiSetTriggerSource(TI_TRIGGER_TSINPUTS); /* TS Inputs enabled */
	}
      else
	{
	  tiSetTriggerSource(TI_TRIGGER_PULSER); /* Internal Pulser */
	}

      daLogMsg("INFO","Setting trigger source (%d)", rocTriggerSource);
    }
#else
  printf("%s: ERROR: TI is not Master  Ignoring change to %d.\n",
	 __func__, source);
#endif
}

/*
  Local Variables:
  compile-command: "make -k tiConfig_master_list.so "
  End:
*/
