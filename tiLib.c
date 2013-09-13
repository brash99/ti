/*----------------------------------------------------------------------------*
 *  Copyright (c) 2012        Southeastern Universities Research Association, *
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
 *     Primitive trigger control for VME CPUs using the TJNAF Trigger
 *     Supervisor (TI) card
 *
 * SVN: $Rev$
 *
 *----------------------------------------------------------------------------*/

#define _GNU_SOURCE

#define DEVEL

#ifdef VXWORKS
#include <vxWorks.h>
#include <sysLib.h>
#include <logLib.h>
#include <taskLib.h>
#include <intLib.h>
#include <iv.h>
#include <semLib.h>
#include <vxLib.h>
#include "vxCompat.h"
#else 
#include <sys/prctl.h>
#include <unistd.h>
#include "jvme.h"
#endif
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include "tiLib.h"

/* Mutex to guard TI read/writes */
pthread_mutex_t   tiMutex = PTHREAD_MUTEX_INITIALIZER;
#define TILOCK     if(pthread_mutex_lock(&tiMutex)<0) perror("pthread_mutex_lock");
#define TIUNLOCK   if(pthread_mutex_unlock(&tiMutex)<0) perror("pthread_mutex_unlock");

/* Global Variables */
volatile struct TI_A24RegStruct  *TIp=NULL;    /* pointer to TI memory map */
volatile        unsigned int     *TIpd=NULL;  /* pointer to TI data FIFO */
int tiA24Offset=0;                            /* Difference in CPU A24 Base and VME A24 Base */
int tiA32Base  =0x08000000;                   /* Minimum VME A32 Address for use by TI */
int tiA32Offset=0;                            /* Difference in CPU A32 Base and VME A32 Base */
int tiMaster=1;                               /* Whether or not this TI is the Master */
int tiCrateID=0x59;                           /* Crate ID */
int tiBlockLevel=0;                           /* Block level for TI */
unsigned int        tiIntCount    = 0;
unsigned int        tiAckCount    = 0;
unsigned int        tiDaqCount    = 0;       /* Block count from previous update (in daqStatus) */
unsigned int        tiReadoutMode = 0;
unsigned int        tiTriggerSource = 0;     /* Set with tiSetTriggerSource(...) */
unsigned int        tiSlaveMask   = 0;       /* TI Slaves (mask) to be used with TI Master */
int                 tiDoAck       = 0;
int                 tiNeedAck     = 0;
static BOOL         tiIntRunning  = FALSE;   /* running flag */
static VOIDFUNCPTR  tiIntRoutine  = NULL;    /* user intererrupt service routine */
static int          tiIntArg      = 0;       /* arg to user routine */
static unsigned int tiIntLevel    = TI_INT_LEVEL;       /* VME Interrupt level */
static unsigned int tiIntVec      = TI_INT_VEC;  /* default interrupt vector */
static VOIDFUNCPTR  tiAckRoutine  = NULL;    /* user trigger acknowledge routine */
static int          tiAckArg      = 0;       /* arg to user trigger ack routine */
static int          tiReadoutEnabled = 1;    /* Readout enabled, by default */
int                 tiFiberLatencyOffset = 0xbf; /* Default offset for fiber latency */
static int          tiVersion     = 0x0;     /* Firmware version */
static int          tiSyncEventFlag = 0;     /* Sync Event/Block Flag */
static int          tiSyncEventReceived = 0; /* Indicates reception of sync event */
static int          tiDoSyncResetRequest =0; /* Option to request a sync reset during readout ack */
static int          tiSlotNumber=0;          /* Slot number in which the TI resides */
static int          tiSwapTriggerBlock=0;    /* Decision on whether or not to swap the trigger block endianness */

/* Interrupt/Polling routine prototypes (static) */
static void tiInt(void);
#ifndef VXWORKS
static void tiPoll(void);
static void tiStartPollingThread(void);
/* polling thread pthread and pthread_attr */
pthread_attr_t tipollthread_attr;
pthread_t      tipollthread;
#endif

#ifdef VXWORKS
extern  int sysBusToLocalAdrs(int, char *, char **);
extern  int intDisconnect(int);
extern  int sysIntEnable(int);
IMPORT  STATUS sysIntDisable(int);
IMPORT  STATUS sysVmeDmaDone(int, int);
IMPORT  STATUS sysVmeDmaSend(UINT32, UINT32, int, BOOL);
#endif

static void FiberMeas();

/* VXS Payload Port to VME Slot map */
#define MAX_VME_SLOTS 21    /* This is either 20 or 21 */
unsigned short PayloadPort[MAX_VME_SLOTS+1] =
  {
    0,     /* Filler for mythical VME slot 0 */ 
#if MAX_VME_SLOTS == 21
    0,     /* VME Controller */
#endif
    17, 15, 13, 11, 9, 7, 5, 3, 1,  
    0,     /* Switch Slot A - SD */
    0,     /* Switch Slot B - CTP/GTP */
    2, 4, 6, 8, 10, 12, 14, 16, 
    18     /* VME Slot Furthest to the Right - TI */ 
  };

/* Library of routines for the SD */
#include "sdLib.c"

/* Library of routines for the CTP */
#include "ctpLib.c"

#ifndef VXWORKS
/* Library of routines for the GTP */
#include "gtpLib.c"
#endif

/*******************************************************************************
 *
 *  tiInit - Initialize the TIp register space into local memory,
 *  and setup registers given user input
 *
 *  ARGs: 
 *    tAddr  - A24 VME Address of the TI
 *    mode   - Readout/Triggering Mode
 *          0: External Trigger - Interrupt Mode
 *          1: TI/TImaster Trigger - Interrupt Mode
 *          2: External Trigger - Polling Mode
 *          3: TI/TImaster Trigger - Polling Mode
 *
 *    iFlag  - Initialization type
 *          0: Initialize the TI (default behavior)
 *          1: Do not initialize the board, just setup the pointers
 *             to the registers
 *
 *  RETURNS: OK if successful, otherwise ERROR.
 *
 */

int
tiInit(unsigned int tAddr, unsigned int mode, int iFlag)
{
  unsigned int laddr;
  unsigned int rval, boardID, prodID;
  unsigned int firmwareInfo;
  int stat;
  int noBoardInit=0;

  /* Check VME address */
  if(tAddr<0 || tAddr>0xffffff)
    {
      printf("%s: ERROR: Invalid VME Address (%d)\n",__FUNCTION__,
	     tAddr);
    }
  if(tAddr==0)
    {
      printf("%s: Scanning for TI...\n",__FUNCTION__);
      tAddr=tiFind();

      if(tAddr==0)
	{
	  printf("%s: ERROR: Unable to find TI\n",__FUNCTION__);
	  return ERROR;
	}
      
    }

  noBoardInit = iFlag&(0x1);

#ifdef VXWORKS
  stat = sysBusToLocalAdrs(0x39,(char *)tAddr,(char **)&laddr);
  if (stat != 0) 
    {
      printf("%s: ERROR: Error in sysBusToLocalAdrs res=%d \n",__FUNCTION__,stat);
      return ERROR;
    } 
  else 
    {
      printf("TI address = 0x%x\n",laddr);
    }
#else
  stat = vmeBusToLocalAdrs(0x39,(char *)tAddr,(char **)&laddr);
  if (stat != 0) 
    {
      printf("%s: ERROR: Error in vmeBusToLocalAdrs res=%d \n",__FUNCTION__,stat);
      return ERROR;
    } 
  else 
    {
      if(!noBoardInit)
	printf("TI VME (Local) address = 0x%.8x (0x%.8x)\n",tAddr,laddr);
    }
#endif
  tiA24Offset = laddr-tAddr;

  /* Set Up pointer */
  TIp = (struct TI_A24RegStruct *)laddr;

  /* Check if TI board is readable */
#ifdef VXWORKS
  stat = vxMemProbe((char *)(&TIp->boardID),0,4,(char *)&rval);
#else
  stat = vmeMemProbe((char *)(&TIp->boardID),4,(char *)&rval);
#endif

  if (stat != 0) 
    {
      printf("%s: ERROR: TI card not addressable\n",__FUNCTION__);
      TIp=NULL;
      return(-1);
    }
  else
    {
      /* Check that it is a TI */
      if(((rval&TI_BOARDID_TYPE_MASK)>>16) != TI_BOARDID_TYPE_TI) 
	{
	  printf("%s: ERROR: Invalid Board ID: 0x%x (rval = 0x%08x)\n",
		 __FUNCTION__,
		 (rval&TI_BOARDID_TYPE_MASK)>>16,rval);
	  TIp=NULL;
	  return(ERROR);
	}
      /* Check if this is board has a valid slot number */
      boardID =  (rval&TI_BOARDID_GEOADR_MASK)>>8;
      if((boardID <= 0)||(boardID >21)) 
	{
	  printf("%s: ERROR: Board Slot ID is not in range: %d\n",
		 __FUNCTION__,boardID);
	  TIp=NULL;
	  return(ERROR);
	}
      tiSlotNumber = boardID;

      /* Get the "production" type bits.  1=production, 0=prototype */
      prodID = (rval&TI_BOARDID_PROD_MASK)>>16;

      /* Determine whether or not we'll need to swap the trigger block endianess */
      if( ((TIp->boardID & TI_BOARDID_TYPE_MASK)>>16) != TI_BOARDID_TYPE_TI)
	tiSwapTriggerBlock=1;
      else
	tiSwapTriggerBlock=0;
      
    }
  
  /* Check if we should exit here, or initialize some board defaults */
  if(noBoardInit)
    {
      return OK;
    }

  
/*   tiDisableVXSSignals(); */
  tiReload();
  taskDelay(60);
  tiDisableVXSSignals();

  /* Get the Firmware Information and print out some details */
  firmwareInfo = tiGetFirmwareVersion();
  if(firmwareInfo>0)
    {
      printf("  User ID: 0x%x \tFirmware (version - revision): 0x%X - 0x%03X\n",
	     (firmwareInfo&0xFFFF0000)>>16, (firmwareInfo&0xF000)>>12, firmwareInfo&0xFFF);
      tiVersion = firmwareInfo&0xFFF;
      if(tiVersion < TI_SUPPORTED_FIRMWARE)
	{
	  printf("%s: ERROR: Firmware version (0x%x) not supported by this driver.\n  Supported version = 0x%x\n",
		 __FUNCTION__,tiVersion,TI_SUPPORTED_FIRMWARE);
	  TIp=NULL;
	  return ERROR;
	}
    }
  else
    {
      printf("%s:  ERROR: Invalid firmware 0x%08x\n",
	     __FUNCTION__,firmwareInfo);
      return ERROR;
    }

  /* Set some defaults, dependent on Master/Slave status */
  tiReadoutMode = mode;
  switch(mode)
    {
    case TI_READOUT_EXT_INT:
    case TI_READOUT_EXT_POLL:
      printf("... Configure as TI Master...\n");
      /* Master (Supervisor) Configuration: takes in external triggers */
      tiMaster = 1;

      /* BUSY from Loopback and Switch Slot B */
      tiSetBusySource(TI_BUSY_LOOPBACK | TI_BUSY_SWB,1);
      /* Onboard Clock Source */
      tiSetClockSource(TI_CLOCK_INTERNAL);
      /* Loopback Sync Source */
      tiSetSyncSource(TI_SYNC_LOOPBACK);
      break;

    case TI_READOUT_TS_INT:
    case TI_READOUT_TS_POLL:
      printf("... Configure as TI Slave...\n");
      /* Slave Configuration: takes in triggers from the Master (supervisor) */
      tiMaster = 0;
      /* BUSY from Switch Slot B */
      tiSetBusySource(TI_BUSY_SWB,1);
      /* Enable HFBR#1 */
      tiEnableFiber(1);
      /* HFBR#1 Clock Source */
      tiSetClockSource(1);
      /* HFBR#1 Sync Source */
      tiSetSyncSource(TI_SYNC_HFBR1);
      /* HFBR#1 Trigger Source */
      tiSetTriggerSource(TI_TRIGGER_HFBR1);
      break;

    default:
      printf("%s: ERROR: Invalid TI Mode %d\n",
	     __FUNCTION__,mode);
      return ERROR;
    }
  tiReadoutMode = mode;

  /* Setup some Other Library Defaults */
  if(tiMaster!=1)
    {
      FiberMeas();

      vmeWrite32(&TIp->syncWidth, 0x24);
      // TI IODELAY reset
      vmeWrite32(&TIp->reset,TI_RESET_IODELAY);
      taskDelay(1);

      // TI Sync auto alignment
      vmeWrite32(&TIp->reset,TI_RESET_AUTOALIGN_HFBR1_SYNC);
      taskDelay(1);

      // TI auto fiber delay measurement
      vmeWrite32(&TIp->reset,TI_RESET_MEASURE_LATENCY);
      taskDelay(1);

      // TI auto alignement fiber delay
      vmeWrite32(&TIp->reset,TI_RESET_FIBER_AUTO_ALIGN);
      taskDelay(1);
    }
  else
    {
      // TI IODELAY reset
      vmeWrite32(&TIp->reset,TI_RESET_IODELAY);
      taskDelay(1);

      // TI Sync auto alignment
      vmeWrite32(&TIp->reset,TI_RESET_AUTOALIGN_HFBR1_SYNC);
      taskDelay(1);
    }

  /* Reset I2C engine */
  vmeWrite32(&TIp->reset,TI_RESET_I2C);

  /* Setup a default Sync Delay and Pulse width */
  if(tiMaster==1)
    tiSetSyncDelayWidth(0x54, 0x2f, 0);

  /* Set default sync delay (fiber compensation) */
  if(tiMaster==1)
    vmeWrite32(&TIp->fiberSyncDelay,0x1f1f1f1f);

  /* Set Default Block Level to 1, and default crateID */
  tiSetBlockLevel(1);
  tiSetCrateID(tiCrateID);

  /* Set Event format 2 */
  tiSetEventFormat(1);

  /* Set Default Trig1 and Trig2 delay=4ns (0+1)*4ns, width=64ns (15+1)*4ns */
  tiSetTriggerPulse(1,0,15);
  tiSetTriggerPulse(2,0,15);

  /* Set the default prescale factor to 0 for rate/(0+1) */
  tiSetPrescale(0);

  /* Setup A32 data buffer with library default */
  tiSetAdr32(tiA32Base);

  /* Enable Bus Errors to complete Block transfers */
  tiEnableBusError();

  /* MGT reset */
  vmeWrite32(&TIp->reset,TI_RESET_MGT);
  taskDelay(1);

  /* Set this to 1 (ROC Lock mode), by default. */
  tiSetBlockBufferLevel(1);

  /* Disable all TS Inputs */
  tiDisableTSInput(TI_TSINPUT_ALL);

  return OK;
}

/*******************************************************************************
 *  
 *  tiFind - Find the TI within the prescribed "GEO Slot to A24 VME Address"
 *           range from slot 3 to 21.
 *           
 *  RETURNS: A24 VME address if found.  Otherwise, 0
 */

unsigned int
tiFind()
{
  int islot, stat, tiFound=0;
  unsigned int tAddr, laddr, rval;

  for(islot = 2; islot<22; islot++)
    {
      /* Form VME base address from slot number */
      tAddr = (islot<<19);
      
#ifdef VXWORKS
      stat = sysBusToLocalAdrs(0x39,(char *)tAddr,(char **)&laddr);
#else
      stat = vmeBusToLocalAdrs(0x39,(char *)tAddr,(char **)&laddr);
#endif
      if(stat != 0)
	continue;

      /* Check if this address is readable */
#ifdef VXWORKS
      stat = vxMemProbe((char *)(laddr),0,4,(char *)&rval);
#else
      stat = vmeMemProbe((char *)(laddr),4,(char *)&rval);
#endif

      if (stat != 0) 
	{
	  continue;
	}
      else
	{
	  /* Check that it is a TI */
	  if(((rval&TI_BOARDID_TYPE_MASK)>>16) != TI_BOARDID_TYPE_TI) 
	    {
	      continue;
	    }
	  else
	    {
	      printf("%s: Found TI at 0x%08x\n",__FUNCTION__,tAddr);
	      tiFound=1;
	      break;
	    }
	}
    }

  if(tiFound)
    return tAddr;
  else
    return 0;

}

int
tiCheckAddresses()
{
  unsigned int offset=0, expected=0, base=0;
  
  if(TIp==NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  printf("%s:\n\t ---------- Checking TI address space ---------- \n",__FUNCTION__);

  base = (unsigned int) &TIp->boardID;

  offset = ((unsigned int) &TIp->trigsrc) - base;
  expected = 0x20;
  if(offset != expected)
    printf("%s: ERROR TIp->triggerSource not at offset = 0x%x (@ 0x%x)\n",
	   __FUNCTION__,expected,offset);

/*   offset = ((unsigned int) &TIp->GTPtrigger) - base; */
/*   expected = 0x40; */
/*   if(offset != expected) */
/*     printf("%s: ERROR TIp->GTPtrigger not at offset = 0x%x (@ 0x%x)\n", */
/* 	   __FUNCTION__,expected,offset); */
    
  offset = ((unsigned int) &TIp->syncWidth) - base;
  expected = 0x80;
  if(offset != expected)
    printf("%s: ERROR TIp->syncWidth not at offset = 0x%x (@ 0x%x)\n",
	   __FUNCTION__,expected,offset);
    
  offset = ((unsigned int) &TIp->adr24) - base;
  expected = 0xD0;
  if(offset != expected)
    printf("%s: ERROR TIp->adr24 not at offset = 0x%x (@ 0x%x)\n",
	   __FUNCTION__,expected,offset);
    
  offset = ((unsigned int) &TIp->reset) - base;
  expected = 0x100;
  if(offset != expected)
    printf("%s: ERROR TIp->reset not at offset = 0x%x (@ 0x%x)\n",
	   __FUNCTION__,expected,offset);
    
  offset = ((unsigned int) &TIp->JTAGPROMBase[0]) - base;
  expected = 0x10000;
  if(offset != expected)
    printf("%s: ERROR TIp->JTAGPROMBase[0] not at offset = 0x%x (@ 0x%x)\n",
	   __FUNCTION__,expected,offset);
    
  offset = ((unsigned int) &TIp->JTAGFPGABase[0]) - base;
  expected = 0x20000;
  if(offset != expected)
    printf("%s: ERROR TIp->JTAGFPGABase[0] not at offset = 0x%x (@ 0x%x)\n",
	   __FUNCTION__,expected,offset);
    
  offset = ((unsigned int) &TIp->SWA) - base;
  expected = 0x30000;
  if(offset != expected)
    printf("%s: ERROR TIp->SWA not at offset = 0x%x (@ 0x%x)\n",
	   __FUNCTION__,expected,offset);
    
  offset = ((unsigned int) &TIp->SWB) - base;
  expected = 0x40000;
  if(offset != expected)
    printf("%s: ERROR TIp->SWB not at offset = 0x%x (@ 0x%x)\n",
	   __FUNCTION__,expected,offset);

  return OK;
}

/*******************************************************************************
 *
 * tiStatus - Print some status information of the TI to standard out
 *
 * RETURNS: OK if successful, ERROR otherwise
 *
 */

void
tiStatus()
{
  unsigned int boardID, fiber, intsetup, trigDelay;
  unsigned int adr32, blocklevel, vmeControl, trigger, sync;
  unsigned int busy, clock,prescale, blockBuffer;
  unsigned int tsInput, iinp;
  unsigned int output, fiberSyncDelay;
  unsigned int livetime, busytime;
  unsigned int inputCounter;
  unsigned int blockStatus[5], iblock, nblocksReady, nblocksNeedAck;
  unsigned int nblocks;
  unsigned int ifiber, fibermask;
  unsigned int TIBase;
  unsigned long long int l1a_count=0;
  unsigned int blocklimit;

  if(TIp==NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return;
    }

  /* latch live and busytime scalers */
  tiLatchTimers();
  l1a_count    = tiGetEventCounter();

  TILOCK;
  boardID      = vmeRead32(&TIp->boardID);
  fiber        = vmeRead32(&TIp->fiber);
  intsetup     = vmeRead32(&TIp->intsetup);
  trigDelay    = vmeRead32(&TIp->trigDelay);
  adr32        = vmeRead32(&TIp->adr32);
  blocklevel   = vmeRead32(&TIp->blocklevel);
  vmeControl   = vmeRead32(&TIp->vmeControl);
  trigger      = vmeRead32(&TIp->trigsrc);
  sync         = vmeRead32(&TIp->sync);
  busy         = vmeRead32(&TIp->busy);
  clock        = vmeRead32(&TIp->clock);
  prescale     = vmeRead32(&TIp->trig1Prescale);
  blockBuffer  = vmeRead32(&TIp->blockBuffer);

  tsInput      = vmeRead32(&TIp->tsInput);

  output       = vmeRead32(&TIp->output);
  blocklimit   = vmeRead32(&TIp->blocklimit);
  fiberSyncDelay = vmeRead32(&TIp->fiberSyncDelay);

  /* Latch scalers first */
  vmeWrite32(&TIp->reset,TI_RESET_SCALERS_LATCH);
  livetime     = vmeRead32(&TIp->livetime);
  busytime     = vmeRead32(&TIp->busytime);

  inputCounter = vmeRead32(&TIp->inputCounter);

  for(iblock=0;iblock<4;iblock++)
    blockStatus[iblock] = vmeRead32(&TIp->blockStatus[iblock]);

  blockStatus[4] = vmeRead32(&TIp->adr24);

  nblocks      = vmeRead32(&TIp->nblocks);
  TIUNLOCK;

  TIBase = (unsigned int)TIp;

  printf("\n");
#ifdef VXWORKS
  printf("STATUS for TI at base address 0x%08x \n",
	 (unsigned int) TIp);
#else
  printf("STATUS for TI at VME (Local) base address 0x%08x (0x%08x) \n",
	 (unsigned int) TIp - tiA24Offset, (unsigned int) TIp);
#endif
  printf("--------------------------------------------------------------------------------\n");
  printf(" A32 Data buffer ");
  if((vmeControl&TI_VMECONTROL_A32) == TI_VMECONTROL_A32)
    {
      printf("ENABLED at ");
#ifdef VXWORKS
      printf("base address 0x%08x\n",
	     (unsigned int)TIpd);
#else
      printf("VME (Local) base address 0x%08x (0x%08x)\n",
	     (unsigned int)TIpd - tiA32Offset, (unsigned int)TIpd);
#endif
    }
  else
    printf("DISABLED\n");

  if(tiMaster)
    printf(" Configured as a TI Master\n");
  else
    printf(" Configured as a TI Slave\n");

  printf(" Readout Count: %d\n",tiIntCount);
  printf("     Ack Count: %d\n",tiAckCount);
  printf("     L1A Count: %llu\n",l1a_count);
  printf("   Block Limit: %d\n",blocklimit);
  printf("   Block Count: %d\n",nblocks & TI_NBLOCKS_COUNT_MASK);
  printf(" Registers (offset):\n");
  printf("  boardID        (0x%04x) = 0x%08x\t", (unsigned int)(&TIp->boardID) - TIBase, boardID);
  printf("  fiber          (0x%04x) = 0x%08x\n", (unsigned int)(&TIp->fiber) - TIBase, fiber);
  printf("  intsetup       (0x%04x) = 0x%08x\t", (unsigned int)(&TIp->intsetup) - TIBase, intsetup);
  printf("  trigDelay      (0x%04x) = 0x%08x\n", (unsigned int)(&TIp->trigDelay) - TIBase, trigDelay);
  printf("  adr32          (0x%04x) = 0x%08x\t", (unsigned int)(&TIp->adr32) - TIBase, adr32);
  printf("  vmeControl     (0x%04x) = 0x%08x\n", (unsigned int)(&TIp->vmeControl) - TIBase, vmeControl);
  printf("  trigger        (0x%04x) = 0x%08x\t", (unsigned int)(&TIp->trigsrc) - TIBase, trigger);
  printf("  sync           (0x%04x) = 0x%08x\n", (unsigned int)(&TIp->sync) - TIBase, sync);
  printf("  busy           (0x%04x) = 0x%08x\t", (unsigned int)(&TIp->busy) - TIBase, busy);
  printf("  clock          (0x%04x) = 0x%08x\n", (unsigned int)(&TIp->clock) - TIBase, clock);
  printf("  blockBuffer    (0x%04x) = 0x%08x\t", (unsigned int)(&TIp->blockBuffer) - TIBase, blockBuffer);

  printf("  output         (0x%04x) = 0x%08x\n", (unsigned int)(&TIp->output) - TIBase, output);
  printf("  fiberSyncDelay (0x%04x) = 0x%08x\n", (unsigned int)(&TIp->fiberSyncDelay) - TIBase, fiberSyncDelay);

  printf("  livetime       (0x%04x) = 0x%08x\t", (unsigned int)(&TIp->livetime) - TIBase, livetime);
  printf("  busytime       (0x%04x) = 0x%08x\n", (unsigned int)(&TIp->busytime) - TIBase, busytime);
  printf("\n");

  printf(" Crate ID = 0x%02x\n",boardID&TI_BOARDID_CRATEID_MASK);
  printf(" Block size = %d\n",blocklevel & TI_BLOCKLEVEL_MASK);

  fibermask = fiber;
  if(fibermask)
    {
      printf(" HFBR enabled (0x%x)= ",fibermask);
      for(ifiber=0; ifiber<8; ifiber++)
	{
	  if( fibermask & (1<<ifiber) ) 
	    printf(" %d",ifiber+1);
	}
      printf("\n");
    }
  else
    printf(" All HFBR Disabled\n");

  if(tiMaster)
    {
      if(tiSlaveMask)
	{
	  printf(" TI Slaves Configured on HFBR (0x%x) = ",tiSlaveMask);
	  fibermask = tiSlaveMask;
	  for(ifiber=0; ifiber<8; ifiber++)
	    {
	      if( fibermask & (1<<ifiber)) 
		printf(" %d",ifiber+1);
	    }
	  printf("\n");	
	}
      else
	printf(" No TI Slaves Configured on HFBR\n");
      
    }

  if(tiTriggerSource&TI_TRIGSRC_SOURCEMASK)
    {
      if(trigger)
	printf(" Trigger input source (ENABLED) =\n");
      else
	printf(" Trigger input source (DISABLED) =\n");
      if(tiTriggerSource & TI_TRIGSRC_P0)
	printf("   P0 Input\n");
      if(tiTriggerSource & TI_TRIGSRC_HFBR1)
	printf("   HFBR #1 Input\n");
      if(tiTriggerSource & TI_TRIGSRC_LOOPBACK)
	printf("   Loopback\n");
      if(tiTriggerSource & TI_TRIGSRC_FPTRG)
	printf("   Front Panel TRG\n");
      if(tiTriggerSource & TI_TRIGSRC_VME)
	printf("   VME Command\n");
      if(tiTriggerSource & TI_TRIGSRC_TSINPUTS)
	printf("   Front Panel TS Inputs\n");
      if(tiTriggerSource & TI_TRIGSRC_TSREV2)
	printf("   Trigger Supervisor (rev2)\n");
      if(tiTriggerSource & TI_TRIGSRC_PULSER)
	printf("   Internal Pulser\n");
      if(tiTriggerSource & TI_TRIGSRC_PART_1)
	printf("   TS Partition 1 (HFBR #1)\n");
      if(tiTriggerSource & TI_TRIGSRC_PART_2)
	printf("   TS Partition 2 (HFBR #1)\n");
      if(tiTriggerSource & TI_TRIGSRC_PART_3)
	printf("   TS Partition 3 (HFBR #1)\n");
      if(tiTriggerSource & TI_TRIGSRC_PART_4)
	printf("   TS Partition 4 (HFBR #1)\n");
    }
  else 
    {
      printf(" No Trigger input sources\n");
    }

  if(tsInput & TI_TSINPUT_MASK)
    {
      printf(" Front Panel TS Inputs Enabled: ");
      for(iinp=0; iinp<6; iinp++)
	{
	  if( (tsInput & TI_TSINPUT_MASK) & (1<<iinp)) 
	    printf(" %d",iinp+1);
	}
      printf("\n");	
    }
  else
    {
      printf(" All Front Panel TS Inputs Disabled\n");
    }

  if(sync&TI_SYNC_SOURCEMASK)
    {
      printf(" Sync source = \n");
      if(sync & TI_SYNC_P0)
	printf("   P0 Input\n");
      if(sync & TI_SYNC_HFBR1)
	printf("   HFBR #1 Input\n");
      if(sync & TI_SYNC_HFBR5)
	printf("   HFBR #5 Input\n");
      if(sync & TI_SYNC_FP)
	printf("   Front Panel Input\n");
      if(sync & TI_SYNC_LOOPBACK)
	printf("   Loopback\n");
      if(sync & TI_SYNC_USER_SYNCRESET_ENABLED)
	printf("   User SYNCRESET Receieve Enabled\n");
    }
  else
    {
      printf(" No SYNC input source configured\n");
    }

  if(busy&TI_BUSY_SOURCEMASK)
    {
      printf(" BUSY input source = \n");
      if(busy & TI_BUSY_SWA)
	printf("   Switch Slot A    %s\n",(busy&TI_BUSY_MONITOR_SWA)?"** BUSY **":"");
      if(busy & TI_BUSY_SWB)
	printf("   Switch Slot B    %s\n",(busy&TI_BUSY_MONITOR_SWB)?"** BUSY **":"");
      if(busy & TI_BUSY_P2)
	printf("   P2 Input         %s\n",(busy&TI_BUSY_MONITOR_P2)?"** BUSY **":"");
      if(busy & TI_BUSY_FP_FTDC)
	printf("   Front Panel TDC  %s\n",(busy&TI_BUSY_MONITOR_FP_FTDC)?"** BUSY **":"");
      if(busy & TI_BUSY_FP_FADC)
	printf("   Front Panel ADC  %s\n",(busy&TI_BUSY_MONITOR_FP_FADC)?"** BUSY **":"");
      if(busy & TI_BUSY_FP)
	printf("   Front Panel      %s\n",(busy&TI_BUSY_MONITOR_FP)?"** BUSY **":"");
      if(busy & TI_BUSY_LOOPBACK)
	printf("   Loopback         %s\n",(busy&TI_BUSY_MONITOR_LOOPBACK)?"** BUSY **":"");
      if(busy & TI_BUSY_HFBR1)
	printf("   HFBR #1          %s\n",(busy&TI_BUSY_MONITOR_HFBR1)?"** BUSY **":"");
      if(busy & TI_BUSY_HFBR2)
	printf("   HFBR #2          %s\n",(busy&TI_BUSY_MONITOR_HFBR1)?"** BUSY **":"");
      if(busy & TI_BUSY_HFBR3)
	printf("   HFBR #3          %s\n",(busy&TI_BUSY_MONITOR_HFBR1)?"** BUSY **":"");
      if(busy & TI_BUSY_HFBR4)
	printf("   HFBR #4          %s\n",(busy&TI_BUSY_MONITOR_HFBR1)?"** BUSY **":"");
      if(busy & TI_BUSY_HFBR5)
	printf("   HFBR #5          %s\n",(busy&TI_BUSY_MONITOR_HFBR1)?"** BUSY **":"");
      if(busy & TI_BUSY_HFBR6)
	printf("   HFBR #6          %s\n",(busy&TI_BUSY_MONITOR_HFBR1)?"** BUSY **":"");
      if(busy & TI_BUSY_HFBR7)
	printf("   HFBR #7          %s\n",(busy&TI_BUSY_MONITOR_HFBR1)?"** BUSY **":"");
      if(busy & TI_BUSY_HFBR8)
	printf("   HFBR #8          %s\n",(busy&TI_BUSY_MONITOR_HFBR1)?"** BUSY **":"");
    }
  else
    {
      printf(" No BUSY input source configured\n");
    }

  if(intsetup&TI_INTSETUP_ENABLE)
    printf(" Interrupts ENABLED\n");
  else
    printf(" Interrupts DISABLED\n");
  printf("   Level = %d   Vector = 0x%02x\n",
	 (intsetup&TI_INTSETUP_LEVEL_MASK)>>8, (intsetup&TI_INTSETUP_VECTOR_MASK));
  
  if(vmeControl&TI_VMECONTROL_BERR)
    printf(" Bus Errors Enabled\n");

  printf(" Blocks ready for readout: %d\n",(blockBuffer&TI_BLOCKBUFFER_BLOCKS_READY_MASK)>>8);
  if(tiMaster)
    {
      /* TI slave block status */
      fibermask = tiSlaveMask;
      for(ifiber=0; ifiber<8; ifiber++)
	{
	  if( fibermask & (1<<ifiber) )
	    {
	      if( (ifiber % 2) == 0)
		{
		  nblocksReady   = blockStatus[ifiber/2] & TI_BLOCKSTATUS_NBLOCKS_READY0;
		  nblocksNeedAck = (blockStatus[ifiber/2] & TI_BLOCKSTATUS_NBLOCKS_NEEDACK0)>>8;
		}
	      else
		{
		  nblocksReady   = (blockStatus[(ifiber-1)/2] & TI_BLOCKSTATUS_NBLOCKS_READY1)>>16;
		  nblocksNeedAck = (blockStatus[(ifiber-1)/2] & TI_BLOCKSTATUS_NBLOCKS_NEEDACK1)>>24;
		}
	      printf("  Fiber %d  :  Blocks ready / need acknowledge: %d / %d\n",
		     ifiber+1,nblocksReady, nblocksNeedAck);
	    }
	}

      /* TI master block status */
      nblocksReady   = (blockStatus[4] & TI_BLOCKSTATUS_NBLOCKS_READY1)>>16;
      nblocksNeedAck = (blockStatus[4] & TI_BLOCKSTATUS_NBLOCKS_NEEDACK1)>>24;
      printf("  Loopback :  Blocks ready / need acknowledge: %d / %d\n",
	     nblocksReady, nblocksNeedAck);

    }
  printf(" Input counter %d\n",inputCounter);

  printf("--------------------------------------------------------------------------------\n");
  printf("\n\n");

}


/*******************************************************************************
 *
 * tiGetFirmwareVersion - Get the Firmware Version
 *
 * RETURNS: Firmware Version if successful, ERROR otherwise
 *
 */
int
tiGetFirmwareVersion()
{
  unsigned int rval=0;
  if(TIp==NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  /* reset the VME_to_JTAG engine logic */
  vmeWrite32(&TIp->reset,TI_RESET_JTAG);

  /* Reset FPGA JTAG to "reset_idle" state */
  vmeWrite32(&TIp->JTAGFPGABase[(0x003C)>>2],0);

  /* enable the user_code readback */
  vmeWrite32(&TIp->JTAGFPGABase[(0x092C)>>2],0x3c8);

  /* shift in 32-bit to FPGA JTAG */
  vmeWrite32(&TIp->JTAGFPGABase[(0x1F1C)>>2],0);
  
  /* Readback the firmware version */
  rval = vmeRead32(&TIp->JTAGFPGABase[(0x1F1C)>>2]);
  TIUNLOCK;

  return rval;
}


/*******************************************************************************
 *
 * tiReload - Reload the firmware on the FPGA
 *
 * RETURNS: OK if successful, ERROR otherwise
 *
 */

int
tiReload()
{
  if(TIp==NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  vmeWrite32(&TIp->reset,TI_RESET_JTAG);
  vmeWrite32(&TIp->JTAGPROMBase[(0x3c)>>2],0);
  vmeWrite32(&TIp->JTAGPROMBase[(0xf2c)>>2],0xEE);
  TIUNLOCK;

  printf ("%s: \n FPGA Re-Load ! \n",__FUNCTION__);
  return OK;
  
}

unsigned int
tiGetSerialNumber(char **rSN)
{
  unsigned int rval=0;
  char retSN[10];

  memset(retSN,0,sizeof(retSN));
  
  if(TIp==NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  vmeWrite32(&TIp->reset,TI_RESET_JTAG);           /* reset */
  vmeWrite32(&TIp->JTAGPROMBase[(0x3c)>>2],0);     /* Reset_idle */
  vmeWrite32(&TIp->JTAGPROMBase[(0xf2c)>>2],0xFD); /* load the UserCode Enable */
  vmeWrite32(&TIp->JTAGPROMBase[(0x1f1c)>>2],0);   /* shift in 32-bit of data */
  rval = vmeRead32(&TIp->JTAGPROMBase[(0x1f1c)>>2]);
  TIUNLOCK;

  if(rSN!=NULL)
    {
      sprintf(retSN,"TI-%d",rval&0x7ff);
      strcpy((char *)rSN,retSN);
    }


  printf("%s: TI Serial Number is %s (0x%08x)\n", 
	 __FUNCTION__,retSN,rval);

  return rval;
  

}

/*******************************************************************************
 *
 * tiClockResync - Resync the 250 MHz Clock
 *
 * RETURNS: OK if successful, ERROR otherwise
 *
 */

int
tiClockResync()
{
  if(TIp==NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  
  TILOCK;
  vmeWrite32(&TIp->syncCommand,TI_SYNCCOMMAND_AD9510_RESYNC); 
  TIUNLOCK;

  printf ("%s: \n\t AD9510 ReSync ! \n",__FUNCTION__);
  return OK;
  
}

/*******************************************************************************
 *
 * tiReset - Perform a soft reset of the TI
 *
 * RETURNS: OK if successful, ERROR otherwise
 *
 */

int
tiReset()
{
  if(TIp==NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }
  
  TILOCK;
  vmeWrite32(&TIp->reset,TI_RESET_SOFT);
  TIUNLOCK;
  return OK;
}

/*******************************************************************************
 *
 * tiSetCrateID - Set the crate ID that shows up in the data fifo
 *
 * RETURNS: OK if successful, ERROR otherwise
 *
 */

int
tiSetCrateID(unsigned int crateID)
{
  if(TIp==NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  if( (crateID>0xff) || (crateID==0) )
    {
      printf("%s: ERROR: Invalid crate id (0x%x)\n",__FUNCTION__,crateID);
      return ERROR;
    }
  
  TILOCK;
  vmeWrite32(&TIp->boardID,
	   (vmeRead32(&TIp->boardID) & ~TI_BOARDID_CRATEID_MASK)  | crateID);
  tiCrateID = crateID;
  TIUNLOCK;

  return OK;
  
}

/*******************************************************************************
 *
 * tiSetBlockLevel - Set the number of events per block
 *
 * RETURNS: OK if successful, ERROR otherwise
 *
 */

int
tiSetBlockLevel(unsigned int blockLevel)
{
  if(TIp==NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  if( (blockLevel>TI_BLOCKLEVEL_MASK) || (blockLevel==0) )
    {
      printf("%s: ERROR: Invalid Block Level (%d)\n",__FUNCTION__,blockLevel);
      return ERROR;
    }

  TILOCK;
  vmeWrite32(&TIp->blocklevel, blockLevel);
  tiBlockLevel = blockLevel;
  TIUNLOCK;

  return OK;

}

/*******************************************************************************
 *
 * tiSetTriggerSource - Set the trigger source
 *     This routine will set a library variable to be set in the TI registers
 *     at a call to tiIntEnable.  
 *
 *  trig - integer indicating the trigger source
 *         0: P0
 *         1: HFBR#1
 *         2: Front Panel (TRG)
 *         3: Front Panel TS Inputs
 *         4: TS (rev2) 
 *         5: Random
 *       6-9: TS Partition 1-4
 *
 * RETURNS: OK if successful, ERROR otherwise
 *
 */

int
tiSetTriggerSource(int trig)
{
  unsigned int trigenable=0;

  if(TIp==NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  if( (trig>9) || (trig<0) )
    {
      printf("%s: ERROR: Invalid Trigger Source (%d).  Must be between 0 and 10.\n",
	     __FUNCTION__,trig);
      return ERROR;
    }


  if(!tiMaster)
    { 
      /* Setup for TI Slave */
      trigenable = TI_TRIGSRC_VME;

      if((trig>=6) && (trig<=9)) /* TS partition specified */
	{
	  switch(trig)
	    {
	    case TI_TRIGGER_PART_1:
	      trigenable |= TI_TRIGSRC_PART_1;
	      break;
	  
	    case TI_TRIGGER_PART_2:
	      trigenable |= TI_TRIGSRC_PART_2;
	      break;
	  
	    case TI_TRIGGER_PART_3:
	      trigenable |= TI_TRIGSRC_PART_3;
	      break;

	    case TI_TRIGGER_PART_4:
	      trigenable |= TI_TRIGSRC_PART_4;
	      break;
	    }
	}
      else
	{
	  trigenable |= TI_TRIGSRC_HFBR1;
	  if( (trig & ~TI_TRIGGER_HFBR1) != 0)
	    {
	      printf("%s: WARN:  Only valid trigger source for TI Slave is HFBR1 (%d).",
		     __FUNCTION__,TI_TRIGGER_HFBR1);
	      printf("  Ignoring specified trig (0x%x)\n",trig);
	    }
	}

    }
  else
    {
      /* Setup for TI Master */

      /* Set VME and Loopback by default */
      trigenable  = TI_TRIGSRC_VME;
      trigenable |= TI_TRIGSRC_LOOPBACK;

      switch(trig)
	{
	case TI_TRIGGER_P0:
	  trigenable |= TI_TRIGSRC_P0;
	  break;

	case TI_TRIGGER_HFBR1:
	  trigenable |= TI_TRIGSRC_HFBR1;
	  break;

	case TI_TRIGGER_FPTRG:
	  trigenable |= TI_TRIGSRC_FPTRG;
	  break;

	case TI_TRIGGER_TSINPUTS:
	  trigenable |= TI_TRIGSRC_TSINPUTS;
	  break;

	case TI_TRIGGER_TSREV2:
	  trigenable |= TI_TRIGSRC_TSREV2;
	  break;

	case TI_TRIGGER_PULSER:
	  trigenable |= TI_TRIGSRC_PULSER;
	  break;

	default:
	  printf("%s: ERROR: Invalid Trigger Source (%d) for TI Master\n",
		 __FUNCTION__,trig);
	  return ERROR;
	}
    }

  tiTriggerSource = trigenable;
  printf("%s: INFO: tiTriggerSource = 0x%x\n",__FUNCTION__,tiTriggerSource);

  return OK;
}

/*******************************************************************************
 *
 * tiSetTriggerSourceMask - Set trigger sources with specified trigmask
 *
 *    This routine is for special use when tiSetTriggerSource(...) does
 *    not set all of the trigger sources that is required by the user.
 *
 *  trigmask bits:  
 *                 0:  P0
 *                 1:  HFBR #1 
 *                 2:  TI Master Loopback
 *                 3:  Front Panel (TRG) Input
 *                 4:  VME Trigger
 *                 5:  Front Panel TS Inputs
 *                 6:  TS (rev 2) Input
 *                 7:  Random Trigger
 *                 8:  FP/Ext/GTP 
 *                 9:  P2 Busy 
 *
 * RETURNS: OK if successful, ERROR otherwise
 *
 */

int
tiSetTriggerSourceMask(int trigmask)
{
  if(TIp==NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  /* Check input mask */
  if(trigmask>TI_TRIGSRC_SOURCEMASK)
    {
      printf("%s: ERROR: Invalid trigger source mask (0x%x).\n",
	     __FUNCTION__,trigmask);
      return ERROR;
    }

  tiTriggerSource = trigmask;

  return OK;
}

/*******************************************************************************
 *
 * tiEnableTriggerSource - Enable trigger sources set by 
 *                          tiSetTriggerSource(...) or
 *                          tiSetTriggerSourceMask(...)
 *
 * RETURNS: OK if successful, ERROR otherwise
 *
 */


int
tiEnableTriggerSource()
{
  if(TIp==NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  if(tiTriggerSource==0)
    {
      printf("%s: WARN: No Trigger Sources Enabled\n",__FUNCTION__);
    }

  TILOCK;
  vmeWrite32(&TIp->trigsrc, tiTriggerSource);
  TIUNLOCK;

  return OK;

}

/*******************************************************************************
 *
 * tiDisableTriggerSource - Disable trigger sources
 *    
 *    ARGs: fflag - 0: Disable Triggers
 *                 >0: Disable Triggers and generate enough
 *                     triggers to fill the current block
 *                     ** Must be TI master **
 *
 * RETURNS: OK if successful, ERROR otherwise
 *
 */

int
tiDisableTriggerSource(int fflag)
{
  int regset=0;
  if(TIp==NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;

  if(tiMaster)
    regset = TI_TRIGSRC_LOOPBACK;

  vmeWrite32(&TIp->trigsrc,regset);

  TIUNLOCK;
  if(fflag && tiMaster)
    {
      tiFillToEndBlock();      
    }

  return OK;

}

/*******************************************************************************
 *
 * tiSetSyncSource - Set the Sync source mask
 *
 *  sync - MASK indicating the sync source
 *       bit: description
 *         0: P0
 *         1: HFBR1
 *         2: HFBR5
 *         3: FP
 *         4: LOOPBACK
 *
 * RETURNS: OK if successful, ERROR otherwise
 *
 */

int
tiSetSyncSource(unsigned int sync)
{
  if(TIp==NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  if(sync>TI_SYNC_SOURCEMASK)
    {
      printf("%s: ERROR: Invalid Sync Source Mask (%d).\n",
	     __FUNCTION__,sync);
      return ERROR;
    }

  TILOCK;
  vmeWrite32(&TIp->sync,sync);
  TIUNLOCK;

  return OK;
}

/*******************************************************************************
 *
 * tiSetEventFormat - Set the event format
 *
 *  format - integer number indicating the event format
 *
 *           Description
 *           0: Shortest words per trigger
 *           1: Timing word enabled
 *           2: Status word enabled
 *           3: Timing and Status words enabled
 *
 * RETURNS: OK if successful, ERROR otherwise
 *
 */


int
tiSetEventFormat(int format)
{
  unsigned int formatset=0;

  if(TIp==NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  if( (format>3) || (format<0) )
    {
      printf("%s: ERROR: Invalid Event Format (%d).  Must be between 0 and 3.\n",
	     __FUNCTION__,format);
      return ERROR;
    }

  TILOCK;
/*   formatset = TI_DATAFORMAT_TWOBLOCK_PLACEHOLDER; */

  switch(format)
    {
    case 0:
      break;

    case 1:
      formatset |= TI_DATAFORMAT_TIMING_WORD;
      break;

    case 2:
      formatset |= TI_DATAFORMAT_STATUS_WORD;
      break;

    case 3:
      formatset |= (TI_DATAFORMAT_TIMING_WORD | TI_DATAFORMAT_STATUS_WORD);
      break;

    }
 
  vmeWrite32(&TIp->dataFormat,formatset);

  TIUNLOCK;

  return OK;
}

/*******************************************************************************
 *
 * tiSoftTrig - Set and enable the "software" trigger
 *
 *  trigger     - trigger type 1 or 2 (playback trigger)
 *  nevents     - integer number of events to trigger
 *  period_inc  - period multiplier, depends on range (0-0x7FFF)
 *  range       - small (0) or large (1)
 *     For small, the period range:
 *            minimum of 120ns, increments of 30ns up to 983.13us
 *     In large, the period range:
 *            minimum of 120ns, increments of 30.72us up to 1.007s
 *
 * RETURNS: OK if successful, ERROR otherwise
 *
 */


int
tiSoftTrig(int trigger, unsigned int nevents, unsigned int period_inc, int range)
{
  unsigned int periodMax=(TI_FIXEDPULSER1_PERIOD_MASK>>16);
  unsigned int reg=0;
  int time=0;

  if(TIp==NULL)
    {
      logMsg("\ntsSoftTrig: ERROR: TI not initialized\n",1,2,3,4,5,6);
      return ERROR;
    }

  if(trigger!=1 && trigger!=2)
    {
      logMsg("\ntiSoftTrig: ERROR: Invalid trigger type %d\n",trigger,2,3,4,5,6);
      return ERROR;
    }

  if(nevents>TI_FIXEDPULSER1_NTRIGGERS_MASK)
    {
      logMsg("\ntiSoftTrig: ERROR: nevents (%d) must be less than %d\n",nevents,
	     TI_FIXEDPULSER1_NTRIGGERS_MASK,3,4,5,6);
      return ERROR;
    }
  if(period_inc>periodMax)
    {
      logMsg("\ntiSoftTrig: ERROR: period_inc (%d) must be less than %d ns\n",
	     period_inc,periodMax,3,4,5,6);
      return ERROR;
    }
  if( (range!=0) && (range!=1) )
    {
      logMsg("\ntiSoftTrig: ERROR: range must be 0 or 1\n",
	     periodMax,2,3,4,5,6);
      return ERROR;
    }

  if(range==0)
    time = 120+30*period_inc;
  if(range==1)
    time = 120+30*period_inc*1024;

  logMsg("\ntiSoftTrig: INFO: Setting software trigger for %d nevents with period of %d\n",
	 nevents,time,3,4,5,6);

  reg = (range<<31)| (period_inc<<16) | (nevents);
  TILOCK;
  if(trigger==1)
    {
      vmeWrite32(&TIp->fixedPulser1, reg);
    }
  else if(trigger==2)
    {
      vmeWrite32(&TIp->fixedPulser2, reg);
    }
  TIUNLOCK;

  return OK;

}


/*******************************************************************************
 *
 * tiSetRandomTrigger - Set the parameters of the random internal trigger
 *
 *    ARGS: trigger  - Trigger Selection
 *                     1: trig1
 *                     2: trig2
 *          setting  - 
 *
 * RETURNS: OK if successful, ERROR otherwise.
 *
 */


int
tiSetRandomTrigger(int trigger, int setting)
{
  double rate;

  if(TIp==NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  if(trigger!=1 && trigger!=2)
    {
      logMsg("\ntiSetRandomTrigger: ERROR: Invalid trigger type %d\n",trigger,2,3,4,5,6);
      return ERROR;
    }

  if(setting>TI_RANDOMPULSER_TRIG1_RATE_MASK)
    {
      printf("%s: ERROR: setting (0x%x) must be less than 0x%x\n",
	     __FUNCTION__,setting,TI_RANDOMPULSER_TRIG1_RATE_MASK);
      return ERROR;
    }

  rate = ((double)500000) / ((double) (2<<(setting-1)));

  setting |= (TI_RANDOMPULSER_TRIG1_ENABLE);  /* Set the enable bit */

  printf("%s: Enabling random trigger (trig%d) at rate (kHz) = %.2f\n",
	 __FUNCTION__,trigger,rate);

  TILOCK;
  if(trigger==1)
    vmeWrite32(&TIp->randomPulser, (setting | (setting<<4)) );
  else if (trigger==2)
    vmeWrite32(&TIp->randomPulser, ((setting | (setting<<4))<<8));
  TIUNLOCK;

  return OK;
}


int
tiDisableRandomTrigger()
{
  if(TIp==NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  vmeWrite32(&TIp->randomPulser,0);
  TIUNLOCK;
  return OK;
}



/*******************************************************************************
 *
 * tiReadBlock - Read a block of events from the TI
 *
 *    data  - local memory address to place data
 *    nwrds - Max number of words to transfer
 *    rflag - Readout Flag
 *              0 - programmed I/O from the specified board
 *              1 - DMA transfer using Universe/Tempe DMA Engine 
 *                    (DMA VME transfer Mode must be setup prior)
 *
 * RETURNS: Number of words transferred to data if successful, ERROR otherwise
 *
 */

int
tiReadBlock(volatile unsigned int *data, int nwrds, int rflag)
{
  int ii, dummy=0;
  int dCnt, retVal, xferCount;
  volatile unsigned int *laddr;
  unsigned int vmeAdr, val;

  if(TIp==NULL)
    {
      logMsg("\ntiReadBlock: ERROR: TI not initialized\n",1,2,3,4,5,6);
      return ERROR;
    }

  if(TIpd==NULL)
    {
      logMsg("\ntiReadBlock: ERROR: TI A32 not initialized\n",1,2,3,4,5,6);
      return ERROR;
    }

  if(data==NULL) 
    {
      logMsg("\ntiReadBlock: ERROR: Invalid Destination address\n",0,0,0,0,0,0);
      return(ERROR);
    }

  TILOCK;
  if(rflag >= 1)
    { /* Block transfer */

      /* Assume that the DMA programming is already setup. 
	 Don't Bother checking if there is valid data - that should be done prior
	 to calling the read routine */
      
      /* Check for 8 byte boundary for address - insert dummy word (Slot 0 FADC Dummy DATA)*/
      if((unsigned long) (data)&0x7) 
	{
#ifdef VXWORKS
	  *data = (TI_FILLER_WORD_TYPE) | (tiSlotNumber<<22);
#else
	  *data = LSWAP((TI_FILLER_WORD_TYPE) | (tiSlotNumber<<22));
#endif
	  dummy = 1;
	  laddr = (data + 1);
	} 
      else 
	{
	  dummy = 0;
	  laddr = data;
	}
      
      vmeAdr = ((unsigned int)(TIpd) - tiA32Offset);

#ifdef VXWORKS
      retVal = sysVmeDmaSend((UINT32)laddr, vmeAdr, (nwrds<<2), 0);
#else
      retVal = vmeDmaSend((UINT32)laddr, vmeAdr, (nwrds<<2));
#endif
      if(retVal != 0) 
	{
	  logMsg("\ntiReadBlock: ERROR in DMA transfer Initialization 0x%x\n",retVal,0,0,0,0,0);
	  TIUNLOCK;
	  return(retVal);
	}

      /* Wait until Done or Error */
#ifdef VXWORKS
      retVal = sysVmeDmaDone(10000,1);
#else
      retVal = vmeDmaDone();
#endif

      if(retVal > 0)
	{
#ifdef VXWORKS
	  xferCount = (nwrds - (retVal>>2) + dummy); /* Number of longwords transfered */
#else
	  xferCount = ((retVal>>2) + dummy); /* Number of longwords transfered */
#endif
	  TIUNLOCK;
	  return(xferCount);
	}
      else if (retVal == 0) 
	{
#ifdef VXWORKS
	  logMsg("\ntiReadBlock: WARN: DMA transfer terminated by word count 0x%x\n",
		 nwrds,0,0,0,0,0);
#else
	  logMsg("\ntiReadBlock: WARN: DMA transfer returned zero word count 0x%x\n",
		 nwrds,0,0,0,0,0,0);
#endif
	  TIUNLOCK;
	  return(nwrds);
	}
      else 
	{  /* Error in DMA */
#ifdef VXWORKS
	  logMsg("\ntiReadBlock: ERROR: sysVmeDmaDone returned an Error\n",
		 0,0,0,0,0,0);
#else
	  logMsg("\ntiReadBlock: ERROR: vmeDmaDone returned an Error\n",
		 0,0,0,0,0,0);
#endif
	  TIUNLOCK;
	  return(retVal>>2);
	  
	}
    }
  else
    { /* Programmed IO */
      dCnt = 0;
      ii=0;

      while(ii<nwrds) 
	{
	  val = (unsigned int) *TIpd;
#ifndef VXWORKS
	  val = LSWAP(val);
#endif
	  if(val==TI_EMPTY_FIFO)
	    break;
#ifndef VXWORKS
	  val = LSWAP(val);
#endif
	  data[ii] = val;
	  ii++;
	}
      ii++;
      dCnt += ii;

      TIUNLOCK;
      return(dCnt);
    }

  TIUNLOCK;

  return OK;
}

/*******************************************************************************
 *
 * tiReadTriggerBlock - Read a block from the TI and form it into a 
 *                      CODA Trigger Bank
 *
 *    data  - local memory address to place data
 *    nwrds - Max number of words to transfer
 *    rflag - Readout Flag
 *              0 - programmed I/O from the specified board
 *              1 - DMA transfer using Universe/Tempe DMA Engine 
 *                    (DMA VME transfer Mode must be setup prior)
 *
 * RETURNS: Number of words transferred to data if successful, ERROR otherwise
 *
 */


int
tiReadTriggerBlock(volatile unsigned int *data, int nwrds, int rflag)
{
  int rval=0;
  int iword=0;
  unsigned int word=0;
  int iblkhead=-1, iblktrl=-1;


  if(data==NULL) 
    {
      logMsg("\ntiReadTriggerBlock: ERROR: Invalid Destination address\n",0,0,0,0,0,0);
      return(ERROR);
    }

  /* Obtain the trigger bank by just making a call the tiReadBlock */
  rval = tiReadBlock(data, nwrds, rflag);
  if(rval < 0)
    {
      /* Error occurred */
      return ERROR;
    }
  else if (rval == 0)
    {
      /* No data returned */
      return 0; 
    }
    
  /* Work down to find index of block header */
  while(iword<rval)
    { 

      word = data[iword];
#ifndef VXWORKS
      word = LSWAP(word);
#endif
      if(word & TI_DATA_TYPE_DEFINE_MASK)
	{
	  if(((word & TI_WORD_TYPE_MASK)>>27) == 0)
	    {
	      iblkhead = iword;
	      break;
	    }
	}     
      iword++;
    }

  /* Check if the index is valid */
  if(iblkhead == -1)
    {
      printf("%s: ERROR: Failed to find TI Block Header\n",
	     __FUNCTION__);
      return ERROR;
    }
  if(iblkhead != 0)
    {
      printf("%s: WARN: Invalid index (%d) for the TI Block header.\n",
	     __FUNCTION__,iblkhead);
    }

  /* Work up to find index of block trailer */
  iword=rval-1;
  while(iword>=0)
    { 

      word = data[iword];
#ifndef VXWORKS
      word = LSWAP(word);
#endif
      if(word & TI_DATA_TYPE_DEFINE_MASK)
	{
	  if(((word & TI_WORD_TYPE_MASK)>>27) == 1)
	    {
#ifdef CDEBUG
	      printf("%s: block trailer? 0x%08x\n",
		     __FUNCTION__,word);
#endif
	      iblktrl = iword;
	      break;
	    }
	}     
      iword--;
    }

  /* Check if the index is valid */
  if(iblktrl == -1)
    {
      printf("%s: ERROR: Failed to find TI Block Trailer\n",
	     __FUNCTION__);
      return ERROR;
    }

  /* Get the block trailer, and check the number of words contained in it */
  word = data[iblktrl];
#ifndef VXWORKS
  word = LSWAP(word);
#endif
  if((iblktrl - iblkhead + 1) != (word & 0x3fffff))
    {
      printf("%s: Number of words inconsistent (index count = %d, block trailer count = %d",
	     __FUNCTION__,(iblktrl - iblkhead + 1), word & 0x3fffff);
      return ERROR;
    }

  /* Modify the total words returned */
  rval = iblktrl - iblkhead;

  /* Write in the Trigger Bank Length */
#ifdef VXWORKS
  data[iblkhead] = rval-1;
#else
  data[iblkhead] = LSWAP(rval-1);
#endif

  if(tiSwapTriggerBlock==1)
    {
      for(iword=iblkhead; iword<rval; iword++)
	{
	  word = data[iword];
	  data[iword] = LSWAP(word);
	}
    }

  return rval;

}

/*******************************************************************************
 *
 * tiEnableFiber / tiDisableFiber
 *  - Enable/Disable Fiber transceiver
 *    fiber: integer indicative of the transceiver to enable / disable
 *
 *  Note:  All Fiber are enabled by default 
 *         (no harm, except for 1-2W power usage)
 *
 * RETURNS: OK if successful, ERROR otherwise.
 *
 */


int
tiEnableFiber(unsigned int fiber)
{
  unsigned int sval;
  unsigned int fiberbit;

  if(TIp==NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  if((fiber<1) | (fiber>8))
    {
      printf("%s: ERROR: Invalid value for fiber (%d)\n",
	     __FUNCTION__,fiber);
      return ERROR;
    }

  fiberbit = (1<<(fiber-1));

  TILOCK;
  sval = vmeRead32(&TIp->fiber);
  vmeWrite32(&TIp->fiber,
	     sval | fiberbit );
  TIUNLOCK;

  return OK;
  
}


int
tiDisableFiber(unsigned int fiber)
{
  unsigned int rval;
  unsigned int fiberbit;

  if(TIp==NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  if((fiber<1) | (fiber>8))
    {
      printf("%s: ERROR: Invalid value for fiber (%d)\n",
	     __FUNCTION__,fiber);
      return ERROR;
    }

  fiberbit = (1<<(fiber-1));

  TILOCK;
  rval = vmeRead32(&TIp->fiber);
  vmeWrite32(&TIp->fiber,
	   rval & ~fiberbit );
  TIUNLOCK;

  return rval;
  
}

/*******************************************************************************
 *
 * tiSetBusySource
 *  - Set the busy source with a given sourcemask
 *    sourcemask bits: 
 *             N: FILL THIS IN
 *
 *    rFlag - decision to reset the global source flags
 *             0: Keep prior busy source settings and set new "sourcemask"
 *             1: Reset, using only that specified with "sourcemask"
*/

int
tiSetBusySource(unsigned int sourcemask, int rFlag)
{
  unsigned int busybits=0;

  if(TIp==NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }
  
  if(sourcemask>TI_BUSY_SOURCEMASK)
    {
      printf("%s: ERROR: Invalid value for sourcemask (0x%x)\n",
	     __FUNCTION__, sourcemask);
      return ERROR;
    }

  if(sourcemask & TI_BUSY_P2_TRIGGER_INPUT)
    {
      printf("%s: ERROR: Do not use this routine to set P2 Busy as a trigger input.\n",
	     __FUNCTION__);
      return ERROR;
    }

  TILOCK;
  if(rFlag)
    {
      /* Read in the previous value , resetting previous BUSYs*/
      busybits = vmeRead32(&TIp->busy) & ~(TI_BUSY_SOURCEMASK);
    }
  else
    {
      /* Read in the previous value , keeping previous BUSYs*/
      busybits = vmeRead32(&TIp->busy);
    }

  busybits |= sourcemask;

  vmeWrite32(&TIp->busy, busybits);
  TIUNLOCK;

  return OK;

}

/*******************************************************************************
 *
 * tiEnableBusError / tiDisableBusError
 *  - Enable/Disable Bus Errors to terminate Block Reads
 */

void
tiEnableBusError()
{

  if(TIp==NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return;
    }

  TILOCK;
  vmeWrite32(&TIp->vmeControl,
	   vmeRead32(&TIp->vmeControl) | (TI_VMECONTROL_BERR) );
  TIUNLOCK;

}


void
tiDisableBusError()
{

  if(TIp==NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return;
    }

  TILOCK;
  vmeWrite32(&TIp->vmeControl,
	   vmeRead32(&TIp->vmeControl) & ~(TI_VMECONTROL_BERR) );
  TIUNLOCK;

}


/*******************************************************************************
 *
 * tiPayloadPort2VMESlot
 *  - Routine to return the VME slot, provided the VXS payload port
 *  - This does not access the bus, just a map in the library.
 *
 */
int
tiPayloadPort2VMESlot(int payloadport)
{
  int rval=0;
  int islot;
  if(payloadport<1 || payloadport>18)
    {
      printf("%s: ERROR: Invalid payloadport %d\n",
	     __FUNCTION__,payloadport);
      return ERROR;
    }

  for(islot=1;islot<MAX_VME_SLOTS;islot++)
    {
      if(payloadport == PayloadPort[islot])
	{
	  rval = islot;
	  break;
	}
    }

  if(rval==0)
    {
      printf("%s: ERROR: Unable to find VME Slot from Payload Port %d\n",
	     __FUNCTION__,payloadport);
      rval=ERROR;
    }

  return rval;
}

/*******************************************************************************
 *
 *  tiVMESlot2PayloadPort
 *  - Routine to return the VXS Payload Port provided the VME slot
 *  - This does not access the bus, just a map in the library.
 */
int
tiVMESlot2PayloadPort(int vmeslot)
{
  int rval=0;
  if(vmeslot<1 || vmeslot>MAX_VME_SLOTS) 
    {
      printf("%s: ERROR: Invalid VME slot %d\n",
	     __FUNCTION__,vmeslot);
      return ERROR;
    }

  rval = (int)PayloadPort[vmeslot];

  if(rval==0)
    {
      printf("%s: ERROR: Unable to find Payload Port from VME Slot %d\n",
	     __FUNCTION__,vmeslot);
      rval=ERROR;
    }

  return rval;

}


/*******************************************************************************
 *
 *  tiSetPrescale - Set the prescale factor for the external trigger
 *
 *     prescale: Factor for prescale.  
 *               Max {prescale} available is 65535
 *
 *  RETURNS: OK if successful, otherwise ERROR.
 *
 */


int
tiSetPrescale(int prescale)
{
  if(TIp==NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  if(prescale<0 || prescale>0xffff)
    {
      printf("%s: ERROR: Invalid prescale (%d).  Must be between 0 and 65535.",
	     __FUNCTION__,prescale);
      return ERROR;
    }

  TILOCK;
  vmeWrite32(&TIp->trig1Prescale, prescale);
  TIUNLOCK;

  return OK;
}


int
tiGetPrescale()
{
  int rval;
  if(TIp==NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  rval = vmeRead32(&TIp->trig1Prescale);
  TIUNLOCK;

  return rval;
}

/*******************************************************************************
 *
 *  tiSetTriggerPulse - Set the characteristics of a specified trigger
 *
 *   trigger:  
 *           1: set for trigger 1
 *           2: set for trigger 2 (playback trigger)
 *   delay:    delay in units of 4ns
 *   width:    pulse width in units of 4ns
 *
 * RETURNS: OK if successful, otherwise ERROR
 *
 */

int
tiSetTriggerPulse(int trigger, int delay, int width)
{
  unsigned int rval=0;
  if(TIp==NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  if(trigger<1 || trigger>2)
    {
      printf("%s: ERROR: Invalid trigger (%d).  Must be 1 or 2.\n",
	     __FUNCTION__,trigger);
      return ERROR;
    }
  if(delay<0 || delay>TI_TRIGDELAY_TRIG1_DELAY_MASK)
    {
      printf("%s: ERROR: Invalid delay (%d).  Must be less than %d\n",
	     __FUNCTION__,delay,TI_TRIGDELAY_TRIG1_DELAY_MASK);
      return ERROR;
    }
  if(width<0 || width>TI_TRIGDELAY_TRIG1_WIDTH_MASK)
    {
      printf("%s: ERROR: Invalid width (%d).  Must be less than %d\n",
	     __FUNCTION__,width,TI_TRIGDELAY_TRIG1_WIDTH_MASK);
    }

  TILOCK;
  if(trigger==1)
    {
      rval = vmeRead32(&TIp->trigDelay) & 
	~(TI_TRIGDELAY_TRIG1_DELAY_MASK | TI_TRIGDELAY_TRIG1_WIDTH_MASK) ;
      rval |= ( (delay) | (width<<8) );
      vmeWrite32(&TIp->trigDelay, rval);
    }
  if(trigger==2)
    {
      rval = vmeRead32(&TIp->trigDelay) & 
	~(TI_TRIGDELAY_TRIG2_DELAY_MASK | TI_TRIGDELAY_TRIG2_WIDTH_MASK) ;
      rval |= ( (delay<<16) | (width<<24) );
      vmeWrite32(&TIp->trigDelay, rval);
    }
  TIUNLOCK;
  
  return OK;
}

/*******************************************************************************
 *
 *  tiSetSyncDelayWidth
 *  - Set the delay time and width of the Sync signal
 *
 *  ARGS:
 *       delay:  the delay (latency) set in units of 4ns.
 *       width:  the width set in units of 4ns.
 *      twidth:  if this is non-zero, set width in units of 32ns.
 *
 */

void
tiSetSyncDelayWidth(unsigned int delay, unsigned int width, int widthstep)
{
  int twidth=0, tdelay=0;

  if(TIp == NULL) 
    {
      printf("%s: ERROR: TS not initialized\n",__FUNCTION__);
      return;
    }

  if(delay>TI_SYNCDELAY_MASK)
    {
      printf("%s: ERROR: Invalid delay (%d)\n",__FUNCTION__,delay);
      return;
    }

  if(width>TI_SYNCWIDTH_MASK)
    {
      printf("%s: WARN: Invalid width (%d).\n",__FUNCTION__,width);
      return;
    }

  if(widthstep)
    width |= TI_SYNCWIDTH_LONGWIDTH_ENABLE;

  tdelay = delay*4;
  if(widthstep)
    twidth = (width&TI_SYNCWIDTH_MASK)*32;
  else
    twidth = width*4;

  printf("%s: Setting Sync delay = %d (ns)   width = %d (ns)\n",
	 __FUNCTION__,tdelay,twidth);

  TILOCK;
  vmeWrite32(&TIp->syncDelay,delay);
  vmeWrite32(&TIp->syncWidth,width);
  TIUNLOCK;

}

/*******************************************************************************
 *
 *  tiTrigLinkReset
 *  - Reset the trigger link.
 *
 */

void 
tiTrigLinkReset()
{
  if(TIp == NULL) 
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return;
    }
  
  TILOCK;
  vmeWrite32(&TIp->syncCommand,TI_SYNCCOMMAND_TRIGGERLINK_DISABLE); 
  taskDelay(1);

  vmeWrite32(&TIp->syncCommand,TI_SYNCCOMMAND_TRIGGERLINK_DISABLE); 
  taskDelay(1);

  vmeWrite32(&TIp->syncCommand,TI_SYNCCOMMAND_TRIGGERLINK_ENABLE);
  taskDelay(1);

  TIUNLOCK;

  printf ("%s: Trigger Data Link was reset.\n",__FUNCTION__);
}

/*******************************************************************************
 *
 *  tiSyncReset
 *  - Generate a Sync Reset signal.  This signal is sent to the loopback and
 *    all configured TI Slaves.
 *
 */

void
tiSyncReset()
{
  if(TIp == NULL) 
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return;
    }
  
  TILOCK;
  vmeWrite32(&TIp->syncCommand,TI_SYNCCOMMAND_SYNCRESET); 
  vmeWrite32(&TIp->syncCommand,TI_SYNCCOMMAND_RESET_EVNUM); 
  taskDelay(1);
  TIUNLOCK;

}

/*******************************************************************************
 *
 *  tiSyncResetResync
 *  - Generate a Sync Reset Resync signal.  This signal is sent to the loopback and
 *    all configured TI Slaves.  This type of Sync Reset will NOT reset 
 *    event numbers
 *
 */

void
tiSyncResetResync()
{
  if(TIp == NULL) 
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return;
    }
  
  TILOCK;
  vmeWrite32(&TIp->syncCommand,TI_SYNCCOMMAND_SYNCRESET); 
  TIUNLOCK;

}

/*******************************************************************************
 *
 *  tiClockReset
 *  - Generate a Clock Reset signal.  This signal is sent to the loopback and
 *    all configured TI Slaves.
 *
 */

void
tiClockReset()
{
  unsigned int old_syncsrc=0;
  if(TIp == NULL) 
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return;
    }

  if(tiMaster!=1)
    {
      printf("%s: ERROR: TI is not the Master.  No Clock Reset.\n", __FUNCTION__);
      return;
    }
  
  TILOCK;
  /* Send a clock reset */
  vmeWrite32(&TIp->syncCommand,TI_SYNCCOMMAND_CLK250_RESYNC); 
  taskDelay(2);

  /* Store the old sync source */
  old_syncsrc = vmeRead32(&TIp->sync) & TI_SYNC_SOURCEMASK;
  /* Disable sync source */
  vmeWrite32(&TIp->sync, 0);
  taskDelay(2);

  /* Send another clock reset */
  vmeWrite32(&TIp->syncCommand,TI_SYNCCOMMAND_CLK250_RESYNC); 
  taskDelay(2);

  /* Re-enable the sync source */
  vmeWrite32(&TIp->sync, old_syncsrc);
  TIUNLOCK;

}

/*******************************************************************************
 *
 *  tiSetAdr32
 *  - Routine to set the A32 Base
 *
 */

int
tiSetAdr32(unsigned int a32base)
{
  unsigned long laddr=0;
  int res=0,a32Enabled=0;

  if(TIp == NULL) 
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  if(a32base<0x00800000)
    {
      printf("%s: ERROR: a32base out of range (0x%08x)\n",
	     __FUNCTION__,a32base);
      return ERROR;
    }

  TILOCK;
  vmeWrite32(&TIp->adr32, 
	     (a32base & TI_ADR32_BASE_MASK) );

  vmeWrite32(&TIp->vmeControl, 
	     vmeRead32(&TIp->vmeControl) | TI_VMECONTROL_A32);

  a32Enabled = vmeRead32(&TIp->vmeControl)&(TI_VMECONTROL_A32);
  if(!a32Enabled)
    {
      printf("%s: ERROR: Failed to enable A32 Address\n",__FUNCTION__);
      TIUNLOCK;
      return ERROR;
    }

#ifdef VXWORKS
  res = sysBusToLocalAdrs(0x09,(char *)a32base,(char **)&laddr);
  if (res != 0) 
    {
      printf("%s: ERROR in sysBusToLocalAdrs(0x09,0x%x,&laddr) \n",
	     __FUNCTION__,a32base);
      TIUNLOCK;
      return(ERROR);
    }
#else
  res = vmeBusToLocalAdrs(0x09,(char *)a32base,(char **)&laddr);
  if (res != 0) 
    {
      printf("%s: ERROR in vmeBusToLocalAdrs(0x09,0x%x,&laddr) \n",
	     __FUNCTION__,a32base);
      TIUNLOCK;
      return(ERROR);
    }
#endif

  tiA32Base = a32base;
  tiA32Offset = laddr - tiA32Base;
  TIpd = (unsigned int *)(laddr);  /* Set a pointer to the FIFO */
  TIUNLOCK;

  printf("%s: A32 Base address set to 0x%08x\n",
	 __FUNCTION__,tiA32Base);

  return OK;
}

/*******************************************************************************
 *
 * tiDisableA32
 *    - Disable A32
 *
 */

int
tiDisableA32()
{
  if(TIp == NULL) 
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }
  
  TILOCK;
  vmeWrite32(&TIp->adr32,0x0);
  vmeWrite32(&TIp->vmeControl, 
	     vmeRead32(&TIp->vmeControl) & ~TI_VMECONTROL_A32);
  TIUNLOCK;

  return OK;
}

/*******************************************************************************
 *
 *  tiResetEventCounter
 *  - Reset the L1A counter, as incremented by the TI.
 *
 */

int
tiResetEventCounter()
{
  if(TIp == NULL) 
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }
  
  TILOCK;
  vmeWrite32(&TIp->reset, TI_RESET_SCALERS_RESET);
  TIUNLOCK;

  return OK;
}

unsigned long long int
tiGetEventCounter()
{
  unsigned long long int rval=0;
  unsigned int lo=0, hi=0;

  if(TIp == NULL) 
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  lo = vmeRead32(&TIp->eventNumber_lo);
  hi = (vmeRead32(&TIp->eventNumber_hi) & TI_EVENTNUMBER_HI_MASK)>>16;

  rval = lo | ((unsigned long long)hi<<32);
  TIUNLOCK;
  
  return rval;
}

int
tiSetBlockLimit(unsigned int limit)
{
  if(TIp == NULL) 
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  vmeWrite32(&TIp->blocklimit,limit);
  TIUNLOCK;

  return OK;
}

unsigned int
tiGetBlockLimit()
{
  unsigned int rval=0;
  if(TIp == NULL) 
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  rval = vmeRead32(&TIp->blocklimit);
  TIUNLOCK;

  return rval;
}


/*******************************************************************************
 *
 *  tiBReady
 *   - Returns the number of Blocks available for readout
 *
 * RETURNS: Number of blocks available for readout if successful, otherwise ERROR
 *
 */

unsigned int
tiBReady()
{
  unsigned int blockBuffer, rval;

  if(TIp == NULL) 
    {
      logMsg("tiBReady: ERROR: TI not initialized\n",1,2,3,4,5,6);
      return 0;
    }

  TILOCK;
  blockBuffer = vmeRead32(&TIp->blockBuffer);
  rval        = (blockBuffer&TI_BLOCKBUFFER_BLOCKS_READY_MASK)>>8;
  tiSyncEventReceived = (blockBuffer&TI_BLOCKBUFFER_SYNCEVENT)>>31;

  if( (rval==1) && (tiSyncEventReceived) )
    tiSyncEventFlag = 1;
  else
    tiSyncEventFlag = 0;

  TIUNLOCK;

  return rval;
}

/*
 * tsGetSyncEventFlag
 *   - Return the value of the Synchronization flag, obtained from tsBReady
 *     1: if sync event received, and current blocks available = 1
 *     0: Otherwise
 *
 */

int
tiGetSyncEventFlag()
{
  int rval=0;
  
  TILOCK;
  rval = tiSyncEventFlag;
  TIUNLOCK;

  return rval;
}

/*
 * tiSyncEventReceived
 *  - Return the value of whether or not the sync event has been received
 *     1: if sync event received
 *     0: Otherwise
 *
 */

int
tiGetSyncEventReceived()
{
  int rval=0;
  
  TILOCK;
  rval = tiSyncEventReceived;
  TIUNLOCK;

  return rval;
}

/*******************************************************************************
 *
 *  tiEnableVXSSignals/tiDisableVXSSignals
 *   - Enable/Disable trigger and sync signals sent through the VXS
 *     to the Signal Distribution (SD) module.
 *     This may be required to eliminate the possibility of accidental
 *     signals being sent during Clock Synchronization or Trigger
 *     Enable/Disabling by the TI Master or TS.
 *
 * RETURNS: OK if successful, otherwise ERROR
 *
 */

int
tiEnableVXSSignals()
{
  if(TIp == NULL) 
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  vmeWrite32(&TIp->fiber,
	     (vmeRead32(&TIp->fiber) & 0xFF) | TI_FIBER_ENABLE_P0);
  TIUNLOCK;

  return OK;
}

int
tiDisableVXSSignals()
{
  if(TIp == NULL) 
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  vmeWrite32(&TIp->fiber,
	     (vmeRead32(&TIp->fiber) & 0xFF) & ~TI_FIBER_ENABLE_P0);
  TIUNLOCK;

  return OK;
}

/*******************************************************************************
 *
 *  tiSetBlockBufferLevel
 *   - Set the block buffer level for the number of blocks in the system
 *     that need to be read out.
 *
 *     If this buffer level is full, the TI will go BUSY.
 *     The BUSY is released as soon as the number of buffers in the system
 *     drops below this level.
 *
 *  ARG:     level: 
 *               0:  No Buffer Limit  -  Pipeline mode
 *               1:  One Block Limit - "ROC LOCK" mode
 *         2-65535:  "Buffered" mode.
 *
 * RETURNS: OK if successful, otherwise ERROR
 *
 */


int
tiSetBlockBufferLevel(unsigned int level)
{
  if(TIp == NULL) 
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  if(level>TI_BLOCKBUFFER_BUFFERLEVEL_MASK)
    {
      printf("%s: ERROR: Invalid value for level (%d)\n",
	     __FUNCTION__,level);
      return ERROR;
    }

  TILOCK;
  vmeWrite32(&TIp->blockBuffer, level);
  TIUNLOCK;

  return OK;
}

/*******************************************************************************
 *
 *   tidEnableTSInput / tidDisableTSInput
 *   - Enable/Disable trigger inputs labelled TS#1-6 on the Front Panel
 *     These inputs MUST be disabled if not connected.
 *
 *   ARGs:   inpMask:
 *       bits 0:  TS#1
 *       bits 1:  TS#2
 *       bits 2:  TS#3
 *       bits 3:  TS#4
 *       bits 4:  TS#5
 *       bits 5:  TS#6
 *
 */

int
tiEnableTSInput(unsigned int inpMask)
{
  if(TIp == NULL) 
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  if(inpMask>0x3f)
    {
      printf("%s: ERROR: Invalid inpMask (0x%x)\n",__FUNCTION__,inpMask);
      return ERROR;
    }

  TILOCK;
  vmeWrite32(&TIp->tsInput, inpMask);
  TIUNLOCK;

  return OK;
}

int
tiDisableTSInput(unsigned int inpMask)
{
  if(TIp == NULL) 
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  if(inpMask>0x3f)
    {
      printf("%s: ERROR: Invalid inpMask (0x%x)\n",__FUNCTION__,inpMask);
      return ERROR;
    }

  TILOCK;
  vmeWrite32(&TIp->tsInput, vmeRead32(&TIp->tsInput) & ~inpMask);
  TIUNLOCK;

  return OK;
}

/*******************************************************************************
 *
 *   tiSetOutputPort
 *   - Set (or unset) high level for the output ports on the front panel
 *     labelled as O#1-4
 *
 *   ARGs:   
 *       set1:  O#1
 *       set2:  O#2
 *       set3:  O#3
 *       set4:  O#4
 *
 */

int
tiSetOutputPort(unsigned int set1, unsigned int set2, unsigned int set3, unsigned int set4)
{
  unsigned int bits=0;
  if(TIp == NULL) 
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }
  
  if(set1)
    bits |= (1<<0);
  if(set2)
    bits |= (1<<1);
  if(set3)
    bits |= (1<<2);
  if(set4)
    bits |= (1<<3);

  TILOCK;
  vmeWrite32(&TIp->output, bits);
  TIUNLOCK;

  return OK;
}



/*******************************************************************************
 *
 *   tiSetClockSource
 *   - Set the clock to the specified source.
 *
 *   ARGs:   source:
 *            0:  Onboard clock
 *            1:  External clock (HFBR1 input)
 *
 */


int
tiSetClockSource(unsigned int source)
{
  int rval=OK;
  unsigned int clkset=0;
  unsigned int clkread=0;
  char sClock[20] = "";

  if(TIp == NULL) 
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  switch(source)
    {
    case 0: /* ONBOARD */
      clkset = TI_CLOCK_INTERNAL;
      sprintf(sClock,"ONBOARD (%d)",source);
      break;
    case 1: /* EXTERNAL (HFBR1) */
      clkset = TI_CLOCK_HFBR1;
      sprintf(sClock,"EXTERNAL-HFBR1 (%d)",source);
      break;
    default:
      printf("%s: ERROR: Invalid Clock Souce (%d)\n",__FUNCTION__,source);
      return ERROR;      
    }

  printf("%s: Setting clock source to %s\n",__FUNCTION__,sClock);


  TILOCK;
  vmeWrite32(&TIp->clock, clkset);
  /* Reset DCM (Digital Clock Manager) - 250/200MHz */
  vmeWrite32(&TIp->reset,TI_RESET_CLK250);
  taskDelay(1);
  /* Reset DCM (Digital Clock Manager) - 125MHz */
  vmeWrite32(&TIp->reset,TI_RESET_CLK125);
  taskDelay(1);

  if(source==1) /* Turn on running mode for External Clock verification */
    {
      vmeWrite32(&TIp->runningMode,TI_RUNNINGMODE_ENABLE);
      taskDelay(1);
      clkread = vmeRead32(&TIp->clock) & TI_CLOCK_MASK;
      if(clkread != clkset)
	{
	  printf("%s: ERROR Setting Clock Source (clkset = 0x%x, clkread = 0x%x)\n",
		 __FUNCTION__,clkset, clkread);
	  rval = ERROR;
	}
      vmeWrite32(&TIp->runningMode,TI_RUNNINGMODE_DISABLE);
    }
  TIUNLOCK;

  return rval;
}

int
tiGetClockSource()
{
  int rval=0;
  if(TIp == NULL) 
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  rval = vmeRead32(&TIp->clock) & 0x3;
  TIUNLOCK;

  return rval;
}

/*******************************************************************************
 *
 * tiSetFiberDelay
 *    - Set the fiber delay required to align the sync and triggers for all crates.
 *
 */
void
tiSetFiberDelay(unsigned int delay, unsigned int offset)
{
  unsigned int fiberLatency, syncDelay;
  if(TIp == NULL) 
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return;
    }

  fiberLatency=0;
  TILOCK;

  syncDelay = (offset-(delay));
  syncDelay=((syncDelay<<16)|(syncDelay<<8))&0xffff00;  /* set the sync delay according to the fiber latency */

  vmeWrite32(&TIp->fiberSyncDelay,syncDelay);

#ifdef STOPTHIS
  if(tiMaster != 1)  /* Slave only */
    {
      taskDelay(10);
      vmeWrite32(&TIp->reset,0x4000);  /* reset the IODELAY */
      taskDelay(10);
      vmeWrite32(&TIp->reset,0x800);   /* auto adjust the sync phase for HFBR#1 */
      taskDelay(10);
    }
#endif

  TIUNLOCK;

  printf("%s: Wrote 0x%08x to fiberSyncDelay\n",__FUNCTION__,syncDelay);

}

/*******************************************************************************
 *
 * tiAddSlave
 *    - Add and configurate a TI Slave for the TI Master.
 *      This routine should be used by the TI Master to configure
 *      HFBR porti and BUSY sources.
 *  ARGs:
 *     fiber:  The fiber port of the TI Master that is connected to the slave
 *
 */

int
tiAddSlave(unsigned int fiber)
{
  if(TIp == NULL) 
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  if(!tiMaster)
    {
      printf("%s: ERROR: TI is not the TI Master.\n",__FUNCTION__);
      return ERROR;
    }

  if((fiber<1) || (fiber>8) )
    {
      printf("%s: ERROR: Invalid value for fiber (%d)\n",
	     __FUNCTION__,fiber);
      return ERROR;
    }

  /* Add this slave to the global slave mask */
  tiSlaveMask |= (1<<(fiber-1));
  
  /* Add this fiber as a busy source (use first fiber macro as the base) */
  if(tiSetBusySource(TI_BUSY_HFBR1<<(fiber-1),0)!=OK)
    return ERROR;

  /* Enable the fiber */
  if(tiEnableFiber(fiber)!=OK)
    return ERROR;

  return OK;

}

/*******************************************************************************
 *
 * tiSetTriggerHoldoff
 *    - Set the value for a specified trigger rule.
 *
 *   ARGS: 
 *    rule  - the number of triggers within some time period..
 *            e.g. rule=1: No more than ONE trigger within the
 *                         specified time period
 *
 *    value - the specified time period (in steps of timestep)
 * timestep - 0: 16ns, 1: 500ns
 *
 *   RETURNS: OK if successful, otherwise ERROR.
 *
 */


int
tiSetTriggerHoldoff(int rule, unsigned int value, int timestep)
{
  unsigned int wval=0, rval=0;
  unsigned int maxvalue=0x3f;

  if(TIp == NULL) 
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  if( (rule<1) || (rule>5) )
    {
      printf("%s: ERROR: Invalid value for rule (%d).  Must be 1-4\n",
	     __FUNCTION__,rule);
      return ERROR;
    }
  if(value>maxvalue)
    {
      printf("%s: ERROR: Invalid value (%d). Must be less than %d.\n",
	     __FUNCTION__,value,maxvalue);
      return ERROR;
    }

  if(timestep)
    value |= (1<<7);

  /* Read the previous values */
  TILOCK;
  rval = vmeRead32(&TIp->triggerRule);
  
  switch(rule)
    {
    case 1:
      wval = value | (rval & ~TI_TRIGGERRULE_RULE1_MASK);
      break;
    case 2:
      wval = (value<<8) | (rval & ~TI_TRIGGERRULE_RULE2_MASK);
      break;
    case 3:
      wval = (value<<16) | (rval & ~TI_TRIGGERRULE_RULE3_MASK);
      break;
    case 4:
      wval = (value<<24) | (rval & ~TI_TRIGGERRULE_RULE4_MASK);
      break;
    }

  vmeWrite32(&TIp->triggerRule,wval);
  TIUNLOCK;

  return OK;

}

/*******************************************************************************
 *
 * tiGetTriggerHoldoff
 *    - Get the value for a specified trigger rule.
 *
 *   ARGS: 
 *    rule  - the number of triggers within some time period..
 *            e.g. rule=1: No more than ONE trigger within the
 *                         specified time period
 *
 *   RETURNS: If successful, returns the value (in steps of 16ns) 
 *            for the specified rule. ERROR, otherwise.
 *
 */


int
tiGetTriggerHoldoff(int rule)
{
  unsigned int rval=0;
  
  if(TIp == NULL) 
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }
  
  if(rule<1 || rule>5)
    {
      printf("%s: ERROR: Invalid value for rule (%d).  Must be 1 or 2.\n",
	     __FUNCTION__,rule);
      return ERROR;
    }

  TILOCK;
  rval = vmeRead32(&TIp->triggerRule);
  TIUNLOCK;
  
  switch(rule)
    {
    case 1:
      rval = (rval & TI_TRIGGERRULE_RULE1_MASK);
      break;

    case 2:
      rval = (rval & TI_TRIGGERRULE_RULE2_MASK)>>8;
      break;

    case 3:
      rval = (rval & TI_TRIGGERRULE_RULE3_MASK)>>16;
      break;

    case 4:
      rval = (rval & TI_TRIGGERRULE_RULE4_MASK)>>24;
      break;
    }

  return rval;

}

/*******************************************************************************
 *
 * tiDisableDataReadout()/ tiEnableDataReadout()
 *    - Disable/Enable the necessity to readout the TI for every block..
 *      For instances when the TI data is not required for analysis
 *
 */

int
tiDisableDataReadout()
{
  if(TIp == NULL) 
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }
  
  tiReadoutEnabled = 0;
  TILOCK;
  vmeWrite32(&TIp->vmeControl,
	     vmeRead32(&TIp->vmeControl) | TI_VMECONTROL_BUFFER_DISABLE);
  TIUNLOCK;
  
  printf("%s: Readout disabled.\n",__FUNCTION__);

  return OK;
}

int
tiEnableDataReadout()
{
  if(TIp == NULL) 
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }
  
  tiReadoutEnabled = 1;
  TILOCK;
  vmeWrite32(&TIp->vmeControl,
	     vmeRead32(&TIp->vmeControl) & ~TI_VMECONTROL_BUFFER_DISABLE);
  TIUNLOCK;

  printf("%s: Readout enabled.\n",__FUNCTION__);

  return OK;
}


/*******************************************************************************
 *
 * tiResetBlockReadout
 *    - Decrement the hardware counter for blocks available, effectively
 *      simulating a readout from the data fifo.
 *
 */

void
tiResetBlockReadout()
{
 
  if(TIp == NULL) 
    {
      logMsg("tiResetBlockReadout: ERROR: TI not initialized\n",1,2,3,4,5,6);
      return;
    }
 
  TILOCK;
  vmeWrite32(&TIp->reset,TI_RESET_BLOCK_READOUT);
  TIUNLOCK;

}

/*******************************************************************************
 *
 * tiLoadTriggerTable
 *    - Load a predefined trigger table (mapping TS inputs to trigger types).
 *   Modes available:
 *     mode 0:
 *	   TS#1,2,3,4,5 generates Trigger1 (physics trigger),
 *         TS#6 generates Trigger2 (playback trigger),
 *         No SyncEvent;
 *     mode 1:
 *         TS#1,2,3 generates Trigger1 (physics trigger), 
 *         TS#4,5,6 generates Trigger2 (playback trigger).  
 *         If both Trigger1 and Trigger2, they are SyncEvent;
 *     mode 2:
 *         TS#1,2,3,4,5 generates Trigger1 (physics trigger),
 *         TS#6 generates Trigger2 (playback trigger),
 *         If both Trigger1 and Trigger2, generates SyncEvent;
 *
 */

int
tiLoadTriggerTable(int mode)
{
  int ipat;

  unsigned int trigPattern[3][16] = 
    {
      { /* mode 0:
	   TS#1,2,3,4,5 generates Trigger1 (physics trigger),
	   TS#6 generates Trigger2 (playback trigger),
	   No SyncEvent;
	*/
	0x43424100, 0x47464544, 0x4b4a4948, 0x4f4e4d4c,
	0x53525150, 0x57565554, 0x5b5a5958, 0x5f5e5d5c,
	0x636261a0, 0x67666564, 0x6b6a6968, 0x6f6e6d6c,
	0x73727170, 0x77767574, 0x7b7a7978, 0x7f7e7d7c, 
      },
      { /* mode 1:
	   TS#1,2,3 generates Trigger1 (physics trigger), 
	   TS#4,5,6 generates Trigger2 (playback trigger).  
	   If both Trigger1 and Trigger2, they are SyncEvent;
	*/
	0x43424100, 0x47464544, 0xcbcac988, 0xcfcecdcc,
	0xd3d2d190, 0xd7d6d5d4, 0xdbdad998, 0xdfdedddc,
	0xe3e2e1a0, 0xe7e6e5e4, 0xebeae9a8, 0xefeeedec,
	0xf3f2f1b0, 0xf7f6f5f4, 0xfbfaf9b8, 0xfffefdfc, 
      },
      { /* mode 2:
	   TS#1,2,3,4,5 generates Trigger1 (physics trigger),
	   TS#6 generates Trigger2 (playback trigger),
	   If both Trigger1 and Trigger2, generates SyncEvent;
	*/
	0x43424100, 0x47464544, 0x4b4a4948, 0x4f4e4d4c,
	0x53525150, 0x57565554, 0x5b5a5958, 0x5f5e5d5c,
	0xe3e2e1a0, 0xe7e6e5e4, 0xebeae9e8, 0xefeeedec,
	0xf3f2f1f0, 0xf7f6f5f4, 0xfbfaf9f8, 0xfffefdfc 
      }
    };


  if(TIp == NULL) 
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  if(mode>2)
    {
      printf("%s: WARN: Invalid mode %d.  Using Trigger Table mode = 0\n",
	     __FUNCTION__,mode);
      mode=0;
    }
  
  TILOCK;
  for(ipat=0; ipat<16; ipat++)
    vmeWrite32(&TIp->trigTable[ipat], trigPattern[mode][ipat]);

  TIUNLOCK;

  return OK;
}

/*******************************************************************************
 *
 * tiLatchTimers
 *   - Latch the Busy and Live Timers.
 *     This routine should be called prior to a call to 
 *
 *   tiGetLiveTime
 *   tiGetBusyTime
 *
 */

int
tiLatchTimers()
{
  if(TIp == NULL) 
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  vmeWrite32(&TIp->reset, TI_RESET_SCALERS_LATCH);
  TIUNLOCK;

  return OK;
}

unsigned int
tiGetLiveTime()
{
  unsigned int rval=0;
  if(TIp == NULL) 
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  rval = vmeRead32(&TIp->livetime);
  TIUNLOCK;

  return rval;
}

unsigned int
tiGetBusyTime()
{
  unsigned int rval=0;
  if(TIp == NULL) 
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  rval = vmeRead32(&TIp->busytime);
  TIUNLOCK;

  return rval;
}

/*******************************************************************************
 *
 * tiLive
 *   - Calculate the live time (percentage) from the live and busy time scalers
 *
 *  ARGs: sflag : if > 0, then returns the integrated live time
 *
 *  RETURNS: live time as a 3 digit integer % (e.g. 987 = 98.7%)
 *
 */

int
tiLive(int sflag)
{
  int rval=0;
  float fval=0;
  unsigned int newBusy=0, newLive=0, newTotal=0;
  unsigned int live=0, total=0;
  static unsigned int oldLive=0, oldTotal=0;

  if(TIp == NULL) 
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  newLive = vmeRead32(&TIp->livetime);
  newBusy = vmeRead32(&TIp->busytime);

  newTotal = newLive+newBusy;

  if((sflag==0) && (oldTotal<newTotal))
    { /* Differential */
      live  = newLive - oldLive;
      total = newTotal - oldTotal;
    }
  else
    { /* Integrated */
      live = newLive;
      total = newTotal;
    }

  oldLive = newLive;
  oldTotal = newTotal;

  if(total>0)
    fval = 1000*(((float) live)/((float) total));

  rval = (int) fval;

  TIUNLOCK;

  return rval;
}


unsigned int
tiBlockStatus(int fiber, int pflag)
{
  unsigned int rval=0;
  char name[50];
  unsigned int nblocksReady, nblocksNeedAck;

  if(TIp == NULL) 
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  if(fiber>8)
    {
      printf("%s: ERROR: Invalid value (%d) for fiber\n",__FUNCTION__,fiber);
      return ERROR;

    }

  switch(fiber)
    {
    case 0:
      rval = (vmeRead32(&TIp->blockStatus[4]) & 0xFFFF0000)>>16;
      break;

    case 1:
    case 3:
    case 5:
    case 7:
      rval = (vmeRead32(&TIp->blockStatus[(fiber-1)/2]) & 0xFFFF);
      break;

    case 2:
    case 4:
    case 6:
    case 8: 
      rval = ( vmeRead32(&TIp->blockStatus[(fiber/2)-1]) & 0xFFFF0000 )>>16;
      break;
    }

  if(pflag)
    {
      nblocksReady   = rval & TI_BLOCKSTATUS_NBLOCKS_READY0;
      nblocksNeedAck = (rval & TI_BLOCKSTATUS_NBLOCKS_NEEDACK0)>>8;

      if(fiber==0)
	sprintf(name,"Loopback");
      else
	sprintf(name,"Fiber %d",fiber);

      printf("%s: %s : Blocks ready / need acknowledge: %d / %d\n",
	     __FUNCTION__, name,
	     nblocksReady, nblocksNeedAck);

    }

  return rval;
}


#ifdef NOTDONE
/*******************************************************************************
 *
 * tidVmeTrigger1
 *    - fire a single trigger 1 via VME
 *
 */

int
tidVmeTrigger1()
{
  if(TIp == NULL) 
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }
  
  TILOCK;
  vmeWrite32(&TIp->triggerCmdCode, 0x0018);
  TIUNLOCK;
  return OK;

}

/*******************************************************************************
 *
 * tidVmeTrigger2
 *    - fire a single trigger 2 via VME
 *
 */

int
tidVmeTrigger2()
{
  if(TIp == NULL) 
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }
  
  TILOCK;
/*   vmeWrite32(&TIp->triggerCmdCode, 0x0180); */
  vmeWrite32(&TIp->softTrig2,0x1| (1<<16));
  TIUNLOCK;
  return OK;

}
#endif

static void 
FiberMeas()
{
  int clksrc=0;
  unsigned int defaultDelay=0, fiberLatency, syncDelay;


  clksrc = tiGetClockSource();
  /* Check to be sure the TI has external HFBR1 clock enabled */
  if(clksrc != TI_CLOCK_HFBR1)
    {
      printf("%s: ERROR: Unable to measure fiber latency without HFBR1 as Clock Source\n",
	     __FUNCTION__);
      printf("\t Using default Fiber Sync Delay = %d (0x%x)",
	     defaultDelay, defaultDelay);

      TILOCK;
      vmeWrite32(&TIp->fiberSyncDelay,defaultDelay);
      TIUNLOCK;

      return;
    }

  TILOCK;
  vmeWrite32(&TIp->reset,TI_RESET_IODELAY); // reset the IODELAY
  taskDelay(1);
  vmeWrite32(&TIp->reset,TI_RESET_FIBER_AUTO_ALIGN);  // auto adjust the return signal phase
  taskDelay(1);
  vmeWrite32(&TIp->reset,TI_RESET_MEASURE_LATENCY);  // measure the fiber latency
  taskDelay(1);

  fiberLatency = vmeRead32(&TIp->fiberLatencyMeasurement);  //fiber latency measurement result
  printf("Software offset = 0x%08x (%d)\n",tiFiberLatencyOffset, tiFiberLatencyOffset);
  printf("Fiber Latency is 0x%08x\n",fiberLatency);
  printf("  Latency data = 0x%08x (%d ns)\n",(fiberLatency>>23), (fiberLatency>>23) * 4);


  vmeWrite32(&TIp->reset,TI_RESET_AUTOALIGN_HFBR1_SYNC);   // auto adjust the sync phase for HFBR#1
  taskDelay(1);

  fiberLatency = vmeRead32(&TIp->fiberLatencyMeasurement);  //fiber latency measurement result
  syncDelay = (tiFiberLatencyOffset-(((fiberLatency>>23)&0x1ff)>>1));
  syncDelay=(syncDelay<<8)&0xff00;  //set the sync delay according to the fiber latency
  taskDelay(1);

  vmeWrite32(&TIp->fiberSyncDelay,syncDelay);
  taskDelay(1);
  syncDelay = vmeRead32(&TIp->fiberSyncDelay);
  TIUNLOCK;

  printf (" \n The fiber latency of 0xA0 is: 0x%08x\n", fiberLatency);
  printf (" \n The sync latency of 0x50 is: 0x%08x\n",syncDelay);
}

int
tiSetUserSyncResetReceive(int enable)
{
  if(TIp == NULL) 
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  if(enable)
    vmeWrite32(&TIp->sync, (vmeRead32(&TIp->sync) & TI_SYNC_SOURCEMASK) | 
	       TI_SYNC_USER_SYNCRESET_ENABLED);
  else
    vmeWrite32(&TIp->sync, (vmeRead32(&TIp->sync) & TI_SYNC_SOURCEMASK) &
	       ~TI_SYNC_USER_SYNCRESET_ENABLED);
  TIUNLOCK;

  return OK;
}

int
tiGetLastSyncCodes(int pflag)
{
  int rval=0;
  if(TIp == NULL) 
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }


  TILOCK;
  if(tiMaster)
    rval = ((vmeRead32(&TIp->sync) & TI_SYNC_LOOPBACK_CODE_MASK)>>16) & 0xF;
  else
    rval = ((vmeRead32(&TIp->sync) & TI_SYNC_HFBR1_CODE_MASK)>>8) & 0xF;
  TIUNLOCK;

  if(pflag)
    {
      printf(" Last Sync Code received:  0x%x\n",rval);
    }

  return rval;
}

int
tiGetSyncHistoryBufferStatus(int pflag)
{
  int hist_status=0, rval=0;
  if(TIp == NULL) 
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  hist_status = vmeRead32(&TIp->sync) 
    & (TI_SYNC_HISTORY_FIFO_MASK);
  TIUNLOCK;

  switch(hist_status)
    {
    case TI_SYNC_HISTORY_FIFO_EMPTY:
      rval=0;
      if(pflag) printf("%s: Sync history buffer EMPTY\n",__FUNCTION__);
      break;

    case TI_SYNC_HISTORY_FIFO_HALF_FULL:
      rval=1;
      if(pflag) printf("%s: Sync history buffer HALF FULL\n",__FUNCTION__);
      break;

    case TI_SYNC_HISTORY_FIFO_FULL:
    default:
      rval=2;
      if(pflag) printf("%s: Sync history buffer FULL\n",__FUNCTION__);
      break;
    }

  return rval;

}

void
tiResetSyncHistory()
{
  if(TIp == NULL) 
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return;
    }

  TILOCK;
  vmeWrite32(&TIp->reset, TI_RESET_SYNC_HISTORY);
  TIUNLOCK;

}



void
tiUserSyncReset(int enable)
{
  if(TIp == NULL) 
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return;
    }

  TILOCK;
  if(enable)
    vmeWrite32(&TIp->syncCommand,TI_SYNCCOMMAND_SYNCRESET_HIGH); 
  else
    vmeWrite32(&TIp->syncCommand,TI_SYNCCOMMAND_SYNCRESET_LOW); 

  taskDelay(2);
  TIUNLOCK;

  printf("%s: User Sync Reset ",__FUNCTION__);
  if(enable)
    printf("HIGH\n");
  else
    printf("LOW\n");

}

void
tiPrintSyncHistory()
{
  unsigned int syncHistory=0;
  int count=0, code=1, valid=0, timestamp=0, overflow=0;
  if(TIp == NULL) 
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return;
    }
  
  while(code!=0)
    {
      TILOCK;
      syncHistory = vmeRead32(&TIp->syncHistory);
      TIUNLOCK;

      printf("     TimeStamp: Code (valid)\n");

      if(tiMaster)
	{
	  code  = (syncHistory & TI_SYNCHISTORY_LOOPBACK_CODE_MASK)>>10;
	  valid = (syncHistory & TI_SYNCHISTORY_LOOPBACK_CODE_VALID)>>14;
	}
      else
	{
	  code  = syncHistory & TI_SYNCHISTORY_HFBR1_CODE_MASK;
	  valid = (syncHistory & TI_SYNCHISTORY_HFBR1_CODE_VALID)>>4;
	}
      
      overflow  = (syncHistory & TI_SYNCHISTORY_TIMESTAMP_OVERFLOW)>>15;
      timestamp = (syncHistory & TI_SYNCHISTORY_TIMESTAMP_MASK)>>16;

      printf("%4d: %d %5d :  0x%x (%d)\n",
	     count,
	     overflow, timestamp, code, valid);
      count++;
      if(count>1024)
	{
	  printf("%s: More than expected in the Sync History Buffer... exitting\n",
		 __FUNCTION__);
	  break;
	}
    }
}


/*
 * tiSetSyncEventInterval
 *    - Set the value of the syncronization event interval
 *
 * Args: 
 *   blk_interval -
 *      Sync Event will occur in the last event of the set blk_interval (number of blocks)
 * 
 */

int
tiSetSyncEventInterval(int blk_interval)
{
  if(TIp == NULL) 
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  if(!tiMaster)
    {
      printf("%s: ERROR: TI is not the TI Master.\n",__FUNCTION__);
      return ERROR;
    }

  if(blk_interval>0xFFFF)
    {
      printf("%s: ERROR: Invalid value for blk_interval (%d)\n",
	     __FUNCTION__,blk_interval);
    }

  TILOCK;
  vmeWrite32(&TIp->syncEventCtrl, blk_interval);
  TIUNLOCK;

  return OK;
}


/*
 * tiForceSyncEvent
 *  - Force a sync event (type = 0).
 */

int
tiForceSyncEvent()
{
  if(TIp == NULL) 
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  if(!tiMaster)
    {
      printf("%s: ERROR: TI is not the TI Master.\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  vmeWrite32(&TIp->reset, TI_RESET_FORCE_SYNCEVENT);
  TIUNLOCK;

  return OK;
}

/********************************************************************************
 *
 * tiSyncResetRequest
 *
 *  - Sync Reset Request is sent to TI-Master or TS.  
 *
 *    This option is available for multicrate systems when the
 *    synchronization is suspect.  It should be exercised only during
 *    "sync events" where the requested sync reset will immediately
 *    follow all ROCs concluding their readout.
 *
 */

int
tiSyncResetRequest()
{
  if(TIp == NULL) 
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  tiDoSyncResetRequest=1;
  TIUNLOCK;

  return OK;
}

/********************************************************************************
 *
 * tiGetSyncResetRequest
 *
 *  - Determine if a TI has requested a Sync Reset
 *
 */

int
tiGetSyncResetRequest()
{
  int request=0;
  if(TIp == NULL) 
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  if(!tiMaster)
    {
      printf("%s: ERROR: TI is not the TI Master.\n",__FUNCTION__);
      return ERROR;
    }


  TILOCK;
  request = (vmeRead32(&TIp->blockBuffer) & TI_BLOCKBUFFER_SYNCRESET_REQUESTED)>>30;
  TIUNLOCK;

  return request;
}

/********************************************************************************
 *
 * tiFillToEndBlock
 *
 *  - Generate non-physics triggers until the current block is filled.
 *    This feature is useful for "end of run" situations.
 *
 */

int
tiFillToEndBlock()
{
  if(TIp == NULL) 
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  if(!tiMaster)
    {
      printf("%s: ERROR: TI is not the TI Master.\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  vmeWrite32(&TIp->reset, TI_RESET_FILL_TO_END_BLOCK);
  TIUNLOCK;

  return OK;
}

unsigned int
tiGetGTPBufferLength(int pflag)
{
  unsigned int rval=0;

  TILOCK;
  rval = vmeRead32(&TIp->GTPtriggerBufferLength);
  TIUNLOCK;

  if(pflag)
    printf("%s: 0x%08x\n",__FUNCTION__,rval);

  return rval;
}

/*************************************************************
 Library Interrupt/Polling routines
*************************************************************/

/*******************************************************************************
 *
 *  tiInt
 *  - Default interrupt handler
 *    Handles the TI interrupt.  Calls a user defined routine,
 *    if it was connected with tiIntConnect()
 *    
 */

static void
tiInt(void)
{
  tiIntCount++;

  INTLOCK;

  if (tiIntRoutine != NULL)	/* call user routine */
    (*tiIntRoutine) (tiIntArg);

  /* Acknowledge trigger */
  if(tiDoAck==1)
    {
      tiIntAck();
    }
  INTUNLOCK;

}

/*******************************************************************************
 *
 *  tiPoll
 *  - Default Polling Server Thread
 *    Handles the polling of latched triggers.  Calls a user
 *    defined routine if was connected with tiIntConnect.
 *
 */
#ifndef VXWORKS
static void
tiPoll(void)
{
  int tidata;
  int policy=0;
  struct sched_param sp;
/* #define DO_CPUAFFINITY */
#ifdef DO_CPUAFFINITY
  int j;
  cpu_set_t testCPU;

  if (pthread_getaffinity_np(pthread_self(), sizeof(testCPU), &testCPU) <0) 
    {
      perror("pthread_getaffinity_np");
    }
  printf("tiPoll: CPUset = ");
  for (j = 0; j < CPU_SETSIZE; j++)
    if (CPU_ISSET(j, &testCPU))
      printf(" %d", j);
  printf("\n");

  CPU_ZERO(&testCPU);
  CPU_SET(7,&testCPU);
  if (pthread_setaffinity_np(pthread_self(),sizeof(testCPU), &testCPU) <0) 
    {
      perror("pthread_setaffinity_np");
    }
  if (pthread_getaffinity_np(pthread_self(), sizeof(testCPU), &testCPU) <0) 
    {
      perror("pthread_getaffinity_np");
    }

  printf("tiPoll: CPUset = ");
  for (j = 0; j < CPU_SETSIZE; j++)
    if (CPU_ISSET(j, &testCPU))
      printf(" %d", j);

  printf("\n");


#endif
  
  /* Set scheduler and priority for this thread */
  policy=SCHED_FIFO;
  sp.sched_priority=40;
  printf("%s: Entering polling loop...\n",__FUNCTION__);
  pthread_setschedparam(pthread_self(),policy,&sp);
  pthread_getschedparam(pthread_self(),&policy,&sp);
  printf ("%s: INFO: Running at %s/%d\n",__FUNCTION__,
	  (policy == SCHED_FIFO ? "FIFO"
	   : (policy == SCHED_RR ? "RR"
	      : (policy == SCHED_OTHER ? "OTHER"
		 : "unknown"))), sp.sched_priority);  
  prctl(PR_SET_NAME,"tiPoll");

  while(1) 
    {

      pthread_testcancel();

      /* If still need Ack, don't test the Trigger Status */
      if(tiNeedAck>0) 
	{
	  continue;
	}

      tidata = 0;
	  
      tidata = tiBReady();
      if(tidata == ERROR) 
	{
	  printf("%s: ERROR: tiIntPoll returned ERROR.\n",__FUNCTION__);
	  break;
	}

      if(tidata && tiIntRunning)
	{
	  INTLOCK; 
	  tiDaqCount = tidata;
	  tiIntCount++;

	  if (tiIntRoutine != NULL)	/* call user routine */
	    (*tiIntRoutine) (tiIntArg);
	
	  /* Write to TI to Acknowledge Interrupt */	  
	  if(tiDoAck==1) 
	    {
	      tiIntAck();
	    }
	  INTUNLOCK;
	}
    
    }
  printf("%s: Read ERROR: Exiting Thread\n",__FUNCTION__);
  pthread_exit(0);

}
#endif 


/*******************************************************************************
 *
 *  tiStartPollingThread
 *  - Routine that launches tiPoll in its own thread 
 *
 */
#ifndef VXWORKS
static void
tiStartPollingThread(void)
{
  int pti_status;

  pti_status = 
    pthread_create(&tipollthread,
		   NULL,
		   (void*(*)(void *)) tiPoll,
		   (void *)NULL);
  if(pti_status!=0) 
    {						
      printf("%s: ERROR: TI Polling Thread could not be started.\n",
	     __FUNCTION__);	
      printf("\t pthread_create returned: %d\n",pti_status);
    }

}
#endif

/*******************************************************************************
 *
 *
 *  tiIntConnect 
 *  - Connect a user routine to the TI Interrupt or
 *    latched trigger, if polling.
 *
 */

int
tiIntConnect(unsigned int vector, VOIDFUNCPTR routine, unsigned int arg)
{
#ifndef VXWORKS
  int status;
#endif

  if(TIp == NULL) 
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return(ERROR);
    }


#ifdef VXWORKS
  /* Disconnect any current interrupts */
  if((intDisconnect(tiIntVec) !=0))
    printf("%s: Error disconnecting Interrupt\n",__FUNCTION__);
#endif

  tiIntCount = 0;
  tiAckCount = 0;
  tiDoAck = 1;

  /* Set Vector and Level */
  if((vector < 0xFF)&&(vector > 0x40)) 
    {
      tiIntVec = vector;
    }
  else
    {
      tiIntVec = TI_INT_VEC;
    }

  TILOCK;
  vmeWrite32(&TIp->intsetup, (tiIntLevel<<8) | tiIntVec );
  TIUNLOCK;

  switch (tiReadoutMode)
    {
    case TI_READOUT_TS_POLL:
    case TI_READOUT_EXT_POLL:
      break;

    case TI_READOUT_TS_INT:
    case TI_READOUT_EXT_INT:
#ifdef VXWORKS
      intConnect(INUM_TO_IVEC(tiIntVec),tiInt,arg);
#else
      status = vmeIntConnect (tiIntVec, tiIntLevel,
			      tiInt,arg);
      if (status != OK) 
	{
	  printf("%s: vmeIntConnect failed with status = 0x%08x\n",
		 __FUNCTION__,status);
	  return(ERROR);
	}
#endif  
      break;

    default:
      printf("%s: ERROR: TI Mode not defined (%d)\n",
	     __FUNCTION__,tiReadoutMode);
      return ERROR;
    }

  printf("%s: INFO: Interrupt Vector = 0x%x  Level = %d\n",
	 __FUNCTION__,tiIntVec,tiIntLevel);

  if(routine) 
    {
      tiIntRoutine = routine;
      tiIntArg = arg;
    }
  else
    {
      tiIntRoutine = NULL;
      tiIntArg = 0;
    }

  return(OK);

}

/*******************************************************************************
 *
 *
 *  tiIntDisconnect
 *  - Disable interrupts or kill the polling service thread
 *
 *
 */

int
tiIntDisconnect()
{
#ifndef VXWORKS
  int status;
  void *res;
#endif

  if(TIp == NULL) 
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  if(tiIntRunning) 
    {
      logMsg("tiIntDisconnect: ERROR: TI is Enabled - Call tiIntDisable() first\n",
	     1,2,3,4,5,6);
      return ERROR;
    }

  INTLOCK;

  switch (tiReadoutMode) 
    {
    case TI_READOUT_TS_INT:
    case TI_READOUT_EXT_INT:

#ifdef VXWORKS
      /* Disconnect any current interrupts */
      sysIntDisable(tiIntLevel);
      if((intDisconnect(tiIntVec) !=0))
	printf("%s: Error disconnecting Interrupt\n",__FUNCTION__);
#else
      status = vmeIntDisconnect(tiIntLevel);
      if (status != OK) 
	{
	  printf("vmeIntDisconnect failed\n");
	}
#endif
      break;

    case TI_READOUT_TS_POLL:
    case TI_READOUT_EXT_POLL:
#ifndef VXWORKS
      if(tipollthread) 
	{
	  if(pthread_cancel(tipollthread)<0) 
	    perror("pthread_cancel");
	  if(pthread_join(tipollthread,&res)<0)
	    perror("pthread_join");
	  if (res == PTHREAD_CANCELED)
	    printf("%s: Polling thread canceled\n",__FUNCTION__);
	  else
	    printf("%s: ERROR: Polling thread NOT canceled\n",__FUNCTION__);
	}
#endif
      break;
    default:
      break;
    }

  INTUNLOCK;

  printf("%s: Disconnected\n",__FUNCTION__);

  return OK;
  
}

/*******************************************************************************
 *
 *  tiAckConnect
 *  - Connect a user routine to be executed instead of the default 
 *  TI interrupt/trigger latching acknowledge prescription
 *
 */

int
tiAckConnect(VOIDFUNCPTR routine, unsigned int arg)
{
  if(routine)
    {
      tiAckRoutine = routine;
      tiAckArg = arg;
    }
  else
    {
      printf("%s: WARN: routine undefined.\n",__FUNCTION__);
      tiAckRoutine = NULL;
      tiAckArg = 0;
      return ERROR;
    }
  return OK;
}

/*******************************************************************************
 *
 *  tiIntAck
 *  - Acknowledge an interrupt or latched trigger.  This "should" effectively 
 *  release the "Busy" state of the TI.
 *  Execute a user defined routine, if it is defined.  Otherwise, use
 *  a default prescription.
 *
 */

void
tiIntAck()
{
  int resetbits=0;
  if(TIp == NULL) {
    logMsg("tiIntAck: ERROR: TI not initialized\n",0,0,0,0,0,0);
    return;
  }

  if (tiAckRoutine != NULL)
    {
      /* Execute user defined Acknowlege, if it was defined */
      TILOCK;
      (*tiAckRoutine) (tiAckArg);
      TIUNLOCK;
    }
  else
    {
      TILOCK;
      tiDoAck = 1;
      tiAckCount++;
      resetbits = TI_RESET_BUSYACK;

      if(!tiReadoutEnabled)
	{
	  /* Readout Acknowledge and decrease the number of available blocks by 1 */
	  resetbits |= TI_RESET_BLOCK_READOUT;
	}
      
      if(tiDoSyncResetRequest)
	{
	  resetbits |= TI_RESET_SYNCRESET_REQUEST;
	  tiDoSyncResetRequest=0;
	}

      vmeWrite32(&TIp->reset, resetbits);
      TIUNLOCK;
    }

}

/*******************************************************************************
 *
 *  tiIntEnable
 *  - Enable interrupts or latching triggers (depending on set TI mode)
 *  
 *  if iflag==1, trigger counter will be reset
 *
 */


int
tiIntEnable(int iflag)
{
#ifdef VXWORKS
  int lock_key=0;
#endif

  if(TIp == NULL) 
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return(-1);
    }

  TILOCK;
  if(iflag == 1)
    {
      tiIntCount = 0;
      tiAckCount = 0;
    }

  tiIntRunning = 1;
  tiDoAck      = 1;
  tiNeedAck    = 0;

  switch (tiReadoutMode)
    {
    case TI_READOUT_TS_POLL:
    case TI_READOUT_EXT_POLL:
#ifndef VXWORKS
      tiStartPollingThread();
#endif
      break;

    case TI_READOUT_TS_INT:
    case TI_READOUT_EXT_INT:
#ifdef VXWORKS
      lock_key = intLock();
      sysIntEnable(tiIntLevel);
#endif
      printf("%s: ******* ENABLE INTERRUPTS *******\n",__FUNCTION__);
      vmeWrite32(&TIp->intsetup,
	       vmeRead32(&TIp->intsetup) | TI_INTSETUP_ENABLE );
      break;

    default:
      tiIntRunning = 0;
#ifdef VXWORKS
      if(lock_key)
	intUnlock(lock_key);
#endif
      printf("%s: ERROR: TI Readout Mode not defined %d\n",
	     __FUNCTION__,tiReadoutMode);
      TIUNLOCK;
      return(ERROR);
      
    }

  vmeWrite32(&TIp->runningMode,0x71);
  TIUNLOCK; /* Locks performed in tiEnableTriggerSource() */

  taskDelay(30);
  tiEnableTriggerSource();

#ifdef VXWORKS
  if(lock_key)
    intUnlock(lock_key);
#endif

  return(OK);

}

/*******************************************************************************
 *
 *  tiIntDisable
 *  - Disable interrupts or latching triggers
 *
*/

void 
tiIntDisable()
{

  if(TIp == NULL) 
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return;
    }

  tiDisableTriggerSource(1);

  TILOCK;
  vmeWrite32(&TIp->intsetup,
	     vmeRead32(&TIp->intsetup) & ~(TI_INTSETUP_ENABLE));
  vmeWrite32(&TIp->runningMode,0x0);
  tiIntRunning = 0;
  TIUNLOCK;
}

unsigned int
tiGetIntCount()
{
  return(tiIntCount);
}

int
tiGetSWBBusy()
{
  unsigned int rval=0;
  if(TIp == NULL) 
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return 0;
    }
  
  TILOCK;
  rval = vmeRead32(&TIp->busy) & (TI_BUSY_SWB<<16);

  printf("%s: busy = 0x%08x\n",
	 __FUNCTION__,vmeRead32(&TIp->busy));
  TIUNLOCK;

  return rval;
}

int
tiSetTokenTestMode(int mode)
{
  if(TIp == NULL) 
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  if(mode)
    vmeWrite32(&TIp->vmeControl,
	       vmeRead32(&TIp->vmeControl) | (TI_VMECONTROL_TOKEN_TESTMODE));
  else
    vmeWrite32(&TIp->vmeControl,
	       vmeRead32(&TIp->vmeControl) & ~(TI_VMECONTROL_TOKEN_TESTMODE));
  TIUNLOCK;

  return OK;

}

int
tiSetTokenOutTest(int level)
{
  if(TIp == NULL) 
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  if(level)
    vmeWrite32(&TIp->vmeControl,
	       vmeRead32(&TIp->vmeControl) | (TI_VMECONTROL_TOKENOUT_HI));
  else
    vmeWrite32(&TIp->vmeControl,
	       vmeRead32(&TIp->vmeControl) & ~(TI_VMECONTROL_TOKENOUT_HI));

  printf("%s: vmeControl = 0x%08x\n",__FUNCTION__,vmeRead32(&TIp->vmeControl));
  TIUNLOCK;

  return OK;

}
