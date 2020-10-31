/*----------------------------------------------------------------------------*/
/**
 * @mainpage
 * <pre>
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
 * </pre>
 *----------------------------------------------------------------------------*/

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
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
#include "../jvme/jvme.h"
#else
#include <sys/prctl.h>
#include <unistd.h>
#include "jvme.h"
#endif
#include <string.h>
#include <pthread.h>
#include "tiLib.h"

/* Mutex to guard TI read/writes */
pthread_mutex_t   tiMutex = PTHREAD_MUTEX_INITIALIZER;
int tiLockKey;
#ifndef VXWORKS
int intLock() {return 0;};
void intUnlock(int key) {};
#endif

#define TILOCK     {							\
    tiLockKey = intLock();						\
    if(pthread_mutex_lock(&tiMutex)<0) perror("pthread_mutex_lock");	\
  }
#define TIUNLOCK   {							\
    if(pthread_mutex_unlock(&tiMutex)<0) perror("pthread_mutex_unlock"); \
    intUnlock(tiLockKey);						\
  }

/* Global Variables */
volatile struct TI_A24RegStruct  *TIp=NULL;    /* pointer to TI memory map */
volatile        unsigned int     *TIpd=NULL;  /* pointer to TI data FIFO */
unsigned long tiA24Offset=0;                            /* Difference in CPU A24 Base and VME A24 Base */
unsigned int  tiA32Base  =0x08000000;                   /* Minimum VME A32 Address for use by TI */
unsigned long tiA32Offset=0;                            /* Difference in CPU A32 Base and VME A32 Base */
int tiMaster=1;                               /* Whether or not this TI is the Master */
int tiUseTsRev2=0;
int tiCrateID=0x59;                           /* Crate ID */
int tiBlockLevel=0;                           /* Current Block level for TI */
int tiNextBlockLevel=0;                       /* Next Block level for TI */
int tiBlockBufferLevel=0;                     /**< Current Block Buffer level for TI */
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
static int          tiFiberLatencyMeasurement = 0; /* Measured fiber latency */
static int          tiVersion     = 0x0;     /* Firmware version */
static int          tiSyncEventFlag = 0;     /* Sync Event/Block Flag */
static int          tiSyncEventReceived = 0; /* Indicates reception of sync event */
static int          tiNReadoutEvents = 0;    /* Number of events to readout from crate modules */
static int          tiTriggerMissed = 0;     /* Flag indicating that a trigger was missed, due to full fifo */
static int          tiDoSyncResetRequest =0; /* Option to request a sync reset during readout ack */
static int          tiSlotNumber=0;          /* Slot number in which the TI resides */
static int          tiSwapTriggerBlock=0;    /* Decision on whether or not to swap the trigger block endianness */
static int          tiBusError=0;            /* Bus Error block termination */
static int          tiSlaveFiberIn=1;        /* Which Fiber port to use when in Slave mode */
static int          tiNoVXS=0;               /* 1 if not in VXS crate */
static int          tiSyncResetType=TI_SYNCCOMMAND_SYNCRESET_4US;  /* Set default SyncReset Type to Fixed 4 us */
static int          tiFakeTriggerBank=1;
static int          tiUseGoOutput=1;
static int          tiUseEvTypeScalers=0;

static unsigned int tiTrigPatternData[16]=   /* Default Trigger Table to be loaded */
  { /* TS#1,2,3,4,5,6 generates Trigger1 (physics trigger),
       No Trigger2 (playback trigger),
       No SyncEvent;
    */
    0x43424100, 0x47464544, 0x4b4a4948, 0x4f4e4d4c,
    0x53525150, 0x57565554, 0x5b5a5958, 0x5f5e5d5c,
    0x63626160, 0x67666564, 0x6b6a6968, 0x6f6e6d6c,
    0x73727170, 0x77767574, 0x7b7a7978, 0x7f7e7d7c,
  };

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

static int FiberMeas();

/**
 * @defgroup PreInit Pre-Initialization
 * @defgroup SlavePreInit Slave Pre-Initialization
 *   @ingroup PreInit
 * @defgroup Config Initialization/Configuration
 * @defgroup MasterConfig Master Configuration
 *   @ingroup Config
 * @defgroup SlaveConfig Slave Configuration
 *   @ingroup Config
 * @defgroup Status Status
 * @defgroup MasterStatus Master Status
 *   @ingroup Status
 * @defgroup Readout Data Readout
 * @defgroup MasterReadout Master Data Readout
 *   @ingroup Readout
 * @defgroup IntPoll Interrupt/Polling
 * @defgroup Deprec Deprecated - To be removed
 */

/**
 * @ingroup PreInit
 * @brief Set the Fiber Latency Offset to be used during initialization
 *
 * @param flo fiber latency offset
 *
 * @return OK if successful, otherwise ERROR
 */
int
tiSetFiberLatencyOffset_preInit(int flo)
{
  if((flo<0) || (flo>0x1ff))
    {
      printf("%s: ERROR: Invalid Fiber Latency Offset (%d)\n",
	     __FUNCTION__,flo);
      return ERROR;
    }

  tiFiberLatencyOffset = flo;

  return OK;
}

/**
 * @ingroup PreInit
 * @brief Set the CrateID to be used during initialization
 *
 * @param cid Crate ID
 *
 * @return OK if successful, otherwise ERROR
 */
int
tiSetCrateID_preInit(int cid)
{
  if((cid<0) || (cid>0xff))
    {
      printf("%s: ERROR: Invalid Crate ID (%d)\n",
	     __FUNCTION__,cid);
      return ERROR;
    }

  tiCrateID = cid;

  return OK;
}

/**
 * @ingroup SlavePreInit
 *
 * @brief Set the Fiber In port to be used during initialization of TI Slave
 *
 * @param port Fiber In Port
 *
 * @return OK if successful, otherwise ERROR
 */
int
tiSetFiberIn_preInit(int port)
{
  if((port!=1) && (port!=5))
    {
      printf("%s: ERROR: Invalid Slave Fiber In Port (%d)\n",
	     __FUNCTION__,port);
      return ERROR;
    }

  tiSlaveFiberIn=port;

  return OK;
}


/**
 *  @ingroup Config
 *  @brief Initialize the TIp register space into local memory,
 *  and setup registers given user input
 *
 *
 *  @param tAddr  Address or Slot Number
 *     - A24 VME Address of the TI (0x000016 - 0xffffff)
 *     - Slot number of TI (1 - 21)
 *
 *  @param  mode  Readout/Triggering Mode
 *     - 0 External Trigger - Interrupt Mode
 *     - 1 TI/TImaster Trigger - Interrupt Mode
 *     - 2 External Trigger - Polling Mode
 *     - 3 TI/TImaster Trigger - Polling Mode
 *     - 4 TSrev2 Trigger + TRG - Interrupt Mode
 *     - 5 TSrev2 Trigger + TRG - Polling Mode
 *
 *  @param iFlag Initialization bit mask
 *     - 0   Do not initialize the board, just setup the pointers to the registers
 *     - 1   Use Slave Fiber 5, instead of 1
 *     - 2   Ignore firmware check
 *
 *  @return OK if successful, otherwise ERROR.
 *
 */

int
tiInit(unsigned int tAddr, unsigned int mode, int iFlag)
{
  unsigned long laddr;
  unsigned int rval, boardID, prodID, i2cread=0;
  unsigned int firmwareInfo;
  int stat;
  int noBoardInit=0, noFirmwareCheck=0;
  int supportedVersion = TI_SUPPORTED_FIRMWARE;
  int supportedType    = TI_SUPPORTED_TYPE;
  int tiFirmwareType;


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
  if(tAddr<22)
    {
      /* User enter slot number, shift it to VME A24 address */
      printf("%s: Initializing using slot number %d (VME address 0x%x)\n",
	     __FUNCTION__,
	     tAddr, tAddr<<19);
      tAddr = tAddr<<19;
    }

  if(iFlag&TI_INIT_NO_INIT)
    {
      noBoardInit = 1;
    }
  if(iFlag&TI_INIT_SLAVE_FIBER_5)
    {
      tiSlaveFiberIn=5;
    }
  if(iFlag&TI_INIT_SKIP_FIRMWARE_CHECK)
    {
      noFirmwareCheck=1;
    }


#ifdef VXWORKS
  stat = sysBusToLocalAdrs(0x39,(char *)tAddr,(char **)&laddr);
  if (stat != 0)
    {
      printf("%s: ERROR: Error in sysBusToLocalAdrs res=%d \n",__FUNCTION__,stat);
      return ERROR;
    }
  else
    {
      printf("TI address = 0x%.8lx\n",laddr);
    }
#else
  stat = vmeBusToLocalAdrs(0x39,(char *)(unsigned long)tAddr,(char **)&laddr);
  if (stat != 0)
    {
      printf("%s: ERROR: Error in vmeBusToLocalAdrs res=%d \n",__FUNCTION__,stat);
      return ERROR;
    }
  else
    {
      if(!noBoardInit)
	printf("TI VME (Local) address = 0x%.8x (0x%.8lx)\n",tAddr,laddr);
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

      /* Get the "production" type bits.  2=modTI, 1=production, 0=prototype */
      prodID = (rval&TI_BOARDID_PROD_MASK)>>16;

      /* Determine whether or not we'll need to swap the trigger block endianess */
      if( ((TIp->boardID & TI_BOARDID_TYPE_MASK)>>16) != TI_BOARDID_TYPE_TI)
	tiSwapTriggerBlock=1;
      else
	tiSwapTriggerBlock=0;

    }

  /* Check to see if we're in a VXS Crate */
  if((boardID==20) || (boardID==21))
    {
      /* Try reading the 'version' register in the SD */
      i2cread = vmeRead32(&TIp->SWB[(0x3C7C)/4]) & 0xFFFF; /* Device 1, Address 0x1F */

      if((i2cread!=0) && (i2cread!=0xffff))
	{ /* Valid response */
	  vmeSetMaximumVMESlots(boardID);
	  tiNoVXS=0;
	}
      else
	{
	  tiNoVXS=1;
	}
    }
  else
    tiNoVXS=1;

  /* Get the Firmware Information and print out some details */
  firmwareInfo = tiGetFirmwareVersion();
  if(firmwareInfo>0)
    {
      tiFirmwareType   = (firmwareInfo & TI_FIRMWARE_TYPE_MASK)>>12;

      tiVersion = firmwareInfo&0xFFF;
      printf("  ID: 0x%x \tFirmware (type - revision): 0x%X - 0x%03X (prodID = %d)\n",
	     (firmwareInfo&TI_FIRMWARE_ID_MASK)>>16, tiFirmwareType,
	     tiVersion,
	     prodID);

      if(tiFirmwareType != supportedType)
	{
	  if(noFirmwareCheck)
	    {
	      printf("%s: WARN: Firmware type (%d) not supported by this driver.\n  Supported type = %d  (IGNORED)\n",
		     __FUNCTION__,tiFirmwareType,supportedType);
	    }
	  else
	    {
	      printf("%s: ERROR: Firmware Type (%d) not supported by this driver.\n  Supported type = %d\n",
		     __FUNCTION__,tiFirmwareType,supportedType);
	      TIp=NULL;
	      return ERROR;
	    }
	}

      if(tiVersion < supportedVersion)
	{
	  if(noFirmwareCheck)
	    {
	      printf("%s: WARN: Firmware version (0x%x) not supported by this driver.\n  Supported version = 0x%x  (IGNORED)\n",
		     __FUNCTION__,tiVersion,supportedVersion);
	    }
	  else
	    {
	      printf("%s: ERROR: Firmware version (0x%x) not supported by this driver.\n  Supported version = 0x%x\n",
		     __FUNCTION__,tiVersion,supportedVersion);
	      TIp=NULL;
	      return ERROR;
	    }
	}
    }
  else
    {
      printf("%s:  ERROR: Invalid firmware 0x%08x\n",
	     __FUNCTION__,firmwareInfo);
      TIp=NULL;
      return ERROR;
    }

  /* Check if we should exit here, or initialize some board defaults */
  if(noBoardInit)
    {
      return OK;
    }


  /* Reset global library variables */
  tiBlockLevel=1;
  tiNextBlockLevel=1;
  tiBlockBufferLevel=1;

  tiIntCount = 0; tiAckCount = 0; tiDaqCount = 0;
  tiDoAck = 0; tiNeedAck = 0;

  tiReadoutEnabled = 1;

  tiSyncEventFlag = 0; tiSyncEventReceived = 0;
  tiNReadoutEvents = 0; tiDoSyncResetRequest = 0;
  tiTriggerMissed = 0;

  if((mode == TI_READOUT_TS_INT) || (mode == TI_READOUT_TS_POLL))
    tiMaster = 0;

  if(tiMaster==0) /* Reload only on the TI Slaves */
    {
      if(tiReload() == ERROR)
	{
	  printf("%s: ERROR returned from tiReload\n",
		 __func__);
	  return -1;
	}
    }

  tiDisableTriggerSource(0);
  tiDisableVXSSignals();

  /* Perform Soft Reset */
  tiReset();

  /* Set some defaults, dependent on Master/Slave status */
  tiReadoutMode = mode;

  switch(mode)
    {
    case TI_READOUT_EXT_INT:
    case TI_READOUT_EXT_POLL:
      printf("... Configure as TI Master...\n");
      /* Master (Supervisor) Configuration: takes in external triggers */
      tiMaster = 1;

      /* Clear the Slave Mask */
      tiSlaveMask = 0;

      /* BUSY from Loopback and Switch Slot B */
      if(tiNoVXS==1)
	tiSetBusySource(TI_BUSY_LOOPBACK,1);
      else
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
      if(tiNoVXS==1)
	tiSetBusySource(0,1);
      else
	tiSetBusySource(TI_BUSY_SWB,1);
      if(tiSlaveFiberIn==1)
	{
	  /* Enable HFBR#1 */
	  tiEnableFiber(1);
	  /* HFBR#1 Clock Source */
	  tiSetClockSource(1);
	  /* HFBR#1 Sync Source */
	  tiSetSyncSource(TI_SYNC_HFBR1);
	  /* HFBR#1 Trigger Source */
	  tiSetTriggerSource(TI_TRIGGER_HFBR1);
	}
      else if(tiSlaveFiberIn==5)
	{
	  /* Enable HFBR#5 */
	  tiEnableFiber(5);
	  /* HFBR#5 Clock Source */
	  tiSetClockSource(5);
	  /* HFBR#5 Sync Source */
	  tiSetSyncSource(TI_SYNC_HFBR5);
	  /* HFBR#5 Trigger Source */
	  tiSetTriggerSource(TI_TRIGGER_HFBR5);
	}
      break;

    case TI_READOUT_TSREV2_INT:
    case TI_READOUT_TSREV2_POLL:
      printf("... Configure as TI on TS (rev2) branch...\n");
      /*
	 TI on TS rev2 Branch Configuration:
	 - Trigger Type and Readout ACK on TS Branch ribbon cable
	 - L1A connected to FP-TRG input
      */
      tiMaster = 1;
      tiUseTsRev2 = 1;

      /* Clear the Slave Mask */
      tiSlaveMask = 0;

      /* BUSY from Loopback and Switch Slot B */
      if(tiNoVXS==1)
	tiSetBusySource(TI_BUSY_LOOPBACK,1);
      else
	tiSetBusySource(TI_BUSY_LOOPBACK | TI_BUSY_SWB,1);

      /* Onboard Clock Source */
      tiSetClockSource(TI_CLOCK_INTERNAL);

      /* Loopback Sync Source */
      tiSetSyncSource(TI_SYNC_LOOPBACK);

      tiUseBroadcastBufferLevel(0);

      break;

    default:
      printf("%s: ERROR: Invalid TI Mode %d\n",
	     __FUNCTION__,mode);
      return ERROR;
    }

  /* Setup some Other Library Defaults */
  /* Reset I2C engine */
  vmeWrite32(&TIp->reset,TI_RESET_I2C);

  /* Setup a default Sync Delay and Pulse width */
  if(tiMaster==1)
    tiSetSyncDelayWidth(0x54, 0x2f, 0);

  /* Set default sync delay (fiber compensation) */
  if(tiMaster==1)
    vmeWrite32(&TIp->fiberSyncDelay,
	       (tiFiberLatencyOffset<<16)&TI_FIBERSYNCDELAY_LOOPBACK_SYNCDELAY_MASK);

  /* Set Default Block Level to 1, and default crateID */
  if(tiMaster==1)
    tiSetBlockLevel(1);

  tiSetCrateID(tiCrateID);

  /* Set Event format for CODA 3.0 */
  tiSetEventFormat(3);

  /* Set Default Trig1 and Trig2 delay=16ns (0+1)*16ns, width=64ns (15+1)*4ns */
  tiSetTriggerPulse(1,0,15,0);
  tiSetTriggerPulse(2,0,15,0);

  /* Set the default prescale factor to 0 for rate/(0+1) */
  tiSetPrescale(0);

  /* Setup A32 data buffer with library default */
  tiSetAdr32(tiA32Base);

  /* Enable Bus Errors to complete Block transfers */
  tiEnableBusError();

  /* MGT reset */
  if(tiMaster==1)
    {
      tiResetMGT();
    }

  /* Set this to 1 (ROC Lock mode), by default. */
  tiSetBlockBufferLevel(1);

  /* Disable all TS Inputs */
  tiDisableTSInput(TI_TSINPUT_ALL);

  if(tiMaster != 1)
    {
      if(FiberMeas() == ERROR)
	{
	  printf("%s: Fiber Measurement failure.  Check fiber and/or fiber port,\n",
		 __FUNCTION__);
	  return -2;
	}
    }
  else
    {
      // TI IODELAY reset
      vmeWrite32(&TIp->reset,TI_RESET_IODELAY);
      tiWaitForIODelayReset(10);

      // TI Sync auto alignment
      vmeWrite32(&TIp->reset,TI_RESET_AUTOALIGN_HFBR1_SYNC);
      taskDelay(1);

      // Perform a trigger link reset
      tiTrigLinkReset();
      taskDelay(1);
    }

  return OK;
}

/**
 *  @ingroup Config
 *  @brief Find the TI within the prescribed "GEO Slot to A24 VME Address"
 *           range from slot 3 to 21.
 *
 *  @return A24 VME address if found.  Otherwise, 0
 */

unsigned int
tiFind()
{
  int islot, stat, tiFound=0;
  unsigned int tAddr, rval;
  unsigned long laddr;

  for(islot = 0; islot<20; islot++)
    {
      /* Form VME base address from slot number
       Start from slot 21 and 20, then go from 2 to 19 */
      switch(islot)
	{
	case 0:
	  tAddr = (21<<19);
	  break;
	case 1:
	  tAddr = (20<<19);
	  break;
	default:
	  tAddr = (islot<<19);
	}

#ifdef VXWORKS
      stat = sysBusToLocalAdrs(0x39,(char *)tAddr,(char **)&laddr);
#else
      stat = vmeBusToLocalAdrs(0x39,(char *)(unsigned long)tAddr,(char **)&laddr);
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
  unsigned long offset=0, expected=0, base=0;

  if(TIp==NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  printf("%s:\n\t ---------- Checking TI address space ---------- \n",__FUNCTION__);

  base = (unsigned long) &TIp->boardID;

  offset = ((unsigned long) &TIp->trigsrc) - base;
  expected = 0x20;
  if(offset != expected)
    printf("%s: ERROR TIp->triggerSource not at offset = 0x%lx (@ 0x%lx)\n",
	   __FUNCTION__,expected,offset);

  offset = ((unsigned long) &TIp->syncWidth) - base;
  expected = 0x80;
  if(offset != expected)
    printf("%s: ERROR TIp->syncWidth not at offset = 0x%lx (@ 0x%lx)\n",
	   __FUNCTION__,expected,offset);

  offset = ((unsigned long) &TIp->adr24) - base;
  expected = 0xD0;
  if(offset != expected)
    printf("%s: ERROR TIp->adr24 not at offset = 0x%lx (@ 0x%lx)\n",
	   __FUNCTION__,expected,offset);

  offset = ((unsigned long) &TIp->reset) - base;
  expected = 0x100;
  if(offset != expected)
    printf("%s: ERROR TIp->reset not at offset = 0x%lx (@ 0x%lx)\n",
	   __FUNCTION__,expected,offset);

  offset = ((unsigned long) &TIp->SWB_status) - base;
  expected = 0x2000;
  if(offset != expected)
    printf("%s: ERROR TIp->SWB_status not at offset = 0x%lx (@ 0x%lx)\n",
	   __FUNCTION__,expected,offset);

  offset = ((unsigned long) &TIp->SWA_status) - base;
  expected = 0x2800;
  if(offset != expected)
    printf("%s: ERROR TIp->SWA_status not at offset = 0x%lx (@ 0x%lx)\n",
	   __FUNCTION__,expected,offset);

  offset = ((unsigned long) &TIp->JTAGPROMBase[0]) - base;
  expected = 0x10000;
  if(offset != expected)
    printf("%s: ERROR TIp->JTAGPROMBase[0] not at offset = 0x%lx (@ 0x%lx)\n",
	   __FUNCTION__,expected,offset);

  offset = ((unsigned long) &TIp->JTAGFPGABase[0]) - base;
  expected = 0x20000;
  if(offset != expected)
    printf("%s: ERROR TIp->JTAGFPGABase[0] not at offset = 0x%lx (@ 0x%lx)\n",
	   __FUNCTION__,expected,offset);

  offset = ((unsigned long) &TIp->SWA) - base;
  expected = 0x30000;
  if(offset != expected)
    printf("%s: ERROR TIp->SWA not at offset = 0x%lx (@ 0x%lx)\n",
	   __FUNCTION__,expected,offset);

  offset = ((unsigned long) &TIp->SWB) - base;
  expected = 0x40000;
  if(offset != expected)
    printf("%s: ERROR TIp->SWB not at offset = 0x%lx (@ 0x%lx)\n",
	   __FUNCTION__,expected,offset);

  return OK;
}

/**
 * @ingroup Status
 * @brief Print some status information of the TI to standard out
 *
 * @param pflag if pflag>0, print out raw registers
 *
 */

void
tiStatus(int pflag)
{
  struct TI_A24RegStruct *ro;
  int iinp, ifiber;
  unsigned long TIBase;
  unsigned long long int l1a_count=0;

  if(TIp==NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return;
    }

  ro = (struct TI_A24RegStruct *) malloc(sizeof(struct TI_A24RegStruct));
  if(ro == NULL)
    {
      printf("%s: ERROR allocating memory for TI register structure\n",
	     __FUNCTION__);
      return;
    }

  /* latch live and busytime scalers */
  tiLatchTimers();
  l1a_count    = tiGetEventCounter();
  tiGetCurrentBlockLevel();

  TILOCK;
  ro->boardID      = vmeRead32(&TIp->boardID);
  ro->fiber        = vmeRead32(&TIp->fiber);
  ro->intsetup     = vmeRead32(&TIp->intsetup);
  ro->trigDelay    = vmeRead32(&TIp->trigDelay);
  ro->adr32        = vmeRead32(&TIp->adr32);
  ro->blocklevel   = vmeRead32(&TIp->blocklevel);
  ro->dataFormat   = vmeRead32(&TIp->dataFormat);
  ro->vmeControl   = vmeRead32(&TIp->vmeControl);
  ro->trigsrc      = vmeRead32(&TIp->trigsrc);
  ro->sync         = vmeRead32(&TIp->sync);
  ro->busy         = vmeRead32(&TIp->busy);
  ro->clock        = vmeRead32(&TIp->clock);
  ro->trig1Prescale = vmeRead32(&TIp->trig1Prescale);
  ro->blockBuffer  = vmeRead32(&TIp->blockBuffer);

  ro->tsInput      = vmeRead32(&TIp->tsInput);

  ro->output       = vmeRead32(&TIp->output);
  ro->syncEventCtrl= vmeRead32(&TIp->syncEventCtrl);
  ro->blocklimit   = vmeRead32(&TIp->blocklimit);
  ro->fiberSyncDelay = vmeRead32(&TIp->fiberSyncDelay);

  ro->GTPStatusA   = vmeRead32(&TIp->GTPStatusA);
  ro->GTPStatusB   = vmeRead32(&TIp->GTPStatusB);

  /* Latch scalers first */
  vmeWrite32(&TIp->reset,TI_RESET_SCALERS_LATCH);
  ro->livetime     = vmeRead32(&TIp->livetime);
  ro->busytime     = vmeRead32(&TIp->busytime);

  ro->inputCounter = vmeRead32(&TIp->inputCounter);

  ro->nblocks      = vmeRead32(&TIp->nblocks);

  ro->GTPtriggerBufferLength = vmeRead32(&TIp->GTPtriggerBufferLength);

  ro->rocEnable    = vmeRead32(&TIp->rocEnable);
  TIUNLOCK;

  TIBase = (unsigned long)TIp;

  printf("\n");
#ifdef VXWORKS
  printf("STATUS for TI at base address 0x%08x \n",
	 (unsigned int) TIp);
#else
  printf("STATUS for TI at VME (Local) base address 0x%08lx (0x%lx) \n",
	 (unsigned long) TIp - tiA24Offset, (unsigned long) TIp);
#endif
  printf("--------------------------------------------------------------------------------\n");

  printf(" Firmware revision: 0x%x\n",
	 tiVersion);

  printf(" A32 Data buffer ");
  if((ro->vmeControl&TI_VMECONTROL_A32) == TI_VMECONTROL_A32)
    {
      printf("ENABLED at ");
#ifdef VXWORKS
      printf("base address 0x%.8lx\n",
	     (unsigned long)TIpd);
#else
      printf("VME (Local) base address 0x%08lx (0x%lx)\n",
	     (unsigned long)TIpd - tiA32Offset, (unsigned long)TIpd);
#endif
    }
  else
    printf("DISABLED\n");

  if(tiMaster)
    if(tiUseTsRev2)
      {
	printf(" Configured as a TI Connected to TS rev2 Branch\n");
      }
    else
      printf(" Configured as a TI Master\n");
  else
    printf(" Configured as a TI Slave\n");

  printf(" Readout Count: %d\n",tiIntCount);
  printf("     Ack Count: %d\n",tiAckCount);
  printf("     L1A Count: %llu\n",l1a_count);
  printf("   Block Limit: %d   %s\n",ro->blocklimit,
	 (ro->blockBuffer & TI_BLOCKBUFFER_BUSY_ON_BLOCKLIMIT)?"* Finished *":"");
  printf("   Block Count: %d\n",ro->nblocks & TI_NBLOCKS_COUNT_MASK);

  if(pflag>0)
    {
      printf("\n");
      printf(" Registers (offset):\n");
      printf("  boardID        (0x%04lx) = 0x%08x\t", (unsigned long)&TIp->boardID - TIBase, ro->boardID);
      printf("  fiber          (0x%04lx) = 0x%08x\n", (unsigned long)(&TIp->fiber) - TIBase, ro->fiber);
      printf("  intsetup       (0x%04lx) = 0x%08x\t", (unsigned long)(&TIp->intsetup) - TIBase, ro->intsetup);
      printf("  trigDelay      (0x%04lx) = 0x%08x\n", (unsigned long)(&TIp->trigDelay) - TIBase, ro->trigDelay);
      printf("  adr32          (0x%04lx) = 0x%08x\t", (unsigned long)(&TIp->adr32) - TIBase, ro->adr32);
      printf("  blocklevel     (0x%04lx) = 0x%08x\n", (unsigned long)(&TIp->blocklevel) - TIBase, ro->blocklevel);
      printf("  dataFormat     (0x%04lx) = 0x%08x\t", (unsigned long)(&TIp->dataFormat) - TIBase, ro->dataFormat);
      printf("  vmeControl     (0x%04lx) = 0x%08x\n", (unsigned long)(&TIp->vmeControl) - TIBase, ro->vmeControl);
      printf("  trigger        (0x%04lx) = 0x%08x\t", (unsigned long)(&TIp->trigsrc) - TIBase, ro->trigsrc);
      printf("  sync           (0x%04lx) = 0x%08x\n", (unsigned long)(&TIp->sync) - TIBase, ro->sync);
      printf("  busy           (0x%04lx) = 0x%08x\t", (unsigned long)(&TIp->busy) - TIBase, ro->busy);
      printf("  clock          (0x%04lx) = 0x%08x\n", (unsigned long)(&TIp->clock) - TIBase, ro->clock);
      printf("  blockBuffer    (0x%04lx) = 0x%08x\t", (unsigned long)(&TIp->blockBuffer) - TIBase, ro->blockBuffer);

      printf("  output         (0x%04lx) = 0x%08x\n", (unsigned long)(&TIp->output) - TIBase, ro->output);
      printf("  fiberSyncDelay (0x%04lx) = 0x%08x\t", (unsigned long)(&TIp->fiberSyncDelay) - TIBase, ro->fiberSyncDelay);
      printf("  nblocks        (0x%04lx) = 0x%08x\n", (unsigned long)(&TIp->nblocks) - TIBase, ro->nblocks);

      printf("  GTPStatusA     (0x%04lx) = 0x%08x\t", (unsigned long)(&TIp->GTPStatusA) - TIBase, ro->GTPStatusA);
      printf("  GTPStatusB     (0x%04lx) = 0x%08x\n", (unsigned long)(&TIp->GTPStatusB) - TIBase, ro->GTPStatusB);

      printf("  livetime       (0x%04lx) = 0x%08x\t", (unsigned long)(&TIp->livetime) - TIBase, ro->livetime);
      printf("  busytime       (0x%04lx) = 0x%08x\n", (unsigned long)(&TIp->busytime) - TIBase, ro->busytime);
      printf("  GTPTrgBufLen   (0x%04lx) = 0x%08x\t", (unsigned long)(&TIp->GTPtriggerBufferLength) - TIBase, ro->GTPtriggerBufferLength);
      printf("\n");
    }
  printf("\n");

  if((!tiMaster) && (tiBlockLevel==0))
    {
      printf(" Block Level not yet received\n");
    }
  else
    {
      printf(" Block Level        = %d ", tiBlockLevel);
      if(tiBlockLevel != tiNextBlockLevel)
	printf("(To be set = %d)\n", tiNextBlockLevel);
      else
	printf("\n");
    }

  printf(" Block Buffer Level = ");
  if(ro->vmeControl & TI_VMECONTROL_USE_LOCAL_BUFFERLEVEL)
    {
      printf("%d -Local- ",
	     ro->blockBuffer & TI_BLOCKBUFFER_BUFFERLEVEL_MASK);
    }
  else
    {
      printf("%d -Broadcast- ",
	     (ro->dataFormat & TI_DATAFORMAT_BCAST_BUFFERLEVEL_MASK) >> 24);
    }

  printf("(%s)\n",(ro->vmeControl & TI_VMECONTROL_BUSY_ON_BUFFERLEVEL)?
	 "Busy Enabled":"Busy not enabled");

  if(tiMaster)
    {
      if(tiUseTsRev2)
	{
	  printf(" Sync Events to be received from TSrev2\n");
	}
      else
	{
	  if((ro->syncEventCtrl & TI_SYNCEVENTCTRL_NBLOCKS_MASK) == 0)
	    printf(" Sync Events DISABLED\n");
	  else
	    printf(" Sync Event period  = %d blocks\n",
		   ro->syncEventCtrl & TI_SYNCEVENTCTRL_NBLOCKS_MASK);
	}
    }

  printf("\n");

  if(!tiUseTsRev2)
    {
      printf(" Fiber Status         1     2     3     4     5     6     7     8\n");
      printf("                    ----- ----- ----- ----- ----- ----- ----- -----\n");
      printf("  Connected          ");
      for(ifiber=0; ifiber<8; ifiber++)
	{
	  printf("%s   ",
		 (ro->fiber & TI_FIBER_CONNECTED_TI(ifiber+1))?"YES":"   ");
	}
      printf("\n");
      if(tiMaster)
	{
	  printf("  Trig Src Enabled   ");
	  for(ifiber=0; ifiber<8; ifiber++)
	    {
	      printf("%s   ",
		     (ro->fiber & TI_FIBER_TRIGSRC_ENABLED_TI(ifiber+1))?"YES":"   ");
	    }
	}
      printf("\n\n");
    }

  printf(" Clock Source (%d) = \n",ro->clock & TI_CLOCK_MASK);
  switch(ro->clock & TI_CLOCK_MASK)
    {
    case TI_CLOCK_INTERNAL:
      printf("   Internal\n");
      break;

    case TI_CLOCK_HFBR5:
      printf("   HFBR #5 Input\n");
      break;

    case TI_CLOCK_HFBR1:
      printf("   HFBR #1 Input\n");
      break;

    case TI_CLOCK_FP:
      printf("   Front Panel\n");
      break;

    default:
      printf("   UNDEFINED!\n");
    }

  if(tiTriggerSource&TI_TRIGSRC_SOURCEMASK)
    {
      if(ro->trigsrc)
	printf(" Trigger input source (%s) =\n",
	       (ro->blockBuffer & TI_BLOCKBUFFER_BUSY_ON_BLOCKLIMIT)?"DISABLED on Block Limit":
	       "ENABLED");
      else
	printf(" Trigger input source (DISABLED) =\n");
      if(tiTriggerSource & TI_TRIGSRC_P0)
	printf("   P0 Input\n");
      if(tiTriggerSource & TI_TRIGSRC_HFBR1)
	printf("   HFBR #1 Input\n");
      if(tiTriggerSource & TI_TRIGSRC_HFBR5)
	printf("   HFBR #5 Input\n");
      if(tiTriggerSource & TI_TRIGSRC_LOOPBACK)
	printf("   Loopback\n");
      if(tiTriggerSource & TI_TRIGSRC_FPTRG)
	printf("   Front Panel TRG\n");
      if(tiTriggerSource & TI_TRIGSRC_VME)
	printf("   VME Command\n");
      if(tiTriggerSource & TI_TRIGSRC_TSINPUTS)
	printf("   Front Panel TS Inputs\n");
      if(tiTriggerSource & TI_TRIGSRC_TSREV2)
	printf("   Trigger Supervisor (rev2) + Front Panel TRG\n");
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

  if(ro->sync&TI_SYNC_SOURCEMASK)
    {
      printf(" Sync source = \n");
      if(ro->sync & TI_SYNC_P0)
	printf("   P0 Input\n");
      if(ro->sync & TI_SYNC_HFBR1)
	printf("   HFBR #1 Input\n");
      if(ro->sync & TI_SYNC_HFBR5)
	printf("   HFBR #5 Input\n");
      if(ro->sync & TI_SYNC_FP)
	printf("   Front Panel Input\n");
      if(ro->sync & TI_SYNC_LOOPBACK)
	printf("   Loopback\n");
      if(ro->sync & TI_SYNC_USER_SYNCRESET_ENABLED)
	printf("   User SYNCRESET Receieve Enabled\n");
    }
  else
    {
      printf(" No SYNC input source configured\n");
    }

  if(ro->busy&TI_BUSY_SOURCEMASK)
    {
      printf(" BUSY input source = \n");
      if(ro->busy & TI_BUSY_SWA)
	printf("   Switch Slot A    %s\n",(ro->busy&TI_BUSY_MONITOR_SWA)?"** BUSY **":"");
      if(ro->busy & TI_BUSY_SWB)
	printf("   Switch Slot B    %s\n",(ro->busy&TI_BUSY_MONITOR_SWB)?"** BUSY **":"");
      if(ro->busy & TI_BUSY_P2)
	printf("   P2 Input         %s\n",(ro->busy&TI_BUSY_MONITOR_P2)?"** BUSY **":"");
      if(ro->busy & TI_BUSY_TRIGGER_LOCK)
	printf("   Trigger Lock     \n");
      if(ro->busy & TI_BUSY_FP_FTDC)
	printf("   Front Panel TDC  %s\n",(ro->busy&TI_BUSY_MONITOR_FP_FTDC)?"** BUSY **":"");
      if(ro->busy & TI_BUSY_FP_FADC)
	printf("   Front Panel ADC  %s\n",(ro->busy&TI_BUSY_MONITOR_FP_FADC)?"** BUSY **":"");
      if(ro->busy & TI_BUSY_FP)
	printf("   Front Panel      %s\n",(ro->busy&TI_BUSY_MONITOR_FP)?"** BUSY **":"");
      if(ro->busy & TI_BUSY_LOOPBACK)
	printf("   Loopback         %s\n",(ro->busy&TI_BUSY_MONITOR_LOOPBACK)?"** BUSY **":"");
      if(ro->busy & TI_BUSY_HFBR1)
	printf("   HFBR #1          %s\n",(ro->busy&TI_BUSY_MONITOR_HFBR1)?"** BUSY **":"");
      if(ro->busy & TI_BUSY_HFBR2)
	printf("   HFBR #2          %s\n",(ro->busy&TI_BUSY_MONITOR_HFBR2)?"** BUSY **":"");
      if(ro->busy & TI_BUSY_HFBR3)
	printf("   HFBR #3          %s\n",(ro->busy&TI_BUSY_MONITOR_HFBR3)?"** BUSY **":"");
      if(ro->busy & TI_BUSY_HFBR4)
	printf("   HFBR #4          %s\n",(ro->busy&TI_BUSY_MONITOR_HFBR4)?"** BUSY **":"");
      if(ro->busy & TI_BUSY_HFBR5)
	printf("   HFBR #5          %s\n",(ro->busy&TI_BUSY_MONITOR_HFBR5)?"** BUSY **":"");
      if(ro->busy & TI_BUSY_HFBR6)
	printf("   HFBR #6          %s\n",(ro->busy&TI_BUSY_MONITOR_HFBR6)?"** BUSY **":"");
      if(ro->busy & TI_BUSY_HFBR7)
	printf("   HFBR #7          %s\n",(ro->busy&TI_BUSY_MONITOR_HFBR7)?"** BUSY **":"");
      if(ro->busy & TI_BUSY_HFBR8)
	printf("   HFBR #8          %s\n",(ro->busy&TI_BUSY_MONITOR_HFBR8)?"** BUSY **":"");
    }
  else
    {
      printf(" No BUSY input source configured\n");
    }
  if(tiMaster)
    {
      if(ro->tsInput & TI_TSINPUT_MASK)
	{
	  printf(" Front Panel TS Inputs Enabled: ");
	  for(iinp=0; iinp<6; iinp++)
	    {
	      if( (ro->tsInput & TI_TSINPUT_MASK) & (1<<iinp))
		printf(" %d",iinp+1);
	    }
	  printf("\n");
	}
      else
	{
	  printf(" All Front Panel TS Inputs Disabled\n");
	}
    }

  if(tiMaster && !tiUseTsRev2)
    {
      printf("\n");
      printf(" Trigger Rules:\n");
      tiPrintTriggerHoldoff(pflag);
    }

  if(tiMaster)
    {
      if(ro->rocEnable & TI_ROCENABLE_SYNCRESET_REQUEST_ENABLE_MASK)
	{
	  printf(" SyncReset Request ENABLED from ");

	  if(ro->rocEnable & (1 << 10))
	    {
	      printf("SELF ");
	    }

	  for(ifiber=0; ifiber<8; ifiber++)
	    {
	      if(ro->rocEnable & (1 << (ifiber + 1 + 10)))
		{
		  printf("%d ", ifiber + 1);
		}
	    }

	  printf("\n");
	}
      else
	{
	  printf(" SyncReset Requests DISABLED\n");
	}

      printf("\n");
      if(!tiUseTsRev2)
	{
	  tiSyncResetRequestStatus(1);
	}
    }
  printf("\n");

  if(ro->intsetup&TI_INTSETUP_ENABLE)
    printf(" Interrupts ENABLED\n");
  else
    printf(" Interrupts DISABLED\n");
  printf("   Level = %d   Vector = 0x%02x\n",
	 (ro->intsetup&TI_INTSETUP_LEVEL_MASK)>>8, (ro->intsetup&TI_INTSETUP_VECTOR_MASK));

  if(ro->vmeControl&TI_VMECONTROL_BERR)
    printf(" Bus Errors Enabled\n");
  else
    printf(" Bus Errors Disabled\n");

  printf("\n");
  printf(" Blocks ready for readout: %d\n",(ro->blockBuffer&TI_BLOCKBUFFER_BLOCKS_READY_MASK)>>8);

  printf("\n");
  printf(" Input counter %d\n",ro->inputCounter);

  if(tiMaster && (!tiUseTsRev2))
    tiSlaveStatus(pflag);

  printf("--------------------------------------------------------------------------------\n");
  printf("\n\n");

  if(ro)
    free(ro);

}

/**
 * @ingroup SlaveConfig
 * @brief This routine provides the ability to switch the port that the TI Slave
 *     receives its Clock, SyncReset, and Trigger.
 *     If the TI has already been configured to use this port, nothing is done.
 *
 *   @param port
 *      -  1  - Port 1
 *      -  5  - Port 5
 *
 * @return OK if successful, ERROR otherwise
 *
 */
int
tiSetSlavePort(int port)
{
 if(TIp==NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  if(tiMaster)
    {
      printf("%s: ERROR: TI is not the TI Slave.\n",__FUNCTION__);
      return ERROR;
    }

  if(tiUseTsRev2)
    {
      printf("%s: ERROR: Invalid usage with TS rev 2.\n",
	     __func__);
      return ERROR;
    }

  if((port!=1) && (port!=5))
    {
      printf("%s: ERROR: Invalid port specified (%d).  Must be 1 or 5 for TI Slave.\n",
	     __FUNCTION__,port);
      return ERROR;
    }

  if(port==tiSlaveFiberIn)
    {
      printf("%s: INFO: TI Slave already configured to use port %d.\n",
	     __FUNCTION__,port);
      return OK;
    }

  TILOCK;
  tiSlaveFiberIn=port;
  TIUNLOCK;

  if(tiSlaveFiberIn==1)
    {
      /* Enable HFBR#1 */
      tiEnableFiber(1);
      /* HFBR#1 Clock Source */
      tiSetClockSource(1);
      /* HFBR#1 Sync Source */
      tiSetSyncSource(TI_SYNC_HFBR1);
      /* HFBR#1 Trigger Source */
      tiSetTriggerSource(TI_TRIGGER_HFBR1);
    }
  else if(tiSlaveFiberIn==5)
    {
      /* Enable HFBR#5 */
      tiEnableFiber(5);
      /* HFBR#5 Clock Source */
      tiSetClockSource(5);
      /* HFBR#5 Sync Source */
      tiSetSyncSource(TI_SYNC_HFBR5);
      /* HFBR#5 Trigger Source */
      tiSetTriggerSource(TI_TRIGGER_HFBR5);
    }

  /* Measure and apply fiber compensation */
  if(FiberMeas() == ERROR)
    return ERROR;

  /* TI IODELAY reset */
  TILOCK;
  vmeWrite32(&TIp->reset,TI_RESET_IODELAY);
  tiWaitForIODelayReset(10);

  /* TI Sync auto alignment */
  if(tiSlaveFiberIn==1)
    vmeWrite32(&TIp->reset,TI_RESET_AUTOALIGN_HFBR1_SYNC);
  else
    vmeWrite32(&TIp->reset,TI_RESET_AUTOALIGN_HFBR5_SYNC);
  taskDelay(1);

  /* TI auto fiber delay measurement */
  vmeWrite32(&TIp->reset,TI_RESET_MEASURE_LATENCY);
  taskDelay(1);

  /* TI auto alignement fiber delay */
  vmeWrite32(&TIp->reset,TI_RESET_FIBER_AUTO_ALIGN);
  taskDelay(1);
  TIUNLOCK;

  printf("%s: INFO: TI Slave configured to use port %d.\n",
	 __FUNCTION__,port);
  return OK;
}

/**
 * @ingroup Status
 * @brief Returns the port of which the TI Slave has been configured (or will be)
 *
 * @return
 *       - 1  - Port 1
 *       - 5  - Port 5
 *
 */

int
tiGetSlavePort()
{
  return tiSlaveFiberIn;
}

/**
 * @ingroup Status
 * @brief Print a summary of all fiber port connections to potential TI Slaves
 *
 * @param  pflag
 *   -  0  - Default output
 *   -  1  - Print Raw Registers
 *
 */

void
tiSlaveStatus(int pflag)
{
  int iport=0, ibs=0, ifiber=0;
  unsigned long TIBase;
  unsigned int hfbr_tiID[8] = {1,2,3,4,5,6,7};
  unsigned int master_tiID;
  unsigned int blockStatus[5];
  unsigned int fiber=0, busy=0, trigsrc=0;
  int nblocksReady=0, nblocksNeedAck=0, slaveCount=0;
  int blocklevel=0;

  if(TIp==NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return;
    }

  if(tiUseTsRev2)
    {
      printf("%s: ERROR: Invalid usage with TS rev 2.\n",
	     __func__);
      return;
    }

  TILOCK;
  for(iport=0; iport<8; iport++)
    {
      hfbr_tiID[iport] = vmeRead32(&TIp->hfbr_tiID[iport]);
    }
  master_tiID = vmeRead32(&TIp->master_tiID);
  fiber       = vmeRead32(&TIp->fiber);
  busy        = vmeRead32(&TIp->busy);
  trigsrc     = vmeRead32(&TIp->trigsrc);
  for(ibs=0; ibs<4; ibs++)
    {
      blockStatus[ibs] = vmeRead32(&TIp->blockStatus[ibs]);
    }
  blockStatus[4] = vmeRead32(&TIp->adr24);

  blocklevel = (vmeRead32(&TIp->blocklevel) & TI_BLOCKLEVEL_CURRENT_MASK)>>16;

  TIUNLOCK;

  TIBase = (unsigned long)TIp;

  printf("\n");
  printf("TI-Master Port STATUS Summary\n");
  printf("--------------------------------------------------------------------------------\n");

  if(pflag>0)
    {
      printf(" Registers (offset):\n");
      printf("  TIBase     (0x%08x)\n",(unsigned int)(TIBase-tiA24Offset));
      printf("  busy           (0x%04lx) = 0x%08x\t", (unsigned long)(&TIp->busy) - TIBase, busy);
      printf("  fiber          (0x%04lx) = 0x%08x\n", (unsigned long)(&TIp->fiber) - TIBase, fiber);
      printf("  hfbr_tiID[0]   (0x%04lx) = 0x%08x\t", (unsigned long)(&TIp->hfbr_tiID[0]) - TIBase, hfbr_tiID[0]);
      printf("  hfbr_tiID[1]   (0x%04lx) = 0x%08x\n", (unsigned long)(&TIp->hfbr_tiID[1]) - TIBase, hfbr_tiID[1]);
      printf("  hfbr_tiID[2]   (0x%04lx) = 0x%08x\t", (unsigned long)(&TIp->hfbr_tiID[2]) - TIBase, hfbr_tiID[2]);
      printf("  hfbr_tiID[3]   (0x%04lx) = 0x%08x\n", (unsigned long)(&TIp->hfbr_tiID[3]) - TIBase, hfbr_tiID[3]);
      printf("  hfbr_tiID[4]   (0x%04lx) = 0x%08x\t", (unsigned long)(&TIp->hfbr_tiID[4]) - TIBase, hfbr_tiID[4]);
      printf("  hfbr_tiID[5]   (0x%04lx) = 0x%08x\n", (unsigned long)(&TIp->hfbr_tiID[5]) - TIBase, hfbr_tiID[5]);
      printf("  hfbr_tiID[6]   (0x%04lx) = 0x%08x\t", (unsigned long)(&TIp->hfbr_tiID[6]) - TIBase, hfbr_tiID[6]);
      printf("  hfbr_tiID[7]   (0x%04lx) = 0x%08x\n", (unsigned long)(&TIp->hfbr_tiID[7]) - TIBase, hfbr_tiID[7]);
      printf("  master_tiID    (0x%04lx) = 0x%08x\t", (unsigned long)(&TIp->master_tiID) - TIBase, master_tiID);

      printf("\n");
    }

  printf("                                                     Block Status\n");
  printf("Port  ROCID   Connected   TrigSrcEn   Busy Status   Ready / NeedAck  Blocklevel\n");
  printf("--------------------------------------------------------------------------------\n");
  /* Master first */
  /* Slot and Port number */
  printf("L     ");

  /* Port Name */
  printf("%5d      ",
	 (master_tiID&TI_ID_CRATEID_MASK)>>8);

  /* Connection Status */
  printf("%s      %s       ",
	 "YES",
	 (trigsrc & TI_TRIGSRC_LOOPBACK)?"ENABLED ":"DISABLED");

  /* Busy Status */
  printf("%s       ",
	 (busy & TI_BUSY_MONITOR_LOOPBACK)?"BUSY":"    ");

  /* Block Status */
  nblocksReady   = (blockStatus[4] & TI_BLOCKSTATUS_NBLOCKS_READY1)>>16;
  nblocksNeedAck = (blockStatus[4] & TI_BLOCKSTATUS_NBLOCKS_NEEDACK1)>>24;
  printf("  %3d / %3d",nblocksReady, nblocksNeedAck);
  printf("        %3d",blocklevel);
  printf("\n");

  /* Slaves last */
  for(iport=1; iport<9; iport++)
    {
      /* Only continue of this port has been configured as a slave */
      if((tiSlaveMask & (1<<(iport-1)))==0) continue;

      /* Slot and Port number */
      printf("%d     ", iport);

      /* Port Name */
      printf("%5d      ",
	     (hfbr_tiID[iport-1]&TI_ID_CRATEID_MASK)>>8);

      /* Connection Status */
      printf("%s      %s       ",
	     (fiber & TI_FIBER_CONNECTED_TI(iport))?"YES":"NO ",
	     (fiber & TI_FIBER_TRIGSRC_ENABLED_TI(iport))?"ENABLED ":"DISABLED");

      /* Busy Status */
      printf("%s       ",
	     (busy & TI_BUSY_MONITOR_FIBER_BUSY(iport))?"BUSY":"    ");

      /* Block Status */
      ifiber=iport-1;
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
      printf("  %3d / %3d",nblocksReady, nblocksNeedAck);

      printf("        %3d",(hfbr_tiID[iport-1]&TI_ID_BLOCKLEVEL_MASK)>>16);

      printf("\n");
      slaveCount++;
    }
  printf("\n");
  printf(" Total Slaves Added = %d\n",slaveCount);

}


/**
 * @ingroup Status
 * @brief Get the Firmware Version
 *
 * @return Firmware Version if successful, ERROR otherwise
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


/**
 * @ingroup Config
 * @brief Reload the firmware on the FPGA
 *
 * @return OK if successful, ERROR otherwise
 *
 */

int
tiReload()
{
  int rval = OK, iwait = 0, reg = 0, Locked = 0;

  if(TIp==NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  printf ("%s: \n FPGA Re-Load ! \n",__FUNCTION__);
  TILOCK;
  vmeWrite32(&TIp->reset,TI_RESET_JTAG);
  vmeWrite32(&TIp->JTAGPROMBase[(0x3c)>>2],0);
  vmeWrite32(&TIp->JTAGPROMBase[(0xf2c)>>2],0xEE);

  taskDelay(2 * 60);

  /* Wait for FPGA Ready / Clock DCM locked */
  while(iwait < 100)
    {
      reg = vmeRead32(&TIp->GTPtriggerBufferLength);
      Locked = (reg >> 29) & 0x7;

      if(Locked == 0x7)
	break;

      taskDelay(50);
      iwait ++;
    }

  TIUNLOCK;

  if(Locked < 0x7)
    {
      printf("%s: ERROR: FPGA is not yet ready.\n",
	     __func__);
      printf("   CLK250 DCM: %s\n", (Locked & (1 << 0)) ? "Locked" :
	     "*** Not Locked ***");
      printf("   CLK125 DCM: %s\n", (Locked & (1 << 1)) ? "Locked" :
	     "*** Not Locked ***");
      printf("   VMECLK DCM: %s\n", (Locked & (1 << 2)) ? "Locked" :
	     "*** Not Locked ***");

      rval = ERROR;
    }

  return rval;

}

/**
 * @ingroup Status
 * @brief Get the Module Serial Number
 *
 * @param rSN  Pointer to string to pass Serial Number
 *
 * @return SerialNumber if successful, ERROR otherwise
 *
 */
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

/**
 * @ingroup MasterConfig
 * @brief Resync the 250 MHz Clock
 *
 * @return OK if successful, ERROR otherwise
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

/**
 * @ingroup Config
 * @brief Perform a soft reset of the TI
 *
 * @return OK if successful, ERROR otherwise
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

/**
 * @ingroup Config
 * @brief Set the crate ID
 *
 * @return OK if successful, ERROR otherwise
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

  if(crateID>0xff)
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

/**
 * @ingroup Status
 * @brief Get the crate ID of the selected port
 *
 * @param  port
 *       - 0 - Self
 *       - 1-8 - Fiber port 1-8 (If Master)
 *
 * @return port Crate ID if successful, ERROR otherwise
 *
 */
int
tiGetCrateID(int port)
{
  int rval=0;
  if(TIp == NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  if((port<0) || (port>8))
    {
      printf("%s: ERROR: Invalid port (%d)\n",
	     __FUNCTION__,port);
    }

  TILOCK;
  if(port==0)
    {
      rval = (vmeRead32(&TIp->master_tiID) & TI_ID_CRATEID_MASK)>>8;
    }
  else
    {
      rval = (vmeRead32(&TIp->hfbr_tiID[port-1]) & TI_ID_CRATEID_MASK)>>8;
    }
  TIUNLOCK;

  return rval;
}

/**
 * @ingroup Status
 * @brief Get the trigger sources enabled bits of the selected port
 *
 * @param  port
 *       - 0 - Self
 *       - 1-8 - Fiber port 1-8  (If Master)
 *
 * @return bitmask of rigger sources enabled if successful, otherwise ERROR
 *         bitmask
 *         - 0 - P0
 *         - 1 - Fiber 1
 *         - 2 - Loopback
 *         - 3 - TRG (FP)
 *         - 4  - VME
 *         - 5 - TS Inputs (FP)
 *         - 6 - TS (rev 2)
 *         - 7 - Internal Pulser
 *
 */
int
tiGetPortTrigSrcEnabled(int port)
{
  int rval=0;
  if(TIp == NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  if(tiUseTsRev2)
    {
      printf("%s: ERROR: Invalid usage with TS rev 2.\n",
	     __func__);
      return ERROR;
    }

  if((port<0) || (port>8))
    {
      printf("%s: ERROR: Invalid port (%d)\n",
	     __FUNCTION__,port);
    }

  TILOCK;
  if(port==0)
    {
      rval = (vmeRead32(&TIp->master_tiID) & TI_ID_TRIGSRC_ENABLE_MASK);
    }
  else
    {
      rval = (vmeRead32(&TIp->hfbr_tiID[port-1]) & TI_ID_TRIGSRC_ENABLE_MASK);
    }
  TIUNLOCK;

  return rval;
}

/**
 * @ingroup Status
 * @brief Get the blocklevel of the TI-Slave on the selected port
 * @param port
 *       - 1-8 - Fiber port 1-8
 *
 * @return port blocklevel if successful, ERROR otherwise
 *
 */
int
tiGetSlaveBlocklevel(int port)
{
  int rval=0;
  if(TIp == NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  if(tiUseTsRev2)
    {
      printf("%s: ERROR: Invalid usage with TS rev 2.\n",
	     __func__);
      return ERROR;
    }

  if((port<1) || (port>8))
    {
      printf("%s: ERROR: Invalid port (%d)\n",
	     __FUNCTION__,port);
    }

  TILOCK;
  rval = (vmeRead32(&TIp->hfbr_tiID[port-1]) & TI_ID_BLOCKLEVEL_MASK)>>16;
  TIUNLOCK;

  return rval;
}

/**
 * @ingroup MasterConfig
 * @brief Set the number of events per block
 * @param blockLevel Number of events per block
 * @return OK if successful, ERROR otherwise
 *
 */
int
tiSetBlockLevel(int blockLevel)
{
  return tiBroadcastNextBlockLevel(blockLevel);
}

/**
 * @ingroup MasterConfig
 * @brief Broadcast the next block level (to be changed at the end of
 * the next sync event, or during a call to tiSyncReset(1).
 *
 * @see tiSyncReset(1)
 * @param blockLevel block level to broadcats
 *
 * @return OK if successful, ERROR otherwise
 *
 */

int
tiBroadcastNextBlockLevel(int blockLevel)
{
  unsigned int trigger=0;
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

  if(!tiMaster)
    {
      printf("%s: ERROR: TI is not the TI Master.\n",__FUNCTION__);
      return ERROR;
    }

  if((tiUseTsRev2 == 1) && (blockLevel != 1))
    {
      printf("%s: WARN: Invalid blockLevel (%d) for use with TS rev2 branch.  Using 1.",
	     __func__, blockLevel);
      blockLevel = 1;
    }

  TILOCK;
  trigger = vmeRead32(&TIp->trigsrc);

  if(!(trigger & TI_TRIGSRC_VME)) /* Turn on the VME trigger, if not enabled */
    vmeWrite32(&TIp->trigsrc, TI_TRIGSRC_VME | trigger);

  vmeWrite32(&TIp->triggerCommand, TI_TRIGGERCOMMAND_SET_BLOCKLEVEL | blockLevel);

  if(!(trigger & TI_TRIGSRC_VME)) /* Turn off the VME trigger, if it was initially disabled */
    vmeWrite32(&TIp->trigsrc, trigger);

  TIUNLOCK;

  tiGetNextBlockLevel();

  return OK;

}

/**
 * @ingroup Status
 * @brief Get the block level that will be updated on the end of the block readout.
 *
 * @return Next Block Level if successful, ERROR otherwise
 *
 */

int
tiGetNextBlockLevel()
{
  unsigned int reg_bl=0;
  int bl=0;
  if(TIp==NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  reg_bl = vmeRead32(&TIp->blocklevel);
  bl = (reg_bl & TI_BLOCKLEVEL_RECEIVED_MASK)>>24;
  tiNextBlockLevel = bl;

  tiBlockLevel = (reg_bl & TI_BLOCKLEVEL_CURRENT_MASK)>>16;
  TIUNLOCK;

  return bl;
}

/**
 * @ingroup Status
 * @brief Get the current block level
 *
 * @return Next Block Level if successful, ERROR otherwise
 *
 */
int
tiGetCurrentBlockLevel()
{
  unsigned int reg_bl=0;
  int bl=0;
  if(TIp==NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  reg_bl = vmeRead32(&TIp->blocklevel);
  bl = (reg_bl & TI_BLOCKLEVEL_CURRENT_MASK)>>16;
  tiBlockLevel = bl;
  tiNextBlockLevel = (reg_bl & TI_BLOCKLEVEL_RECEIVED_MASK)>>24;
  TIUNLOCK;

  /* Change Bus Error block termination, based on blocklevel */
  if(tiBlockLevel>2)
    {
      tiEnableBusError();
    }
  else
    {
      tiDisableBusError();
    }

  return bl;
}

/**
 * @ingroup Config
 * @brief Set TS to instantly change blocklevel when broadcast is received.
 *
 * @param enable Option to enable or disable this feature
 *       - 0: Disable
 *        !0: Enable
 *
 * @return OK if successful, ERROR otherwise
 *
 */
int
tiSetInstantBlockLevelChange(int enable)
{
  if(TIp==NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  if(enable)
    vmeWrite32(&TIp->vmeControl,
	       vmeRead32(&TIp->vmeControl) | TI_VMECONTROL_BLOCKLEVEL_UPDATE);
  else
    vmeWrite32(&TIp->vmeControl,
	       vmeRead32(&TIp->vmeControl) & ~TI_VMECONTROL_BLOCKLEVEL_UPDATE);
  TIUNLOCK;

  return OK;
}

/**
 * @ingroup Status
 * @brief Get Status of instant blocklevel change when broadcast is received.
 *
 * @return 1 if enabled, 0 if disabled , ERROR otherwise
 *
 */
int
tiGetInstantBlockLevelChange()
{
  int rval=0;
  if(TIp==NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  rval = (vmeRead32(&TIp->vmeControl) & TI_VMECONTROL_BLOCKLEVEL_UPDATE)>>21;
  TIUNLOCK;

  return rval;
}


/**
 * @ingroup Config
 * @brief Set the trigger source
 *     This routine will set a library variable to be set in the TI registers
 *     at a call to tiIntEnable.
 *
 *  @param trig - integer indicating the trigger source
 *         - 0: P0
 *         - 1: HFBR#1
 *         - 2: Front Panel (TRG)
 *         - 3: Front Panel TS Inputs
 *         - 4: TS (rev2)
 *         - 5: Random
 *         - 6-9: TS Partition 1-4
 *         - 10: HFBR#5
 *         - 11: Pulser Trig 2 then Trig1 after specified delay
 *
 * @return OK if successful, ERROR otherwise
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

  if( (trig > 11) || (trig < 0) )
    {
      printf("%s: ERROR: Invalid Trigger Source (%d).  Must be between 0 and 11.\n",
	     __FUNCTION__,trig);
      return ERROR;
    }


  if(!tiMaster)
    {
      /* Setup for TI Slave */
      trigenable = TI_TRIGSRC_VME;

      if((trig>=6) && (trig<=9)) /* TS partition specified */
	{
	  if(tiSlaveFiberIn!=1)
	    {
	      printf("%s: WARN: Partition triggers NOT USED on Fiber Port 5.\n",
		     __FUNCTION__);
	      trigenable |= TI_TRIGSRC_HFBR5;
	    }

	  trigenable |= TI_TRIGSRC_HFBR1;
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
	  if(tiSlaveFiberIn==1)
	    {
	      trigenable |= TI_TRIGSRC_HFBR1;
	    }
	  else if(tiSlaveFiberIn==5)
	    {
	      trigenable |= TI_TRIGSRC_HFBR5;
	    }
	  if( ((trig != TI_TRIGGER_HFBR1) && (tiSlaveFiberIn == 1))
	      || ((trig != TI_TRIGGER_HFBR5) && (tiSlaveFiberIn == 5)) )
	    {
	      printf("%s: WARN:  Only valid trigger source for TI Slave is HFBR%d (trig = %d)",
		     __FUNCTION__, tiSlaveFiberIn,
		     (tiSlaveFiberIn==1)?TI_TRIGGER_HFBR1:TI_TRIGGER_HFBR5);
	      printf("  Ignoring specified trig (%d)\n",trig);
	    }
	}

    }
  else
    {
      /* Setup for TI Master */

      /* Set VME and Loopback by default */
      trigenable  = TI_TRIGSRC_VME;
      trigenable |= TI_TRIGSRC_LOOPBACK;

      if(tiUseTsRev2 && (trig != TI_TRIGGER_TSREV2))
	{
	  printf("%s: WARN: Only valid choice with TS rev 2 is (%d).\n",
		 __func__, TI_TRIGGER_TSREV2);
	  printf("  Ignoring specified trig (%d)\n",trig);
	  trig = TI_TRIGGER_TSREV2;
	}

      switch(trig)
	{
	case TI_TRIGGER_P0:
	  trigenable |= TI_TRIGSRC_P0;
	  break;

	case TI_TRIGGER_HFBR1:
	  trigenable |= TI_TRIGSRC_HFBR1;
	  break;

	case TI_TRIGGER_HFBR5:
	  trigenable |= TI_TRIGSRC_HFBR5;
	  break;

	case TI_TRIGGER_FPTRG:
	  trigenable |= TI_TRIGSRC_FPTRG;
	  break;

	case TI_TRIGGER_TSINPUTS:
	  trigenable |= TI_TRIGSRC_TSINPUTS;
	  break;

	case TI_TRIGGER_TSREV2:
	  trigenable &= ~TI_TRIGSRC_VME;
	  trigenable |= TI_TRIGSRC_TSREV2;
	  break;

	case TI_TRIGGER_PULSER:
	  trigenable |= TI_TRIGSRC_PULSER;
	  break;

	case TI_TRIGGER_TRIG21:
	  trigenable |= TI_TRIGSRC_PULSER;
	  trigenable |= TI_TRIGSRC_TRIG21;
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

/**
 * @ingroup Config
 * @brief Set trigger sources with specified trigmask
 *    This routine is for special use when tiSetTriggerSource(...) does
 *    not set all of the trigger sources that is required by the user.
 *
 * @param trigmask bits:
 *        -         0:  P0
 *        -         1:  HFBR #1
 *        -         2:  TI Master Loopback
 *        -         3:  Front Panel (TRG) Input
 *        -         4:  VME Trigger
 *        -         5:  Front Panel TS Inputs
 *        -         6:  TS (rev 2) Input
 *        -         7:  Random Trigger
 *        -        10:  HFBR #5
 *        -        11:  Pulser Trig2 with delayed Trig1 (only compatible with 2 and 7)
 *
 * @return OK if successful, ERROR otherwise
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

/**
 * @ingroup Config
 * @brief Enable/Disable use of O#4 as the 'GO' output level, enabled when
 *         trigger sources are enabled.
 *
 * @param enable Enable flag
 *<pre>
 *     0: Disable
 *    !0: Enable
 *</pre>
 *
 * @return OK if successful, ERROR otherwise
 *
 */
int
tiSetGoOutput(int enable)
{
  TILOCK;
  if(enable == 0)
    tiUseGoOutput = 0;
  else
    tiUseGoOutput = 1;
  TIUNLOCK;

  return OK;
}
/**
 * @ingroup Config
 * @brief Enable trigger sources
 * Enable trigger sources set by
 *                          tiSetTriggerSource(...) or
 *                          tiSetTriggerSourceMask(...)
 * @sa tiSetTriggerSource
 * @sa tiSetTriggerSourceMask
 *
 * @return OK if successful, ERROR otherwise
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
  vmeWrite32(&TIp->boardID,
	   (vmeRead32(&TIp->boardID) & ~TI_BOARDID_CRATEID_MASK)  | tiCrateID);

  if(tiUseGoOutput)
    vmeWrite32(&TIp->trigsrc, tiTriggerSource | TI_TRIGSRC_GO);
  else
    vmeWrite32(&TIp->trigsrc, tiTriggerSource);
  TIUNLOCK;

  return OK;

}

/**
 * @ingroup Config
 * @brief Force TI to send trigger source enabled bits to TI-master or TD
 *
 * @return OK if successful, ERROR otherwise
 *
 */
int
tiForceSendTriggerSourceEnable()
{
  if(TIp==NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  vmeWrite32(&TIp->trigsrc,
	     (vmeRead32(&TIp->trigsrc) & (TI_TRIGSRC_SOURCEMASK | TI_TRIGSRC_GO)) |
	     TI_TRIGSRC_FORCE_SEND);
  TIUNLOCK;

  return OK;

}



/**
 * @ingroup Config
 * @brief Disable trigger sources
 *
 * @param fflag
 *   -  0: Disable Triggers
 *   - >0: Disable Triggers and generate enough triggers to fill the current block
 *
 * @return OK if successful, ERROR otherwise
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

/**
 * @ingroup Config
 * @brief Set the Sync source mask
 *
 * @param sync - MASK indicating the sync source
 *       bit: description
 *       -  0: P0
 *       -  1: HFBR1
 *       -  2: HFBR5
 *       -  3: FP
 *       -  4: LOOPBACK
 *
 * @return OK if successful, ERROR otherwise
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
  vmeWrite32(&TIp->reset, TI_RESET_AUTOALIGN_HFBR1_SYNC | TI_RESET_AUTOALIGN_HFBR5_SYNC);
  taskDelay(1);
  vmeWrite32(&TIp->sync,sync);
  TIUNLOCK;

  return OK;
}

/**
 * @ingroup Config
 * @brief Set the event format
 *
 * @param format - integer number indicating the event format
 *          - 0: 32 bit event number only
 *          - 1: 32 bit event number + 32 bit timestamp
 *          - 2: 32 bit event number + higher 16 bits of timestamp + higher 16 bits of eventnumber
 *          - 3: 32 bit event number + 32 bit timestamp
 *              + higher 16 bits of timestamp + higher 16 bits of eventnumber
 *
 * @return OK if successful, ERROR otherwise
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

  formatset = vmeRead32(&TIp->dataFormat)
    & ~(TI_DATAFORMAT_TIMING_WORD | TI_DATAFORMAT_HIGHERBITS_WORD);

  switch(format)
    {
    case 0:
      break;

    case 1:
      formatset |= TI_DATAFORMAT_TIMING_WORD;
      break;

    case 2:
      formatset |= TI_DATAFORMAT_HIGHERBITS_WORD;
      break;

    case 3:
      formatset |= (TI_DATAFORMAT_TIMING_WORD | TI_DATAFORMAT_HIGHERBITS_WORD);
      break;

    }

  vmeWrite32(&TIp->dataFormat,formatset);

  TIUNLOCK;

  return OK;
}

/**
 * @ingroup Config
 * @brief Set whether or not the latched pattern of FP Inputs in block readout
 *
 * @param enable
 *    - 0: Disable
 *    - >0: Enable
 *
 * @return OK if successful, otherwise ERROR
 *
 */
int
tiSetFPInputReadout(int enable)
{
  if(TIp == NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  if(enable)
    vmeWrite32(&TIp->dataFormat,
	       vmeRead32(&TIp->dataFormat) | TI_DATAFORMAT_FPINPUT_READOUT);
  else
    vmeWrite32(&TIp->dataFormat,
	       vmeRead32(&TIp->dataFormat) & ~TI_DATAFORMAT_FPINPUT_READOUT);
  TIUNLOCK;

  return OK;
}

/**
 * @ingroup MasterConfig
 * @brief Set and enable the "software" trigger
 *
 *  @param trigger  trigger type 1 or 2 (playback trigger)
 *  @param nevents  integer number of events to trigger
 *  @param period_inc  period multiplier, depends on range (0-0x7FFF)
 *  @param range
 *     - 0: small period range (min: 120ns, increments of 120ns)
 *     - 1: large period range (min: 120ns, increments of 245.7us)
 *
 * @return OK if successful, ERROR otherwise
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
      logMsg("\ntiSoftTrig: ERROR: TI not initialized\n",1,2,3,4,5,6);
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
    time = 120 + (30 * period_inc);
  if(range==1)
    time = 120 + (30 * period_inc * 2048);

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


/**
 * @ingroup MasterConfig
 * @brief Set the parameters of the random internal trigger
 *
 * @param trigger  - Trigger Selection
 *       -              1: trig1
 *       -              2: trig2
 * @param setting  - frequency prescale from 500MHz
 *
 * @sa tiDisableRandomTrigger
 * @return OK if successful, ERROR otherwise.
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

  if(setting>0)
    rate = ((double)500000) / ((double) (2<<(setting-1)));
  else
    rate = ((double)500000);

  printf("%s: Enabling random trigger (trig%d) at rate (kHz) = %.2f\n",
	 __FUNCTION__,trigger,rate);

  TILOCK;
  if(trigger==1)
    vmeWrite32(&TIp->randomPulser,
	       setting | (setting<<4) | TI_RANDOMPULSER_TRIG1_ENABLE);
  else if (trigger==2)
    vmeWrite32(&TIp->randomPulser,
	       (setting | (setting<<4))<<8 | TI_RANDOMPULSER_TRIG2_ENABLE );

  TIUNLOCK;

  return OK;
}


/**
 * @ingroup MasterConfig
 * @brief Disable random trigger generation
 * @sa tiSetRandomTrigger
 * @return OK if successful, ERROR otherwise.
 */
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

/**
 * @ingroup Readout
 * @brief Read a block of events from the TI
 *
 * @param   data  - local memory address to place data
 * @param   nwrds - Max number of words to transfer
 * @param   rflag - Readout Flag
 *       -       0 - programmed I/O from the specified board
 *       -       1 - DMA transfer using Universe/Tempe DMA Engine
 *                    (DMA VME transfer Mode must be setup prior)
 *
 * @return Number of words transferred to data if successful, ERROR otherwise
 *
 */
int
tiReadBlock(volatile unsigned int *data, int nwrds, int rflag)
{
  int ii, dummy=0, iword = 0;
  int dCnt, retVal, xferCount;
  volatile unsigned int *laddr;
  unsigned int vmeAdr, val;
  int ntrig=0, itrig = 0, trigwords = 0;

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
      if(tiBusError==0)
	{
	  logMsg("tiReadBlock: WARN: Bus Error Block Termination was disabled.  Re-enabling\n",
		 1,2,3,4,5,6);
	  TIUNLOCK;
	  tiEnableBusError();
	  TILOCK;
	}
      /* Assume that the DMA programming is already setup.
	 Don't Bother checking if there is valid data - that should be done prior
	 to calling the read routine */

      /* Check for 8 byte boundary for address - insert dummy word (Slot 0 FADC Dummy DATA)*/
      if((unsigned long) (data)&0x7)
	{
#ifdef VXWORKS
	  *data = (TI_DATA_TYPE_DEFINE_MASK) | (TI_FILLER_WORD_TYPE) | (tiSlotNumber<<22);
#else
	  *data = LSWAP((TI_DATA_TYPE_DEFINE_MASK) | (TI_FILLER_WORD_TYPE) | (tiSlotNumber<<22));
#endif
	  dummy = 1;
	  laddr = (data + 1);
	}
      else
	{
	  dummy = 0;
	  laddr = data;
	}

      vmeAdr = (unsigned long)TIpd - tiA32Offset;

#ifdef VXWORKS
      retVal = sysVmeDmaSend((UINT32)laddr, vmeAdr, (nwrds<<2), 0);
#else
      retVal = vmeDmaSend((unsigned long)laddr, vmeAdr, (nwrds<<2));
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
	  if(tiUseEvTypeScalers)
	    tiScanAndFillEvTypeScalers(data, xferCount);

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
      if(tiBusError==1)
	{
	  logMsg("tiReadBlock: WARN: Bus Error Block Termination was enabled.  Disabling\n",
		 1,2,3,4,5,6);
	  TIUNLOCK;
	  tiDisableBusError();
	  TILOCK;
	}

      dCnt = 0;
      ii=0;

      /* First word should be the block header */
      val = (unsigned int) *TIpd;
      data[ii++] = val;
#ifndef VXWORKS
      val = LSWAP(val);
#endif
      if((val & 0xffc00000) == (TI_DATA_TYPE_DEFINE_MASK | TI_BLOCK_HEADER_WORD_TYPE
		 | (tiSlotNumber<<22) ) )
	{
	  ntrig = val & TI_DATA_BLKLEVEL_MASK;

	  /* Next word is the CODA 3.0 header */
	  val = (unsigned int) *TIpd;
	  data[ii++] = val;
#ifndef VXWORKS
	  val = LSWAP(val);
#endif
	  if((val & 0xFF102000) == 0xFF102000)
	    {
	      if((val & 0xff) != ntrig)
		{
		  logMsg("\ntiReadBlock: ERROR: TI Blocklevel %d inconsistent with TI Trigger Bank Header (0x%08x)",ntrig, val, 3, 4, 5, 6);
		  // return?

		}

	      /* Loop over triggers in block */
	      for(itrig = 0; itrig < ntrig; itrig++)
		{
		  /* Trigger type word contains number of words to follow */
		  val = (unsigned int) *TIpd;
		  data[ii++] = val;

#ifndef VXWORKS
		  val = LSWAP(val);
#endif
		  trigwords = val & 0xFFFF;
		  for(iword = 0; iword < trigwords; iword++)
		    {
		      val = (unsigned int) *TIpd;
		      data[ii++] = val;
		    }
		}

	      /* Next word should be block trailer */
	      val = (unsigned int) *TIpd;
	      data[ii++] = val;
#ifndef VXWORKS
	      val = LSWAP(val);
#endif
	      if(val == (TI_DATA_TYPE_DEFINE_MASK | TI_BLOCK_TRAILER_WORD_TYPE
			 | (tiSlotNumber<<22) | ii) )
		{
		  if((ii%2)!=0)
		    {
		      /* Read out an extra word (filler) in the fifo */
		      val = (unsigned int) *TIpd;
#ifndef VXWORKS
		      val = LSWAP(val);
#endif
		      if(((val & TI_DATA_TYPE_DEFINE_MASK) != TI_DATA_TYPE_DEFINE_MASK) ||
			 ((val & TI_WORD_TYPE_MASK) != TI_FILLER_WORD_TYPE))
			{
			  logMsg("\ntiReadBlock: ERROR: Unexpected word after block trailer (0x%08x)\n",
				 val,2,3,4,5,6);
			}
		    }

		  dCnt = ii;
		}
	      else
		{
		  logMsg("\ntiReadBlock: ERROR: Invalid TI block trailer 0x%08x\n",
			 val, 2, 3, 4, 5, 6);
		  dCnt = ii;
		}
	    }
	  else
	    {
	      logMsg("\ntiReadBlock: ERROR: Invalid Trigger bank header from TI 0x%08x\n",val, 2, 3, 4, 5, 6);
	      dCnt = ii;
	    }

	}
      else
	{
	  logMsg("\ntiReadBlock: ERROR: Invalid block header from TI 0x%08x\n",
		 val, 2, 3, 4, 5, 6);
	  dCnt = ii;
	}
      if(tiUseEvTypeScalers)
	tiScanAndFillEvTypeScalers(data, dCnt);

      TIUNLOCK;
      return dCnt;
    }

  TIUNLOCK;

  return OK;
}

/**
 * @ingroup Config
 *
 * @brief Option to generate a fake trigger bank when
 *        @tiReadTriggerBlock finds an ERROR.
 *        Enabled by library default.
 *
 * @param enable Enable fake trigger bank if enable != 0.
 *
 * @return OK
 *
 */
int
tiFakeTriggerBankOnError(int enable)
{
  TILOCK;
  if(enable)
    tiFakeTriggerBank = 1;
  else
    tiFakeTriggerBank = 0;
  TIUNLOCK;

  return OK;
}

/**
 * @ingroup Readout
 * @brief Generate a fake trigger bank.  Called by @tiReadTriggerBlock if ERROR.
 *
 * @param   data  - local memory address to place data
 *
 * @return Number of words generated to data if successful, ERROR otherwise
 *
 */
int
tiGenerateTriggerBank(volatile unsigned int *data)
{
  int bl = 0;
  int iword, nwords = 2;
  unsigned int error_tag = 0;
  unsigned int word;

  bl = tiGetCurrentBlockLevel();
  data[0] = nwords - 1;
  data[1] = 0xFF102000 | (error_tag << 16)| bl;

  if(tiSwapTriggerBlock==1)
    {
      for(iword = 0; iword < nwords; iword++)
	{
	  word = data[iword];
	  data[iword] = LSWAP(word);
	}
    }

  return nwords;
}

/**
 * @ingroup Readout
 * @brief Read a block from the TI and form it into a CODA Trigger Bank
 *
 * @param   data  - local memory address to place data
 *
 * @return Number of words transferred to data if successful, ERROR otherwise
 *
 */
int
tiReadTriggerBlock(volatile unsigned int *data)
{
  int rval=0, nwrds=0, rflag=0;
  int iword=0;
  unsigned int word=0;
  int iblkhead=-1, iblktrl=-1;

  if(data==NULL)
    {
      logMsg("\ntiReadTriggerBlock: ERROR: Invalid Destination address\n",0,0,0,0,0,0);
      return(ERROR);
    }

  /* Determine the maximum number of words to expect, from the block level */
  nwrds = (8*tiBlockLevel) + 8;

  /* Optimize the transfer type based on the blocklevel */
  if(tiBlockLevel>2)
    { /* Use DMA */
      rflag = 1;
    }
  else
    { /* Use programmed I/O (Single cycle reads) */
      rflag = 0;
    }

  /* Obtain the trigger bank by just making a call the tiReadBlock */
  rval = tiReadBlock(data, nwrds, rflag);
  if(rval < 0)
    {
      /* Error occurred */
      logMsg("tiReadTriggerBlock: ERROR: tiReadBlock returned ERROR\n",
	     1,2,3,4,5,6);

      if(tiFakeTriggerBank)
	return tiGenerateTriggerBank(data);
      else
	return ERROR;
    }
  else if (rval == 0)
    {
      /* No data returned */
      logMsg("tiReadTriggerBlock: WARN: No data available\n",
	     1,2,3,4,5,6);

      if(tiFakeTriggerBank)
	return tiGenerateTriggerBank(data);
      else
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
	  if(((word & TI_WORD_TYPE_MASK)) == TI_BLOCK_HEADER_WORD_TYPE)
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
      logMsg("tiReadTriggerBlock: ERROR: Failed to find TI Block Header\n",
	     1,2,3,4,5,6);

/* #define DEBUGDATA */
#ifdef DEBUGDATA
      int idbg;
      static int out = 0;
      if(out == 0)
	{
	  for(idbg = 0; idbg < rval; idbg++)
	    printf("%3d: 0x%08x\n",
		   idbg, LSWAP(data[iword]));
	}
      out = 1;
#endif

      if(tiFakeTriggerBank)
	return tiGenerateTriggerBank(data);
      else
	return ERROR;
    }
  if(iblkhead != 0)
    {
      logMsg("tiReadTriggerBlock: WARN: Invalid index (%d) for the TI Block header.\n",
	     iblkhead,2,3,4,5,6);
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
	  if(((word & TI_WORD_TYPE_MASK)) == TI_BLOCK_TRAILER_WORD_TYPE)
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
      logMsg("tiReadTriggerBlock: ERROR: Failed to find TI Block Trailer\n",
	     1,2,3,4,5,6);

      if(tiFakeTriggerBank)
	return tiGenerateTriggerBank(data);
      else
	return ERROR;
    }

  /* Get the block trailer, and check the number of words contained in it */
  word = data[iblktrl];
#ifndef VXWORKS
  word = LSWAP(word);
#endif
  if((iblktrl - iblkhead + 1) != (word & 0x3fffff))
    {
      logMsg("tiReadTriggerBlock: Number of words inconsistent (index count = %d, block trailer count = %d\n",
	     (iblktrl - iblkhead + 1), word & 0x3fffff,3,4,5,6);

      if(tiFakeTriggerBank)
	return tiGenerateTriggerBank(data);
      else
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

/**
 * @ingroup Readout
 * @brief Check the provided array for valid trigger block format
 *
 * @param   data  - local memory address to find trigger block
 *
 * @return OK if successful, ERROR otherwise
 *
 */
int
tiCheckTriggerBlock(volatile unsigned int *data)
{
  unsigned int blen=0, blevel=0, evlen=0;
  int iword=0, iev=0, ievword=0;
  int rval=OK;

  printf("--------------------------------------------------------------------------------\n");
  /* First word should be the trigger bank length */
  blen = data[iword];
  printf("%4d: %08X - TRIGGER BANK LENGTH - len = %d\n",iword, data[iword], blen);
  iword++;

  /* Trigger Bank Header */
  if( ((data[iword] & 0xFF100000)>>16 != 0xFF10) ||
      ((data[iword] & 0x0000FF00)>>8 != 0x20) )
    {
      rval = ERROR;
      printf("%4d: %08X - **** INVALID TRIGGER BANK HEADER ****\n",
	     iword,
	     data[iword]);
      iword++;
      while(iword<blen+1)
	{
	  if(iword>blen)
	    {
	      rval = ERROR;
	      printf("----: **** ERROR: Data continues past Trigger Bank Length (%d) ****\n",blen);
	    }
	  printf("%4d: %08X - **** REST OF DATA ****\n",
		 iword,
		 data[iword]);
	  iword++;
	}
    }
  else
    {
      if(iword>blen)
	{
	  rval = ERROR;
	  printf("----: **** ERROR: Data continues past Trigger Bank Length (%d) ****\n",blen);
	}
      blevel = data[iword] & 0xFF;
      printf("%4d: %08X - TRIGGER BANK HEADER - type = %d  blocklevel = %d\n",
	     iword,
	     data[iword],
	     (data[iword] & 0x000F0000)>>16,
	     blevel);
      iword++;

      for(iev=0; iev<blevel; iev++)
	{
	  if(iword>blen)
	    {
	      rval = ERROR;
	      printf("----: **** ERROR: Data continues past Trigger Bank Length (%d) ****\n",blen);
	    }

	  if((data[iword] & 0x00FF0000)>>16!=0x01)
	    {
	      rval = ERROR;
	      printf("%4d: %08x - **** INVALID EVENT HEADER ****\n",
		     iword, data[iword]);
	      iword++;
	      while(iword<blen+1)
		{
		  printf("%4d: %08X - **** REST OF DATA ****\n",
			 iword,
			 data[iword]);
		  iword++;
		}
	      break;
	    }
	  else
	    {
	      if(iword>blen)
		{
		  rval = ERROR;
		  printf("----: **** ERROR: Data continues past Trigger Bank Length (%d) ****\n",blen);
		}

	      evlen = data[iword] & 0x0000FFFF;
	      printf("%4d: %08x - EVENT HEADER - trigtype = %d  len = %d\n",
		     iword,
		     data[iword],
		     (data[iword] & 0xFF000000)>>24,
		     evlen);
	      iword++;

	      if(iword>blen)
		{
		  rval = ERROR;
		  printf("----: **** ERROR: Data continues past Trigger Bank Length (%d) ****\n",blen);
		}

	      printf("%4d: %08x - EVENT NUMBER - evnum = %d\n",
		     iword,
		     data[iword],
		     data[iword]);
	      iword++;
	      for(ievword=1; ievword<evlen; ievword++)
		{
		  if(iword>blen)
		    {
		      rval = ERROR;
		      printf("----: **** ERROR: Data continues past Trigger Bank Length (%d) ****\n",blen);
		    }
		  printf("%4d: %08X - EVENT DATA\n",
			 iword,
			 data[iword]);
		  iword++;
		}
	    }
	}
    }

  printf("--------------------------------------------------------------------------------\n");
  return rval;
}

/**
 * @ingroup Readout
 * @brief Provided TI data (trigger block, or raw TI blocked data), decode the
 * event types.
 *
 * @param  data  - local memory address to find trigger block
 * @param data_len - length of provided 'data' array
 * @param nevents - how many events in the block to record [1, 255]
 * @param evtypes - where to store the event types.
 *
 * @return Number of event types stored if successful, ERROR otherwise
 *
 */
int
tiDecodeTriggerTypes(volatile unsigned int *data, int data_len,
		     int nevents, unsigned int *evtypes)
{
  int rval = -1;
  int iword = 0;
  int blocklevel = -1;
  int event_len = -1;
  int ievent = 1;
  unsigned int dataword = 0;
  int nevtypes = 0;
  int extra_ev_type_bitshift = 0;

  if(tiUseTsRev2)
    {
      extra_ev_type_bitshift = 1;
    }


  /* Loop until we find the trigger bank */
  while(iword < data_len)
    {
      dataword = data[iword];
#ifndef VXWORKS
      dataword = LSWAP(dataword);
#endif
      if( ((dataword & 0xFF100000)>>16 == 0xFF10) &&
	  ((dataword & 0x0000FF00)>>8 == 0x20) )
	{
	  blocklevel =  dataword & 0xFF;
	  iword++;
	  break;
	}
      iword++;
    }

  if(blocklevel == -1)
    {
      logMsg("tiDecodeTriggerTypes: ERROR: Failed to find Trigger Bank header\n",
	     0,1,2,3,4,5);
      return ERROR;
    }

  if(nevents < blocklevel)
    {
      blocklevel = nevents;
    }

  /* Loop until we get all of the events */
  while((iword < data_len) && (ievent <= blocklevel))
    {
      dataword = data[iword];
#ifndef VXWORKS
      dataword = LSWAP(dataword);
#endif
      if((dataword & 0x00FF0000)>>16 == 0x01)
	{
	  if(extra_ev_type_bitshift)
	    evtypes[nevtypes++] = (dataword & 0xFF000000) >> 26;
	  else
	    evtypes[nevtypes++] = (dataword & 0xFF000000) >> 24;


	  if(ievent == blocklevel)
	    {
	      rval = nevtypes;
	      break;
	    }

	  /* Skip to next event */
	  event_len = dataword & 0xFFFF;
	  ievent++;
	  iword += event_len + 1;
	}
      else
	{
	  /* we're lost... just increment */
	  iword++;
	}
    }

  return rval;

}

/**
 * @ingroup Readout
 * @brief Provided TI data (trigger block, or raw TI blocked data), decode the
 * event type for the specified event of block.
 *
 * @param  data  - local memory address to find trigger block
 * @param data_len - length of provided 'data' array
 * @param events - which event of the block to obtain event type [1, 255]
 *
 * @return Event type if successful, ERROR otherwise
 *
 */
int
tiDecodeTriggerType(volatile unsigned int *data, int data_len, int event)
{
  int rval = 0;
  unsigned int evtypes[256];
  int nevtypes = 0;

  if((event < 0) || (event > 255))
    {
      logMsg("tiDecodeTriggerType: ERROR: Invalid event number (%d)\n",
	     event, 1, 2, 3, 4, 5);
      return ERROR;
    }

  nevtypes = tiDecodeTriggerTypes(data, data_len, event, (unsigned int *)&evtypes);

  if(nevtypes == ERROR)
    {
      logMsg("tiDecodeTriggerType: ERROR: Failed to find trigger type for event %d\n",
	     event, 1, 2, 3, 4, 5);
      rval = ERROR;
    }
  if(nevtypes < event)
    {
      logMsg("tiDecodeTriggerType: ERROR: # EvTypes (%d) < Requested Event (%d)\n",
	     nevtypes, event, 3, 4, 5, 6);
      rval = ERROR;
    }
  else
    {
      rval = evtypes[event - 1];
    }

  return rval;
}

/**
 * @ingroup Readout
 * @brief Provided TI data (trigger block, or raw TI blocked data), decode the
 * event type for the specified event of block.
 *
 * @param data     - local memory address to find trigger block
 * @param data_len - length of provided 'data' array
 * @param syncFlag - local memory to store syncFlag
 * @param lateFail - local memory to store lateFail
 * @param evType   - local memory to store event type
 *
 * @return OK if successful, ERROR otherwise
 *
 */
int
tiDecodeTSrev2Data(volatile unsigned int *data, int data_len,
		   int *syncFlag, int *lateFail, int *evType)
{
  int iword = 0;
  int blocklevel = -1;
  unsigned int dataword = 0;
  int found = 0;

  if(!tiUseTsRev2)
    {
      logMsg("tiDecodeTsRev2Data: ERROR: TI not initialized for TSrev2 feature.\n",
	     0,1,2,3,4,5);
      return ERROR;
    }

  *syncFlag = 0;
  *lateFail = 0;
  *evType   = 0;

  if(data_len > 0xFFF)
    {
      logMsg("tiDecodeTsRev2Data: ERROR: Invalid data length (%d).\n",
	     0,1,2,3,4,5);
      return ERROR;
    }

  /* Loop until we find the trigger bank */
  while(iword < data_len)
    {
      dataword = data[iword];
#ifndef VXWORKS
      dataword = LSWAP(dataword);
#endif
      if( ((dataword & 0xFF100000)>>16 == 0xFF10) &&
	  ((dataword & 0x0000FF00)>>8 == 0x20) )
	{
	  blocklevel =  dataword & 0xFF;
	  iword++;
	  break;
	}
      iword++;
    }

  if(blocklevel == -1)
    {
      logMsg("tiDecodeTSrev2Data: ERROR: Failed to find Trigger Bank header\n",
	     0,1,2,3,4,5);
      return ERROR;
    }

  if(blocklevel != 1)
    {
      logMsg("tiDecodeTSrev2Data: ERROR: Invalid Blocklevel (%d).  Must be 1.\n",
	     blocklevel,1,2,3,4,5);
      return ERROR;
    }

  /* Loop until trigger data is found... should be the next word */
  while(iword < data_len)
    {
      dataword = data[iword];
#ifndef VXWORKS
      dataword = LSWAP(dataword);
#endif

      if( ((dataword & 0x00FF0000)>>16) == 0x01)
	{
	  *syncFlag = (dataword & (1<<24)) ? 1 : 0;
	  *lateFail = (dataword & (1<<25)) ? 1 : 0;
	  *evType   = (dataword & 0xFC000000) >> 26;
	  found = 1;
	  break;
	}

      iword++;
    }

  if(!found)
    {
      logMsg("tiDecodeTSrev2Data: ERROR: Trigger data not found\n",
	     0, 1, 2, 3, 4, 5);
      return ERROR;
    }

  return OK;
}

/**
 * @ingroup Config
 * @brief Enable Fiber transceiver
 *
 *  Note:  All Fiber are enabled by default
 *         (no harm, except for 1-2W power usage)
 *
 * @sa tiDisableFiber
 * @param   fiber: integer indicative of the transceiver to enable
 *
 *
 * @return OK if successful, ERROR otherwise.
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

/**
 * @ingroup Config
 * @brief Disnable Fiber transceiver
 *
 * @sa tiEnableFiber
 *
 * @param   fiber: integer indicative of the transceiver to disable
 *
 *
 * @return OK if successful, ERROR otherwise.
 *
 */
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

/**
 * @ingroup Config
 * @brief Set the busy source with a given sourcemask sourcemask bits:
 *
 * @param sourcemask
 *  - 0: SWA
 *  - 1: SWB
 *  - 2: P2
 *  - 3: FP-FTDC
 *  - 4: FP-FADC
 *  - 5: FP
 *  - 6: Unused
 *  - 7: Loopack
 *  - 8-15: Fiber 1-8
 *
 * @param rFlag - decision to reset the global source flags
 *       -      0: Keep prior busy source settings and set new "sourcemask"
 *       -      1: Reset, using only that specified with "sourcemask"
 * @return OK if successful, ERROR otherwise.
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

/**
 *  @ingroup MasterConfig
 *  @brief Set the the trigger lock mode.
 *
 *  @param enable Enable flag
 *      0: Disable
 *     !0: Enable
 *
 * @return OK if successful, ERROR otherwise.
 */
int
tiSetTriggerLock(int enable)
{
  if(TIp==NULL)
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
  if(enable)
    vmeWrite32(&TIp->busy,
	       vmeRead32(&TIp->busy) | TI_BUSY_TRIGGER_LOCK);
  else
    vmeWrite32(&TIp->busy,
	       vmeRead32(&TIp->busy) & ~TI_BUSY_TRIGGER_LOCK);
  TIUNLOCK;

  return OK;
}

/**
 *  @ingroup MasterStatus
 *  @brief Get the current setting of the trigger lock mode.
 *
 * @return 1 if enabled, 0 if disabled, ERROR otherwise.
*/
int
tiGetTriggerLock()
{
  int rval=0;

  if(TIp==NULL)
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
  rval = (vmeRead32(&TIp->busy) & TI_BUSY_TRIGGER_LOCK)>>6;
  TIUNLOCK;

  return rval;
}


/**
 * @ingroup Config
 * @brief Enable Bus Errors to terminate Block Reads
 * @sa tiDisableBusError
 * @return OK if successful, otherwise ERROR
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
  tiBusError=1;
  TIUNLOCK;

}

/**
 * @ingroup Config
 * @brief Disable Bus Errors to terminate Block Reads
 * @sa tiEnableBusError
 * @return OK if successful, otherwise ERROR
 */
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
  tiBusError=0;
  TIUNLOCK;

}

/**
 *  @ingroup MasterConfig
 *  @brief Set the prescale factor for the external trigger
 *
 *  @param   prescale Factor for prescale.
 *               Max {prescale} available is 65535
 *
 *  @return OK if successful, otherwise ERROR.
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


/**
 *  @ingroup Status
 *  @brief Get the current prescale factor
 *  @return Current prescale factor, otherwise ERROR.
 */
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

/**
 *  @ingroup MasterConfig
 *  @brief Set the prescale factor for the selected input
 *
 *  @param   input Selected trigger input (1-6)
 *  @param   prescale Factor for prescale.
 *               Max {prescale} available is 65535
 *
 *  @return OK if successful, otherwise ERROR.
 */
int
tiSetInputPrescale(int input, int prescale)
{
  unsigned int oldval=0;
  if(TIp==NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  if((prescale<0) || (prescale>0xf))
    {
      printf("%s: ERROR: Invalid prescale (%d).  Must be between 0 and 15.",
	     __FUNCTION__,prescale);
      return ERROR;
    }

  if((input<1) || (input>6))
    {
    {
      printf("%s: ERROR: Invalid input (%d).",
	     __FUNCTION__,input);
      return ERROR;
    }
    }

  TILOCK;
  oldval = vmeRead32(&TIp->inputPrescale) & ~(TI_INPUTPRESCALE_FP_MASK(input));
  vmeWrite32(&TIp->inputPrescale, oldval | (prescale<<(4*(input-1) )) );
  TIUNLOCK;

  return OK;
}


/**
 *  @ingroup Status
 *  @brief Get the current prescale factor for the selected input
 *  @param   input Selected trigger input (1-6)
 *  @return Current prescale factor, otherwise ERROR.
 */
int
tiGetInputPrescale(int input)
{
  int rval;
  if(TIp==NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  rval = vmeRead32(&TIp->inputPrescale) & TI_INPUTPRESCALE_FP_MASK(input);
  rval = rval>>(4*(input-1));
  TIUNLOCK;

  return rval;
}

/**
 *  @ingroup Config
 *  @brief Set the characteristics of a specified trigger
 *
 *  @param trigger
 *           - 1: set for trigger 1
 *           - 2: set for trigger 2 (playback trigger)
 *  @param delay    delay in units of delay_step
 *  @param width    pulse width in units of 4ns
 *  @param delay_step step size of the delay
 *         - 0: 16ns
 *          !0: 64ns (with an offset of ~4.1 us)
 *
 * @return OK if successful, otherwise ERROR
 */
int
tiSetTriggerPulse(int trigger, int delay, int width, int delay_step)
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
  if(delay<0 || delay>0x7F)
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
      if(delay_step)
	rval |= TI_TRIGDELAY_TRIG1_64NS_STEP;

      vmeWrite32(&TIp->trigDelay, rval);
    }
  if(trigger==2)
    {
      rval = vmeRead32(&TIp->trigDelay) &
	~(TI_TRIGDELAY_TRIG2_DELAY_MASK | TI_TRIGDELAY_TRIG2_WIDTH_MASK) ;
      rval |= ( (delay<<16) | (width<<24) );
      if(delay_step)
	rval |= TI_TRIGDELAY_TRIG2_64NS_STEP;

      vmeWrite32(&TIp->trigDelay, rval);
    }
  TIUNLOCK;

  return OK;
}

/**
 *  @ingroup Config
 *  @brief Set the width of the prompt trigger from OT#2
 *
 *  @param width Output width will be set to (width + 2) * 4ns
 *
 *    This routine is only functional for Firmware type=2 (modTI)
 *
 *  @return OK if successful, otherwise ERROR
 */
int
tiSetPromptTriggerWidth(int width)
{
  if(TIp==NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  if((width<0) || (width>TI_PROMPT_TRIG_WIDTH_MASK))
    {
      printf("%s: ERROR: Invalid prompt trigger width (%d)\n",
	     __FUNCTION__,width);
      return ERROR;
    }

  TILOCK;
  vmeWrite32(&TIp->eventNumber_hi, width);
  TIUNLOCK;

  return OK;
}

/**
 *  @ingroup Status
 *  @brief Get the width of the prompt trigger from OT#2
 *
 *    This routine is only functional for Firmware type=2 (modTI)
 *
 *  @return Output width set to (return value + 2) * 4ns, if successful. Otherwise ERROR
 */
int
tiGetPromptTriggerWidth()
{
  unsigned int rval=0;
  if(TIp==NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  rval = vmeRead32(&TIp->eventNumber_hi) & TI_PROMPT_TRIG_WIDTH_MASK;
  TIUNLOCK;

  return rval;
}

/**
 *  @ingroup Config
 *  @brief Set the delay time and width of the Sync signal
 *
 * @param delay  the delay (latency) set in units of 4ns.
 * @param width  the width set in units of 4ns.
 * @param twidth  if this is non-zero, set width in units of 32ns.
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

/**
 * @ingroup MasterConfig
 * @brief Reset the trigger link.
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

/**
 * @ingroup MasterConfig
 * @brief Set type of SyncReset to send to TI Slaves
 *
 * @param type Sync Reset Type
 *    - 0: User programmed width in each TI
 *    - !0: Fixed 4 microsecond width in each TI
 *
 * @return OK if successful, otherwise ERROR
 */
int
tiSetSyncResetType(int type)
{

  if(type)
    tiSyncResetType=TI_SYNCCOMMAND_SYNCRESET_4US;
  else
    tiSyncResetType=TI_SYNCCOMMAND_SYNCRESET;

  return OK;
}


/**
 * @ingroup MasterConfig
 * @brief Generate a Sync Reset signal.  This signal is sent to the loopback and
 *    all configured TI Slaves.
 *
 *  @param blflag Option to change block level, after SyncReset issued
 *       -   0: Do not change block level
 *       -  >0: Broadcast block level to all connected slaves (including self)
 *            BlockLevel broadcasted will be set to library value
 *            (Set with tiSetBlockLevel)
 *
 */
void
tiSyncReset(int blflag)
{
  if(TIp == NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return;
    }

  TILOCK;
  vmeWrite32(&TIp->syncCommand,tiSyncResetType);
  taskDelay(1);
  vmeWrite32(&TIp->syncCommand,TI_SYNCCOMMAND_RESET_EVNUM);
  taskDelay(1);
  TIUNLOCK;

  if(blflag) /* Set the block level from "Next" to Current */
    {
      printf("%s: INFO: Broadcasting Block Level %d, Buffer Level %d\n",
	     __FUNCTION__,
	     tiNextBlockLevel, tiBlockBufferLevel);
      tiBroadcastNextBlockLevel(tiNextBlockLevel);
      tiSetBlockBufferLevel(tiBlockBufferLevel);
    }

}

/**
 * @ingroup MasterConfig
 * @brief Generate a Sync signal to reset only event buffers. This
 *    signal is sent to the loopback and all configured TI Slaves.
 *
 */
void
tiResetEB()
{
  if(TIp == NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return;
    }

  TILOCK;
  vmeWrite32(&TIp->syncCommand,TI_SYNCCOMMAND_RESET_EVNUM);
  taskDelay(1);
  TIUNLOCK;

}

/**
 * @ingroup MasterConfig
 * @brief Generate a Sync Reset Resync signal.  This signal is sent to the loopback and
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
  vmeWrite32(&TIp->syncCommand,tiSyncResetType);
  TIUNLOCK;

}

/**
 * @ingroup MasterConfig
 * @brief Generate a Clock Reset signal.  This signal is sent to the loopback and
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

/**
 * @ingroup Config
 * @brief Routine to set the A32 Base
 *
 * @return OK if successful, otherwise ERROR
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
  res = vmeBusToLocalAdrs(0x09,(char *)(unsigned long)a32base,(char **)&laddr);
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

/**
 * @ingroup Status
 * @brief Routine to get the A32 Base
 *
 * @return A32 base address if successful, otherwise ERROR
 */
unsigned int
tiGetAdr32()
{
  unsigned int rval = 0;
  if(TIp == NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  rval = vmeRead32(&TIp->adr32) & TI_ADR32_BASE_MASK;
  TIUNLOCK;

  return rval;
}

/**
 * @ingroup Config
 * @brief Disable A32
 *
 * @return OK if successful, otherwise ERROR
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

/**
 * @ingroup Config
 * @brief Reset the L1A counter, as incremented by the TI.
 *
 * @return OK if successful, otherwise ERROR
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

/**
 * @ingroup Status
 * @brief Returns the event counter (48 bit)
 *
 * @return Number of accepted events if successful, otherwise ERROR
 */
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

/**
 * @ingroup MasterConfig
 * @brief Set the block number at which triggers will be disabled automatically
 *
 * @return OK if successful, otherwise ERROR
 */
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


/**
 * @ingroup Status
 * @brief Returns the value that is currently programmed as the block limit
 *
 * @return Current Block Limit if successful, otherwise ERROR
 */
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

/**
 * @ingroup Status
 * @brief Get the current status of the block limit
 *
 * @return 1 if block limit has been reached, 0 if not, otherwise ERROR;
 *
 */
int
tiGetBlockLimitStatus()
{
  unsigned int reg=0, rval=0;
  if(TIp == NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  reg = vmeRead32(&TIp->blockBuffer) & TI_BLOCKBUFFER_BUSY_ON_BLOCKLIMIT;
  if(reg)
    rval = 1;
  else
    rval = 0;
  TIUNLOCK;

  return rval;
}


/**
 * @ingroup Readout
 * @brief Returns the number of Blocks available for readout
 *
 * @return Number of blocks available for readout if successful, otherwise ERROR
 *
 */
unsigned int
tiBReady()
{
  unsigned int blockBuffer=0, readyInt=0, rval=0;

  if(TIp == NULL)
    {
      logMsg("tiBReady: ERROR: TI not initialized\n",1,2,3,4,5,6);
      return 0;
    }

  TILOCK;
  blockBuffer = vmeRead32(&TIp->blockBuffer);
  rval        = (blockBuffer&TI_BLOCKBUFFER_BLOCKS_READY_MASK)>>8;
  readyInt    = (blockBuffer&TI_BLOCKBUFFER_BREADY_INT_MASK)>>24;
  tiSyncEventReceived = (blockBuffer&TI_BLOCKBUFFER_SYNCEVENT)>>31;
  tiNReadoutEvents = (blockBuffer&TI_BLOCKBUFFER_RO_NEVENTS_MASK)>>21;
  tiTriggerMissed = (blockBuffer & TI_BLOCKBUFFER_TRIGGER_MISSED) ? 1 : 0;

  if( (readyInt==1) && (tiSyncEventReceived) )
    tiSyncEventFlag = 1;
  else
    tiSyncEventFlag = 0;

  TIUNLOCK;

  return rval;
}

/**
 * @ingroup Readout
 * @brief Return the value of the Synchronization flag, obtained from tiBReady.
 *   i.e. Return the value of the SyncFlag for the current readout block.
 *
 * @sa tiBReady
 * @return
 *   -  1: if current readout block contains a Sync Event.
 *   -  0: Otherwise
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

/**
 * @ingroup Readout
 * @brief Return the value of whether or not the sync event has been received
 *
 * @return
 *     - 1: if sync event received
 *     - 0: Otherwise
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

/**
 * @ingroup Readout
 * @brief Return the value of the number of readout events accepted
 *
 * @return Number of readout events accepted
 */
int
tiGetReadoutEvents()
{
  int rval=0;

  TILOCK;
  rval = tiNReadoutEvents;
  TIUNLOCK;

  return rval;
}


/**
 * @ingroup Readout
 * @brief Return the status of the trigger missed flag (trigger missed due to fifo full)
 *        This bit resets with a syncReset.
 *
 * @return 1 if trigger has been missed, otherwise 0.
 */
int
tiGetTriggerMissedFlag()
{
  int rval=0;

  TILOCK;
  rval = tiTriggerMissed;
  TIUNLOCK;

  return rval;
}

/**
 * @ingroup Config
 * @brief Enable trigger and sync signals sent through the VXS
 *     to the Signal Distribution (SD) module.
 *
 *     This may be required to eliminate the possibility of accidental
 *     signals being sent during Clock Synchronization or Trigger
 *     Enable/Disabling by the TI Master or TS.
 *
 * @sa tiDisableVXSSignals
 * @return OK if successful, otherwise ERROR
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

/**
 * @ingroup Config
 * @brief Disable trigger and sync signals sent through the VXS
 *     to the Signal Distribution (SD) module.
 *
 *     This may be required to eliminate the possibility of accidental
 *     signals being sent during Clock Synchronization or Trigger
 *     Enable/Disabling by the TI Master or TS.
 *
 * @sa tiEnableVXSSignals
 * @return OK if successful, otherwise ERROR
 *
 */
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

/**
 * @ingroup MasterConfig
 * @brief Set the block buffer level for the number of blocks in the system
 *     that need to be read out.
 *
 *     If this buffer level is full, the TI will go BUSY.
 *     The BUSY is released as soon as the number of buffers in the system
 *     drops below this level.
 *
 *  @param     level
 *       -        0:  No Buffer Limit  -  Pipeline mode
 *       -        1:  One Block Limit - "ROC LOCK" mode
 *       -  2-65535:  "Buffered" mode.
 *
 * @return OK if successful, otherwise ERROR
 *
 */
int
tiSetBlockBufferLevel(unsigned int level)
{
  unsigned int trigsrc = 0;

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

  tiBlockBufferLevel = level;

  if(tiMaster)
    {
      /* Broadcast buffer level to TI-slaves */
      trigsrc = vmeRead32(&TIp->trigsrc);

      /* Turn on the VME trigger, if not enabled */
      if(!(trigsrc & TI_TRIGSRC_VME))
	vmeWrite32(&TIp->trigsrc, TI_TRIGSRC_VME | trigsrc);

      /* Broadcast using trigger command */
      vmeWrite32(&TIp->triggerCommand, TI_TRIGGERCOMMAND_SET_BUFFERLEVEL | level);

      /* Turn off the VME trigger, if it was initially disabled */
      if(!(trigsrc & TI_TRIGSRC_VME))
	vmeWrite32(&TIp->trigsrc, trigsrc);
    }

  TIUNLOCK;

  return OK;
}

/**
 *  @ingroup Status
 *  @brief Get the block buffer level, as programmed or broadcasted from the TS
 *
 * @return Block buffer level if successful, otherwise ERROR
 */

int
tiGetBlockBufferLevel()
{
  int rval = 0;

  if(TIp == NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  if(vmeRead32(&TIp->vmeControl) & TI_VMECONTROL_USE_LOCAL_BUFFERLEVEL)
    {
      rval = vmeRead32(&TIp->blockBuffer) & TI_BLOCKBUFFER_BUFFERLEVEL_MASK;
    }
  else
    {
      rval = (vmeRead32(&TIp->dataFormat) & TI_DATAFORMAT_BCAST_BUFFERLEVEL_MASK) >> 24;
    }
  TIUNLOCK;

  return rval;
}

/**
 *  @ingroup Status
 *  @brief Get the block buffer level, as broadcasted from the TS
 *
 * @return Broadcasted block buffer level if successful, otherwise ERROR
 */
int
tiGetBroadcastBlockBufferLevel()
{
  int rval = 0;

  if(TIp == NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  rval =
    (int) ((vmeRead32(&TIp->dataFormat) &
	    TI_DATAFORMAT_BCAST_BUFFERLEVEL_MASK) >> 24);
  TIUNLOCK;

  return rval;
}

/**
 *  @ingroup Config
 *  @brief Set the TI to be BUSY if number of stored blocks is equal to
 *         the set block buffer level
 *
 * @return OK if successful, otherwise ERROR
 */
int
tiBusyOnBufferLevel(int enable)
{
  if(TIp == NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  vmeWrite32(&TIp->vmeControl,
	     vmeRead32(&TIp->vmeControl) | TI_VMECONTROL_BUSY_ON_BUFFERLEVEL);
  TIUNLOCK;

  return OK;
}

/**
 *  @ingroup Config
 *  @brief Enable/Disable the use of the broadcasted buffer level, instead of the
 *         value set locally with @tiSetBlockBufferLevel.
 *
 *  @param enable - 1: Enable, 0: Disable
 *
 * @return OK if successful, otherwise ERROR
 */
int
tiUseBroadcastBufferLevel(int enable)
{
  if(TIp == NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }


  TILOCK;
  if(enable)
    vmeWrite32(&TIp->vmeControl,
	       vmeRead32(&TIp->vmeControl) & ~TI_VMECONTROL_USE_LOCAL_BUFFERLEVEL);
  else
    vmeWrite32(&TIp->vmeControl,
	       vmeRead32(&TIp->vmeControl) | TI_VMECONTROL_USE_LOCAL_BUFFERLEVEL);
  TIUNLOCK;

  return OK;
}

/**
 * @ingroup MasterConfig
 * @brief Enable/Disable trigger inputs labelled TS#1-6 on the Front Panel
 *
 *     These inputs MUST be disabled if not connected.
 *
 *   @param   inpMask
 *       - 0:  TS#1
 *       - 1:  TS#2
 *       - 2:  TS#3
 *       - 3:  TS#4
 *       - 4:  TS#5
 *       - 5:  TS#6
 *
 * @return OK if successful, otherwise ERROR
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

/**
 * @ingroup MasterConfig
 * @brief Disable trigger inputs labelled TS#1-6 on the Front Panel
 *
 *     These inputs MUST be disabled if not connected.
 *
 *   @param   inpMask
 *       - 0:  TS#1
 *       - 1:  TS#2
 *       - 2:  TS#3
 *       - 3:  TS#4
 *       - 4:  TS#5
 *       - 5:  TS#6
 *
 * @return OK if successful, otherwise ERROR
 */
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

/**
 * @ingroup Config
 * @brief Set (or unset) high level for the output ports on the front panel
 *     labelled as O#1-4
 *
 * @param         set1  O#1
 * @param         set2  O#2
 * @param         set3  O#3
 * @param         set4  O#4
 *
 * @return OK if successful, otherwise ERROR
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



/**
 * @ingroup Config
 * @brief Set the clock to the specified source.
 *
 * @param   source
 *         -   0:  Onboard clock
 *         -   1:  External clock (HFBR1 input)
 *         -   5:  External clock (HFBR5 input)
 *
 * @return OK if successful, otherwise ERROR
 */
int
tiSetClockSource(unsigned int source)
{
  int rval=OK, iwait = 0, reg = 0, Locked = 0;
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
    case 5: /* EXTERNAL (HFBR5) */
      clkset = TI_CLOCK_HFBR5;
      sprintf(sClock,"EXTERNAL-HFBR5 (%d)",source);
      break;
    default:
      printf("%s: ERROR: Invalid Clock Souce (%d)\n",__FUNCTION__,source);
      return ERROR;
    }

  printf("%s: Setting clock source to %s\n",__FUNCTION__,sClock);


  TILOCK;
  vmeWrite32(&TIp->clock, clkset);
  taskDelay(10);
  /* Reset DCM (Digital Clock Manager) - 250/200MHz */
  vmeWrite32(&TIp->reset,TI_RESET_CLK250);
  taskDelay(10);
  /* Reset DCM (Digital Clock Manager) - 125MHz */
  vmeWrite32(&TIp->reset,TI_RESET_CLK125);
  taskDelay(10);

  /* Wait for FPGA Ready / Clock DCM locked */
  while(iwait < 100)
    {
      reg = vmeRead32(&TIp->GTPtriggerBufferLength);
      Locked = (reg >> 29) & 0x7;

      if(Locked == 0x7)
	break;

      taskDelay(50);
      iwait ++;
    }

  if(Locked < 0x7)
    {
      printf("%s: ERROR: FPGA is not yet ready.\n",
	     __func__);
      printf("   CLK250 DCM: %s\n", (Locked & (1 << 0)) ? "Locked" :
	     "*** Not Locked ***");
      printf("   CLK125 DCM: %s\n", (Locked & (1 << 1)) ? "Locked" :
	     "*** Not Locked ***");
      printf("   VMECLK DCM: %s\n", (Locked & (1 << 2)) ? "Locked" :
	     "*** Not Locked ***");

      rval = ERROR;
    }

  if(source==1) /* Turn on running mode for External Clock verification */
    {
      vmeWrite32(&TIp->runningMode,TI_RUNNINGMODE_ENABLE);
      taskDelay(5);
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

/**
 * @ingroup Status
 * @brief Get the current clock source
 * @return Current Clock Source
 */
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

/**
 * @ingroup Config
 * @brief Set the fiber delay required to align the sync and triggers for all crates.
 */
void
tiSetFiberDelay(unsigned int delay, unsigned int offset)
{
  unsigned int syncDelay=0, syncDelay_write=0;
  if(TIp == NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return;
    }

  TILOCK;

  if(delay>offset)
    {
      printf("%s: WARN: delay (%d) greater than offset (%d).  Setting difference to zero\n",
	     __FUNCTION__,delay,offset);
      syncDelay = 0;
    }
  else
    {
      syncDelay = (offset-(delay));
    }

  /* set the sync delay according to the fiber latency */
  syncDelay_write = ((syncDelay & 0xff) << 8) |
    ((syncDelay & 0xff) << 16) | ((syncDelay & 0xff) << 24);

  vmeWrite32(&TIp->fiberSyncDelay, syncDelay_write);

  TIUNLOCK;

  printf("%s: Wrote 0x%08x to fiberSyncDelay\n",
	 __FUNCTION__, syncDelay_write);

}

/**
 * @ingroup Config
 * @brief Set the fiber delay required to align the sync and triggers for all crates.
 */
void
tiSetFiberSyncDelay(unsigned int syncDelay)
{
  unsigned int syncDelay_write=0;
  if(TIp == NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return;
    }

  if(syncDelay > 0xFF)
    {
      printf("%s: ERROR: Invalid syncDelay (0x%x)\n",
	     __func__, syncDelay);
      return;
    }

  TILOCK;

  /* set the sync delay according to the fiber latency */
  syncDelay_write = ((syncDelay & 0xff) << 8) |
    ((syncDelay & 0xff) << 16) | ((syncDelay & 0xff) << 24);

  vmeWrite32(&TIp->fiberSyncDelay, syncDelay_write);

  TIUNLOCK;

  printf("%s: Wrote 0x%08x to fiberSyncDelay\n",
	 __FUNCTION__, syncDelay_write);

}

/**
 * @ingroup Status
 * @brief Get the fiber delay required to align the sync and triggers for all crates.
 * @return Current fiber delay setting
 */
int
tiGetFiberDelay()
{
  int rval = 0;

  if(TIp == NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;

  rval = vmeRead32(&TIp->fiberSyncDelay);

  if (tiMaster)
    {
      rval = (rval & TI_FIBERSYNCDELAY_LOOPBACK_SYNCDELAY_MASK) >> 16;
    }
  else
    {
      if (tiSlaveFiberIn == 1)
	{
	  rval = (rval & TI_FIBERSYNCDELAY_HFBR1_SYNCDELAY_MASK) >> 8;
	}
      else if (tiSlaveFiberIn == 5)
	{
	  rval = (rval & TI_FIBERSYNCDELAY_HFBR5_SYNCDELAY_MASK) >> 24;
	}
      else
	{
	  rval = ERROR;
	}
    }

  TIUNLOCK;

  if (rval == ERROR)
    {
      printf("%s: ERROR: Invalid value for tiSlaveFiberIn (%d)\n",
	     __func__, tiSlaveFiberIn);
    }

  return rval;
}

/**
 *  @ingroup MasterConfig
 *  @brief Reset the configuration of TI Slaves on the TI-Master.
 *
 *      This routine removes all slaves and resets the fiber port busys.
 *
 *  @return OK if successful, ERROR otherwise
 *
 */
int
tiResetSlaveConfig()
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
  tiSlaveMask = 0;
  vmeWrite32(&TIp->busy, (vmeRead32(&TIp->busy) & ~TI_BUSY_HFBR_MASK));
  TIUNLOCK;

  return OK;
}

/**
 * @ingroup MasterConfig
 * @brief Add and configurate a TI Slave for the TI Master.
 *
 *      This routine should be used by the TI Master to configure
 *      HFBR porti and BUSY sources.
 *
 * @param    fiber  The fiber port of the TI Master that is connected to the slave
 *
 * @sa tiAddSlaveMask
 *
 * @return OK if successful, otherwise ERROR
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

  if(tiUseTsRev2)
    {
      printf("%s: ERROR: Invalid usage with TS rev 2.\n",
	     __func__);
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

/**
 *  @ingroup MasterConfig
 *  @brief Remove a TI Slave
 *
 *  @param fiber  The fiber port of the TI master to remove.
 *
 *  @return OK if successful, ERROR otherwise
 */
int
tiRemoveSlave(unsigned int fiber)
{
  unsigned int busybits;
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

  if(tiUseTsRev2)
    {
      printf("%s: ERROR: Invalid usage with TS rev 2.\n",
	     __func__);
      return ERROR;
    }

  if((fiber<1) || (fiber>8) )
    {
      printf("%s: ERROR: Invalid value for fiber (%d)\n",
	     __FUNCTION__,fiber);
      return ERROR;
    }

  /* Remove this slave to the global slave mask */
  tiSlaveMask &= ~(1<<(fiber-1));

  /* Remove this fiber as a busy source (use first fiber macro as the base) */
  TILOCK;
  /* Read in previous values, keeping current busy's */
  busybits = vmeRead32(&TIp->busy);

  /* Turn off busy to the fiber in question */
  busybits &= ~(1<<(TI_BUSY_HFBR1-1+fiber));

  /* Write the new mask */
  vmeWrite32(&TIp->busy, busybits);
  TIUNLOCK;

  return OK;
}

/**
 * @ingroup MasterConfig
 * @brief Add and configure  TI Slaves by using a mask for the TI-Master.
 *
 *      This routine should be used by the TI-Master to configure
 *      HFBR ports and BUSY sources.
 *
 *  @param   fibermask The fiber port mask of the TI-Master that is connected to
 *     the slaves
 *
 * @sa tiAddSlave
 */
int
tiAddSlaveMask(unsigned int fibermask)
{
  int ibit=0;

  if(TIp == NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  if(tiUseTsRev2)
    {
      printf("%s: ERROR: Invalid usage with TS rev 2.\n",
	     __func__);
      return ERROR;
    }

  if((fibermask==0) || (fibermask>0x100))
    {
      printf("%s: ERROR: Invalid value for fibermask (0x%x)\n",
	     __FUNCTION__,fibermask);
      return ERROR;
    }

  if(fibermask & (1<<0))
    {
      printf("%s: WARN: Unused bit 0 in fibermask (0x%x)\n",
	     __FUNCTION__,fibermask);
    }

  for(ibit=0; ibit<8; ibit++)
    {
      if(fibermask & (1<<ibit))
	tiAddSlave(ibit+1);
    }

  return OK;

}

static int tiTriggerRuleClockPrescale[3][4] =
  {
    {4, 4, 8, 16}, // 250 MHz ref
    {16, 32, 64, 128}, // 33.3 MHz ref
    {16, 32, 64, 128} // 33.3 MHz ref prescaled by 32
  };

/**
 * @ingroup MasterConfig
 * @brief Set the value for a specified trigger rule.
 *
 * @param   rule  the number of triggers within some time period..
 *            e.g. rule=1: No more than ONE trigger within the
 *                         specified time period
 *
 * @param   value  the specified time period (in steps of timestep)
 * @param timestep Timestep that is dependent on the trigger rule selected
 *<pre>
 *                           rule
 *    timestep    1       2       3       4
 *    --------  ------ ------- ------- --------
 *       0        16ns    16ns    32ns     64ns
 *       1       480ns   960ns  1920ns   3840ns
 *       2     15360ns 30720ns 61440ns 122880ns
 *</pre>
 *
 * @return OK if successful, otherwise ERROR.
 *
 */
int
tiSetTriggerHoldoff(int rule, unsigned int value, int timestep)
{
  unsigned int wval = 0, rval = 0;
  unsigned int maxvalue = 0x7f;
  unsigned int vmeControl = 0;
  static int slow_clock_previously_switched = 0;

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
  vmeControl = vmeRead32(&TIp->vmeControl);

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

  if(timestep==2)
    {
      if(!(vmeControl & TI_VMECONTROL_SLOWER_TRIGGER_RULES))
	{
	  if(slow_clock_previously_switched == 1)
	    {
	      printf("%s: WARNING: Using slower clock for trigger rules.\n",
		     __FUNCTION__);
	      printf("\tThis may affect previously set rules!\n");
	    }
	  vmeWrite32(&TIp->vmeControl,
		     vmeControl | TI_VMECONTROL_SLOWER_TRIGGER_RULES);
	  slow_clock_previously_switched = 1;
	}
    }
  else
    {
      if(vmeControl & TI_VMECONTROL_SLOWER_TRIGGER_RULES)
	{
	  if(slow_clock_previously_switched == 1)
	    {
	      printf("%s: WARNING: Using faster clock for trigger rules.\n",
		     __FUNCTION__);
	      printf("\tThis may affect previously set rules!\n");
	    }
	  vmeWrite32(&TIp->vmeControl,
		     vmeControl & ~TI_VMECONTROL_SLOWER_TRIGGER_RULES);
	  slow_clock_previously_switched = 1;
	}
    }
  TIUNLOCK;

  return OK;

}

/**
 * @ingroup Status
 * @brief Get the value for a specified trigger rule.
 *
 * @param   rule   the number of triggers within some time period..
 *            e.g. rule=1: No more than ONE trigger within the
 *                         specified time period
 *
 * @return If successful, returns the value (in steps of 16ns)
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
      printf("%s: ERROR: Invalid value for rule (%d).  Must be 1-4.\n",
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

int
tiPrintTriggerHoldoff(int dflag)
{
  unsigned long TIBase = 0;
  unsigned int triggerRule = 0, triggerRuleMin = 0, vmeControl = 0;
  int irule = 0, slowclock = 0, clockticks = 0, timestep = 0, minticks = 0;
  float clock[3] = {250, 33.3, 33.3/32.}, stepsize = 0., time = 0., min = 0.;

  if(TIp == NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  triggerRule    = vmeRead32(&TIp->triggerRule);
  triggerRuleMin = vmeRead32(&TIp->triggerRuleMin);
  vmeControl     = vmeRead32(&TIp->vmeControl);
  TIUNLOCK;

  if(dflag)
    {
      printf("  Registers:\n");
      TIBase = (unsigned long)TIp;
      printf("   triggerRule    (0x%04lx) = 0x%08x\t",
	     (unsigned long)(&TIp->triggerRule) - TIBase, triggerRule);
      printf(" triggerRuleMin (0x%04lx) = 0x%08x\n",
	     (unsigned long)(&TIp->triggerRuleMin) - TIBase, triggerRuleMin);
    }

  printf("\n");
  printf("    Rule   Timesteps    + Up to     Minimum  ");
  if(dflag)
    printf("  ticks   clock   prescale\n");
  else
    printf("\n");
  printf("    ----   ---[ns]---  ---[ns]---  ---[ns]---");
  if(dflag)
    printf("  -----  -[MHz]-  --------\n");
  else
    printf("\n");

  slowclock = (vmeControl & (1 << 31)) >> 31;
  for(irule = 0; irule < 4; irule++)
    {
      clockticks = (triggerRule >> (irule*8)) & 0x7F;
      timestep   = ((triggerRule >> (irule*8)) >> 7) & 0x1;
      if((triggerRuleMin >> (irule*8)) & 0x80)
	minticks = (triggerRuleMin >> (irule*8)) & 0x7F;
      else
	minticks = 0;

      if((timestep == 1) && (slowclock == 1))
	{
	  timestep = 2;
	}

      stepsize = ((float) tiTriggerRuleClockPrescale[timestep][irule] /
		  (float) clock[timestep]);

      time = (float)clockticks * stepsize;

      min = (float) minticks * stepsize;

      printf("    %4d     %8.1f    %8.1f    %8.1f ",
	     irule + 1, 1E3 * time, 1E3 * stepsize, min);

      if(dflag)
	printf("   %3d    %5.1f       %3d\n",
	       clockticks, clock[timestep],
	       tiTriggerRuleClockPrescale[timestep][irule]);
      else
	printf("\n");

    }
  printf("\n");

  return OK;
}

/**
 * @ingroup MasterConfig
 * @brief Set the value for the minimum time of specified trigger rule.
 *
 * @param   rule  the number of triggers within some time period..
 *            e.g. rule=1: No more than ONE trigger within the
 *                         specified time period
 *
 * @param   value  the specified time period (in steps of timestep)
 *<pre>
 *       	 	      rule
 *    		         2      3      4
 *    		       ----- ------ ------
 *    		        16ns  480ns  480ns
 *</pre>
 *
 * @return OK if successful, otherwise ERROR.
 *
 */
int
tiSetTriggerHoldoffMin(int rule, unsigned int value)
{
  unsigned int mask=0, enable=0, shift=0;
  if(TIp == NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  if(rule<2 || rule>5)
    {
      printf("%s: ERROR: Invalid rule (%d).  Must be 2-4.\n",
	     __FUNCTION__,rule);
      return ERROR;
    }

  if(value > 0x7f)
    {
      printf("%s: ERROR: Invalid value (%d). Must be less than %d.\n",
	     __FUNCTION__,value,0x7f);
      return ERROR;
    }

  switch(rule)
    {
    case 2:
      mask = ~(TI_TRIGGERRULEMIN_MIN2_MASK | TI_TRIGGERRULEMIN_MIN2_EN);
      enable = TI_TRIGGERRULEMIN_MIN2_EN;
      shift = 8;
      break;
    case 3:
      mask = ~(TI_TRIGGERRULEMIN_MIN3_MASK | TI_TRIGGERRULEMIN_MIN3_EN);
      enable = TI_TRIGGERRULEMIN_MIN3_EN;
      shift = 16;
      break;
    case 4:
      mask = ~(TI_TRIGGERRULEMIN_MIN4_MASK | TI_TRIGGERRULEMIN_MIN4_EN);
      enable = TI_TRIGGERRULEMIN_MIN4_EN;
      shift = 24;
      break;
    }

  TILOCK;
  vmeWrite32(&TIp->triggerRuleMin,
	     (vmeRead32(&TIp->triggerRuleMin) & mask) |
	     enable |
	     (value << shift) );
  TIUNLOCK;

  return OK;
}

/**
 * @ingroup Status
 * @brief Get the value for a specified trigger rule minimum busy.
 *
 * @param   rule   the number of triggers within some time period..
 *            e.g. rule=1: No more than ONE trigger within the
 *                         specified time period
 *
 * @param  pflag  if not 0, print the setting to standard out.
 *
 * @return If successful, returns the value
 *          (in steps of 16ns for rule 2, 480ns otherwise)
 *            for the specified rule. ERROR, otherwise.
 *
 */
int
tiGetTriggerHoldoffMin(int rule, int pflag)
{
  int rval=0;
  unsigned int mask=0, shift=0;
  if(TIp == NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  if(rule<2 || rule>5)
    {
      printf("%s: ERROR: Invalid rule (%d).  Must be 2-4.\n",
	     __FUNCTION__,rule);
      return ERROR;
    }

  switch(rule)
    {
    case 2:
      mask = TI_TRIGGERRULEMIN_MIN2_MASK;
      shift = 8;
      break;
    case 3:
      mask = TI_TRIGGERRULEMIN_MIN3_MASK;
      shift = 16;
      break;
    case 4:
      mask = TI_TRIGGERRULEMIN_MIN4_MASK;
      shift = 24;
      break;
    }

  TILOCK;
  rval = (vmeRead32(&TIp->triggerRuleMin) & mask)>>shift;
  TIUNLOCK;

  if(pflag)
    {
      printf("%s: Trigger rule %d  minimum busy = %d - %s\n",
	     __FUNCTION__,rule,
	     rval & 0x7f,
	     (rval & (1<<7))?"ENABLED":"DISABLED");
    }

  return rval & ~(1<<8);
}

/**
 *  @ingroup Config
 *  @brief Disable the necessity to readout the TI for every block.
 *
 *      For instances when the TI data is not required for analysis
 *      When a block is "ready", a call to tiResetBlockReadout must be made.
 *
 * @sa tiEnableDataReadout tiResetBlockReadout
 * @return OK if successful, otherwise ERROR
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

/**
 *  @ingroup Config
 *  @brief Enable readout the TI for every block.
 *
 * @sa tiDisableDataReadout
 * @return OK if successful, otherwise ERROR
 */
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


/**
 *  @ingroup Readout
 *  @brief Decrement the hardware counter for blocks available, effectively
 *      simulating a readout from the data fifo.
 *
 * @sa tiDisableDataReadout
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

/**
 * @ingroup MasterConfig
 * @brief Configure trigger table to be loaded with a user provided array.
 *
 * @param itable Input Table (Array of 16 4byte words)
 *
 * @return OK if successful, otherwise ERROR
 */
int
tiTriggerTableConfig(unsigned int *itable)
{
  int ielement=0;

  if(itable==NULL)
    {
      printf("%s: ERROR: Invalid input table address\n",
	     __FUNCTION__);
      return ERROR;
    }

  for(ielement=0; ielement<16; ielement++)
    tiTrigPatternData[ielement] = itable[ielement];

  return OK;
}

/**
 * @ingroup MasterConfig
 * @brief Get the current trigger table stored in local memory (not necessarily on TI).
 *
 * @param otable Output Table (Array of 16 4byte words, user must allocate memory)
 *
 * @return OK if successful, otherwise ERROR
 */
int
tiGetTriggerTable(unsigned int *otable)
{
  int ielement=0;

  if(otable==NULL)
    {
      printf("%s: ERROR: Invalid output table address\n",
	     __FUNCTION__);
      return ERROR;
    }

  for(ielement=0; ielement<16; ielement++)
    otable[ielement] = tiTrigPatternData[ielement];

  return OK;
}

/**
 * @ingroup MasterConfig
 * @brief Configure trigger tabled to be loaded with a predefined
 * trigger table (mapping TS inputs to trigger types).
 *
 * @param mode
 *  - 0:
 *    - TS#1,2,3,4,5 generates Trigger1 (physics trigger),
 *    - TS#6 generates Trigger2 (playback trigger),
 *    - No SyncEvent;
 *  - 1:
 *    - TS#1,2,3 generates Trigger1 (physics trigger),
 *    - TS#4,5,6 generates Trigger2 (playback trigger).
 *    - If both Trigger1 and Trigger2, they are SyncEvent;
 *  - 2:
 *    - TS#1,2,3,4,5 generates Trigger1 (physics trigger),
 *    - TS#6 generates Trigger2 (playback trigger),
 *    - If both Trigger1 and Trigger2, generates SyncEvent;
 *  - 3:
 *    - TS#1,2,3,4,5,6 generates Trigger1 (physics trigger),
 *    - No Trigger2 (playback trigger),
 *    - No SyncEvent;
 *
 * @return OK if successful, otherwise ERROR
 */
int
tiTriggerTablePredefinedConfig(int mode)
{
  int ielement=0;
  unsigned int trigPattern[4][16] =
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
      },
      { /* mode 3:
           TS#1,2,3,4,5,6 generates Trigger1 (physics trigger),
           No Trigger2 (playback trigger),
           No SyncEvent;
        */
        0x43424100, 0x47464544, 0x4b4a4948, 0x4f4e4d4c,
        0x53525150, 0x57565554, 0x5b5a5958, 0x5f5e5d5c,
        0x63626160, 0x67666564, 0x6b6a6968, 0x6f6e6d6c,
        0x73727170, 0x77767574, 0x7b7a7978, 0x7f7e7d7c,
      }
    };

  if(mode>3)
    {
      printf("%s: WARN: Invalid mode %d.  Using Trigger Table mode = 0\n",
	     __FUNCTION__,mode);
      mode=0;
    }

  /* Copy predefined choice into static array to be loaded */

  for(ielement=0; ielement<16; ielement++)
    {
      tiTrigPatternData[ielement] = trigPattern[mode][ielement];
    }

  return OK;
}

/**
 * @ingroup MasterConfig
 * @brief Define a specific trigger pattern as a hardware trigger (trig1/trig2/syncevent)
 * and Event Type
 *
 * @param trigMask Trigger Pattern (must be less than 0x3F)
 *    - TS inputs defining the pattern.  Starting bit: TS#1 = bit0
 * @param hwTrig Hardware trigger type (must be less than 3)
 *      0:  no trigger
 *      1:  Trig1 (event trigger)
 *      2:  Trig2 (playback trigger)
 *      3:  SyncEvent
 * @param evType Event Type (must be less than 255)
 *
 * @return OK if successful, otherwise ERROR
 */

int
tiDefineEventType(int trigMask, int hwTrig, int evType)
{
  int element=0, byte=0;
  int data=0;
  unsigned int old_pattern=0;

  if(trigMask>0x3f)
    {
      printf("%s: ERROR: Invalid trigMask (0x%x)\n",
	     __FUNCTION__, trigMask);
      return ERROR;
    }

  if(hwTrig>3)
    {
      printf("%s: ERROR: Invalid hwTrig (%d)\n",
	     __FUNCTION__, hwTrig);
      return ERROR;
    }

  if(evType>0x3F)
    {
      printf("%s: ERROR: Invalid evType (%d)\n",
	     __FUNCTION__, evType);
      return ERROR;
    }

  element = trigMask/4;
  byte    = trigMask%4;

  data    = (hwTrig<<6) | evType;

  old_pattern = (tiTrigPatternData[element] & ~(0xFF<<(byte*8)));
  tiTrigPatternData[element] = old_pattern | (data<<(byte*8));

  return OK;
}

/**
 * @ingroup MasterConfig
 * @brief Define the event type for the TI Master's fixed and random internal trigger.
 *
 * @param fixed_type Fixed Pulser Event Type
 * @param random_type Pseudo Random Pulser Event Type
 *
 * @return OK if successful, otherwise ERROR
 */
int
tiDefinePulserEventType(int fixed_type, int random_type)
{
  if(TIp == NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  if((fixed_type<0)||(fixed_type>0xFF))
    {
      printf("%s: ERROR: Invalid fixed_type (0x%x)\n",__FUNCTION__,fixed_type);
      return ERROR;
    }

  if((random_type<0)||(random_type>0xFF))
    {
      printf("%s: ERROR: Invalid random_type (0x%x)\n",__FUNCTION__,random_type);
      return ERROR;
    }

  TILOCK;
  vmeWrite32(&TIp->pulserEvType,
	     (fixed_type)<<16 | (random_type)<<24);
  TIUNLOCK;

  return OK;
}

/**
 * @ingroup MasterConfig
 * @brief Load a predefined trigger table (mapping TS inputs to trigger types).
 *
 * @param mode
 *  - 0:
 *    - TS#1,2,3,4,5 generates Trigger1 (physics trigger),
 *    - TS#6 generates Trigger2 (playback trigger),
 *    - No SyncEvent;
 *  - 1:
 *    - TS#1,2,3 generates Trigger1 (physics trigger),
 *    - TS#4,5,6 generates Trigger2 (playback trigger).
 *    - If both Trigger1 and Trigger2, they are SyncEvent;
 *  - 2:
 *    - TS#1,2,3,4,5 generates Trigger1 (physics trigger),
 *    - TS#6 generates Trigger2 (playback trigger),
 *    - If both Trigger1 and Trigger2, generates SyncEvent;
 *  - 3:
 *    - TS#1,2,3,4,5,6 generates Trigger1 (physics trigger),
 *    - No Trigger2 (playback trigger),
 *    - No SyncEvent;
 *  - 4:
 *    User configured table @sa tiDefineEventType, tiTriggerTablePredefinedConfig
 *
 * @return OK if successful, otherwise ERROR
 */
int
tiLoadTriggerTable(int mode)
{
  int ipat;

  if(TIp == NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  if(tiUseTsRev2)
    {
      printf("%s: ERROR: Invalid usage with TS rev 2.\n",
	     __func__);
      return ERROR;
    }

  if(mode>4)
    {
      printf("%s: WARN: Invalid mode %d.  Using Trigger Table mode = 0\n",
	     __FUNCTION__,mode);
      mode=0;
    }

  if(mode!=4)
    tiTriggerTablePredefinedConfig(mode);

  TILOCK;
  for(ipat=0; ipat<16; ipat++)
    vmeWrite32(&TIp->trigTable[ipat], tiTrigPatternData[ipat]);

  TIUNLOCK;

  return OK;
}

/**
 * @ingroup MasterStatus
 * @brief Print trigger table to standard out.
 *
 * @param showbits Show trigger bit pattern, instead of hex
 *
 */
void
tiPrintTriggerTable(int showbits)
{
  int ielement, ibyte;
  int hwTrig=0, evType=0;

  for(ielement = 0; ielement<16; ielement++)
    {
      if(showbits)
	{
	  printf("--TS INPUT-\n");
	  printf("1 2 3 4 5 6  HW evType\n");
	}
      else
	{
	  printf("TS Pattern  HW evType\n");
	}

      for(ibyte=0; ibyte<4; ibyte++)
	{
	  hwTrig = ((tiTrigPatternData[ielement]>>(ibyte*8)) & 0xC0)>>6;
	  evType = (tiTrigPatternData[ielement]>>(ibyte*8)) & 0x3F;

	  if(showbits)
	    {
	      printf("%d %d %d %d %d %d   %d   %2d\n",
		     ((ielement*4+ibyte) & (1<<0))?1:0,
		     ((ielement*4+ibyte) & (1<<1))?1:0,
		     ((ielement*4+ibyte) & (1<<2))?1:0,
		     ((ielement*4+ibyte) & (1<<3))?1:0,
		     ((ielement*4+ibyte) & (1<<4))?1:0,
		     ((ielement*4+ibyte) & (1<<5))?1:0,
		     hwTrig, evType);
	    }
	  else
	    {
	      printf("  0x%02x       %d   %2d\n", ielement*4+ibyte,hwTrig, evType);
	    }
	}
      printf("\n");

    }


}


/**
 *  @ingroup MasterConfig
 *  @brief Set the window of the input trigger coincidence window
 *  @param window_width Width of the input coincidence window (units of 4ns)
 *  @return OK if successful, otherwise ERROR
 */
int
tiSetTriggerWindow(int window_width)
{
  if(TIp == NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  if((window_width<1) || (window_width>TI_TRIGGERWINDOW_COINC_MASK))
    {
      printf("%s: ERROR: Invalid Trigger Coincidence Window (%d)\n",
	     __FUNCTION__,window_width);
      return ERROR;
    }

  TILOCK;
  vmeWrite32(&TIp->triggerWindow,
	     (vmeRead32(&TIp->triggerWindow) & ~TI_TRIGGERWINDOW_COINC_MASK)
	     | window_width);
  TIUNLOCK;

  return OK;
}

/**
 *  @ingroup MasterStatus
 *  @brief Get the window of the input trigger coincidence window
 *  @return Width of the input coincidence window (units of 4ns), otherwise ERROR
 */
int
tiGetTriggerWindow()
{
  int rval=0;
  if(TIp == NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  rval = vmeRead32(&TIp->triggerWindow) & TI_TRIGGERWINDOW_COINC_MASK;
  TIUNLOCK;

  return rval;
}

/**
 *  @ingroup MasterConfig
 *  @brief Set the width of the input trigger inhibit window
 *  @param window_width Width of the input inhibit window (units of 4ns)
 *  @return OK if successful, otherwise ERROR
 */
int
tiSetTriggerInhibitWindow(int window_width)
{
  if(TIp == NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  if((window_width<1) || (window_width>(TI_TRIGGERWINDOW_INHIBIT_MASK>>8)))
    {
      printf("%s: ERROR: Invalid Trigger Inhibit Window (%d)\n",
	     __FUNCTION__,window_width);
      return ERROR;
    }

  TILOCK;
  vmeWrite32(&TIp->triggerWindow,
	     (vmeRead32(&TIp->triggerWindow) & ~TI_TRIGGERWINDOW_INHIBIT_MASK)
	     | (window_width<<8));
  TIUNLOCK;

  return OK;
}

/**
 *  @ingroup MasterStatus
 *  @brief Get the width of the input trigger inhibit window
 *  @return Width of the input inhibit window (units of 4ns), otherwise ERROR
 */
int
tiGetTriggerInhibitWindow()
{
  int rval=0;
  if(TIp == NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  rval = (vmeRead32(&TIp->triggerWindow) & TI_TRIGGERWINDOW_INHIBIT_MASK)>>8;
  TIUNLOCK;

  return rval;
}

/**
 *  @ingroup MasterConfig
 *  @brief Set the delay of Trig1 relative to Trig2 when trigger source is 11.
 *
 *  @param delay Trig1 delay after Trig2
 *    - Latency in steps of 4 nanoseconds with an offset of ~2.6 microseconds
 *
 *  @return OK if successful, otherwise ERROR
 */

int
tiSetTrig21Delay(int delay)
{
  if(TIp == NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  if(delay>0x1FF)
    {
      printf("%s: ERROR: Invalid delay (%d)\n",
	     __FUNCTION__,delay);
      return ERROR;
    }

  TILOCK;
  vmeWrite32(&TIp->triggerWindow,
	     (vmeRead32(&TIp->triggerWindow) & ~TI_TRIGGERWINDOW_TRIG21_MASK) |
	     (delay<<16));
  TIUNLOCK;
  return OK;
}

/**
 *  @ingroup MasterStatus
 *  @brief Get the delay of Trig1 relative to Trig2 when trigger source is 11.
 *
 *  @return Latency in steps of 4 nanoseconds with an offset of ~2.6 microseconds, otherwise ERROR
 */

int
tiGetTrig21Delay()
{
  int rval=0;
  if(TIp == NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  rval = (vmeRead32(&TIp->triggerWindow) & TI_TRIGGERWINDOW_TRIG21_MASK)>>16;
  TIUNLOCK;

  return rval;
}

/**
 *  @ingroup MasterConfig
 *  @brief Set the trigger latch pattern readout in the data stream to include
 *          the Level of the input trigger OR the transition to Hi.
 *
 *  @param enable
 *      1 to enable
 *     <1 to disable
 *
 *  @return OK if successful, otherwise ERROR
 */

int
tiSetTriggerLatchOnLevel(int enable)
{
  if(TIp == NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  if(enable < 1)
    enable = 0;

  TILOCK;
  vmeWrite32(&TIp->triggerWindow,
	     (vmeRead32(&TIp->triggerWindow) & ~TI_TRIGGERWINDOW_LEVEL_LATCH) |
	     (enable<<31));
  TIUNLOCK;
  return OK;
}

/**
 *  @ingroup MasterStatus
 *  @brief Get the trigger latch pattern readout in the data stream to include
 *          the Level of the input trigger OR the transition to Hi.
 *
 *  @return 1 if enabled, 0 if disabled, otherwise ERROR
 */

int
tiGetTriggerLatchOnLevel()
{
  int rval=0;
  if(TIp == NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  rval = (vmeRead32(&TIp->triggerWindow) & TI_TRIGGERWINDOW_LEVEL_LATCH)>>31;
  TIUNLOCK;

return rval;
}

/**
 *  @ingroup MasterConfig
 *  @brief Latch the Busy and Live Timers.
 *
 *     This routine should be called prior to a call to tiGetLiveTime and tiGetBusyTime
 *
 *  @sa tiGetLiveTime
 *  @sa tiGetBusyTime
 *
 * @return OK if successful, otherwise ERROR
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

/**
 * @ingroup Status
 * @brief Return the current "live" time of the module
 *
 * @returns The current live time in units of 7.68 us
 *
 */
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

/**
 * @ingroup Status
 * @brief Return the current "busy" time of the module
 *
 * @returns The current live time in units of 7.68 us
 *
 */
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

/**
 * @ingroup Status
 * @brief Calculate the live time (percentage) from the live and busy time scalers
 *
 * @param sflag if > 0, then returns the integrated live time
 *
 * @return live time as a 3 digit integer % (e.g. 987 = 98.7%)
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
  vmeWrite32(&TIp->reset,TI_RESET_SCALERS_LATCH);
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


/**
 * @ingroup Status
 * @brief Get the current counter for the specified TS Input
 *
 * @param input
 *   - 1-6 : TS Input (1-6)
 * @param latch:
 *   -  0: Do not latch before readout
 *   -  1: Latch before readout
 *   -  2: Latch and reset before readout
 *
 *
 * @return Specified counter value
 *
 */
unsigned int
tiGetTSscaler(int input, int latch)
{
  unsigned int rval=0;
  if(TIp == NULL)
    {
      logMsg("tiGetTSscaler: ERROR: TI not initialized\n",1,2,3,4,5,6);
      return ERROR;
    }

  if((input<1)||(input>6))
    {
      logMsg("tiGetTSscaler: ERROR: Invalid input (%d).\n",
	     input,2,3,4,5,6);
      return ERROR;
    }

  if((latch<0) || (latch>2))
    {
      logMsg("tiGetTSscaler: ERROR: Invalid latch (%d).\n",
	     latch,2,3,4,5,6);
      return ERROR;
    }

  TILOCK;
  switch(latch)
    {
    case 1:
      vmeWrite32(&TIp->reset,TI_RESET_SCALERS_LATCH);
      break;

    case 2:
      vmeWrite32(&TIp->reset,TI_RESET_SCALERS_LATCH | TI_RESET_SCALERS_RESET);
      break;
    }

  rval = vmeRead32(&TIp->ts_scaler[input-1]);
  TIUNLOCK;

  return rval;
}

/**
 * @ingroup Status
 * @brief Show block Status of specified fiber
 * @param fiber  Fiber port to show
 * @param pflag  Whether or not to print to standard out
 * @return 0
 */
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
      rval = (vmeRead32(&TIp->adr24) & 0xFFFF0000)>>16;
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

unsigned char *measurement;
int maxCount = 0, maxIndex = 0;
int FiberMeasMaxCount()
{
  return maxCount;
}

int FiberMeasMaxIndex()
{
  return maxIndex;
}

void FiberMeasHisto()
{
  int imeas;
  for(imeas = 0; imeas < 256; imeas++)
    {
      printf("%s: %2d: measurement = %d  %s\n",
	     __func__, imeas, measurement[imeas],
	     (imeas==maxIndex)?"***":"");
    }
}

static int
FiberMeas()
{
  int clksrc=0, imeas = 0, ntries = 5;
  unsigned int defaultDelay=0x1f1f1f00, fiberLatency=0, syncDelay=0, syncDelay_write=0;
  int index = 0;
  int failed = 0;
  int rval = OK;

  measurement = (unsigned char *)malloc(256*sizeof(unsigned char));
  memset((void *)measurement, 0, 256*sizeof(unsigned char));

  clksrc = tiGetClockSource();
  /* Check to be sure the TI has external HFBR1/5 clock enabled */
  if((clksrc != TI_CLOCK_HFBR1) && (clksrc != TI_CLOCK_HFBR5))
    {
      printf("%s: ERROR: Unable to measure fiber latency without HFBR1/5 as Clock Source\n",
	     __FUNCTION__);
      printf("\t Using default Fiber Sync Delay = %d (0x%x)",
	     defaultDelay, defaultDelay);

      TILOCK;
      vmeWrite32(&TIp->fiberSyncDelay,defaultDelay);
      TIUNLOCK;

      free(measurement);
      return ERROR;
    }

  TILOCK;
  for(imeas = 0; imeas < ntries; imeas++)
    {
#ifdef SKIPIODELAY
      /* Reset the IODELAY */
      vmeWrite32(&TIp->reset,TI_RESET_IODELAY);
      tiWaitForIODelayReset(10);
#endif
      /* Auto adjust the return signal phase */
      vmeWrite32(&TIp->reset,TI_RESET_FIBER_AUTO_ALIGN);
      taskDelay(20);

      /* Measure the fiber latency */
      vmeWrite32(&TIp->reset,TI_RESET_MEASURE_LATENCY);
      taskDelay(2);

      /* Get the fiber latency measurement result */
      if(tiSlaveFiberIn==1)
	fiberLatency = vmeRead32(&TIp->fiberLatencyMeasurement);
      else
	fiberLatency = vmeRead32(&TIp->fiberAlignment);

#ifdef DEBUGFIBERMEAS
      printf("Software offset = 0x%08x (%d)\n",
	     tiFiberLatencyOffset, tiFiberLatencyOffset);
      printf("Fiber Latency is 0x%08x\n",
	     fiberLatency);
      printf("  Latency data = 0x%08x (%d ns)\n",
	     (fiberLatency>>23), (fiberLatency>>23) * 4);
#endif /* DEBUGFIBERMEAS */

      /* Auto adjust the sync phase */
      if(tiSlaveFiberIn==1)
	vmeWrite32(&TIp->reset,TI_RESET_AUTOALIGN_HFBR1_SYNC);
      else
	vmeWrite32(&TIp->reset,TI_RESET_AUTOALIGN_HFBR5_SYNC);

      taskDelay(1);

      /* Get the fiber latency measurement result */
      if(tiSlaveFiberIn==1)
	fiberLatency = vmeRead32(&TIp->fiberLatencyMeasurement);
      else
	fiberLatency = vmeRead32(&TIp->fiberAlignment);

      /* Divide by two to get the one way trip */
      index = ((fiberLatency& TI_FIBERLATENCYMEASUREMENT_DATA_MASK)>>23)>>1;
      measurement[index]++;

      taskDelay(1);

    }

  /* Loop through measurements to find the most common */
  for(imeas = 0; imeas < 256; imeas++)
    {
      if(measurement[imeas] >= maxCount)
	{
	  maxCount = measurement[imeas];
	  maxIndex = imeas;
	}
/* #define DEBUGMEAS */
#ifdef DEBUGMEAS
      printf("%s: %2d: measurement = %d  %s\n",
	     __func__, imeas, measurement[imeas],
	     (imeas==maxIndex)?"***":"");
#endif
    }
  tiFiberLatencyMeasurement = maxIndex;

  syncDelay = (tiFiberLatencyOffset - tiFiberLatencyMeasurement);

  syncDelay_write = (syncDelay & 0xFF) << 8 |
    (syncDelay & 0xFF) << 16 | (syncDelay & 0xFF) << 24;

  vmeWrite32(&TIp->fiberSyncDelay,syncDelay_write);
  taskDelay(1);
  syncDelay = vmeRead32(&TIp->fiberSyncDelay);
  TIUNLOCK;

#ifdef DEBUGFIBERMEAS
  printf (" \n The fiber latency of 0xA0 is: 0x%08x, ", fiberLatency);
  printf (" The sync latency of 0x50 is: 0x%08x\n",syncDelay);
#endif /* DEBUGFIBERMEAS */

  if(failed == 1)
    {
      printf("\n");
      /* printf("%s: ERROR: TI Fiber Measurement failed!" */
      /* 	     "\n\tFirst Measurement != Second Measurement (%d != %d)\n\n", */
      /* 	     __FUNCTION__, */
      /* 	     firstMeas, tiFiberLatencyMeasurement); */
      tiFiberLatencyMeasurement = 0;
      rval = ERROR;
    }
  else if(!((tiFiberLatencyMeasurement > 0) && (tiFiberLatencyMeasurement <= 0xFF)))
    {
      printf("\n");
      printf("%s: ERROR: TI Fiber Measurement failed!"
	     "\n\tMeasurement out of bounds (%d)\n\n",
	     __FUNCTION__,
	     tiFiberLatencyMeasurement);
      tiFiberLatencyMeasurement = 0;
      rval = ERROR;
    }
  else
    {
      printf("%s: TI Fiber Measurement success!"
	     "  tiFiberLatencyMeasurement = 0x%x (%d)\n",
	     __FUNCTION__,
	     tiFiberLatencyMeasurement, tiFiberLatencyMeasurement);
      rval = OK;
    }

  free(measurement);
  return rval;
}

/**
 * @ingroup Status
 * @brief Return measured fiber length
 * @return Value of measured fiber length
 */
int
tiGetFiberLatencyMeasurement()
{
  return tiFiberLatencyMeasurement;
}

/**
 * @ingroup MasterConfig
 * @brief Enable/Disable operation of User SyncReset
 * @sa tiUserSyncReset
 * @param enable
 *   - >0: Enable
 *   - 0: Disable
 *
 * @return OK if successful, otherwise ERROR
 */
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

/**
 * @ingroup Status
 * @brief Return last SyncCommand received
 * @param
 *   - >0: print to standard out
 * @return Last SyncCommand received
 */
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

/**
 * @ingroup Status
 * @brief Get the status of the SyncCommand History Buffer
 *
 * @param pflag
 *   - >0: Print to standard out
 *
 * @return
 *   - 0: Empty
 *   - 1: Half Full
 *   - 2: Full
 */
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

/**
 * @ingroup Config
 * @brief Reset the SyncCommand history buffer
 */
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

/**
 * @ingroup Config
 * @brief Control level of the SyncReset signal
 * @sa tiSetUserSyncResetReceive
 * @param enable
 *   - >0: High
 *   -  0: Low
 * @param pflag
 *   - >0: Print status to standard out
 *   -  0: Supress status message
 */
void
tiUserSyncReset(int enable, int pflag)
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

  if(pflag)
    {
      printf("%s: User Sync Reset ",__FUNCTION__);
      if(enable)
	printf("HIGH\n");
      else
	printf("LOW\n");
    }

}

/**
 * @ingroup Status
 * @brief Print to standard out the history buffer of Sync Commands received.
 */
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

  printf("     syncHistory  OF   TStmp  Code  valid\n");

  while(code!=0)
    {
      TILOCK;
      syncHistory = vmeRead32(&TIp->syncHistory);
      TIUNLOCK;

      if(syncHistory == 0)
	break;

      if(tiMaster)
	{
	  code  = (syncHistory & TI_SYNCHISTORY_LOOPBACK_CODE_MASK)>>10;
	  valid = (syncHistory & TI_SYNCHISTORY_LOOPBACK_CODE_VALID)>>14;
	}
      else
	{
	  if(tiSlaveFiberIn == 1)
	    {
	      code  = syncHistory & TI_SYNCHISTORY_HFBR1_CODE_MASK;
	      valid = (syncHistory & TI_SYNCHISTORY_HFBR1_CODE_VALID)>>4;
	    }
	  else if(tiSlaveFiberIn == 5)
	    {
	      code  = (syncHistory & TI_SYNCHISTORY_HFBR5_CODE_MASK) >> 5;
	      valid = (syncHistory & TI_SYNCHISTORY_HFBR5_CODE_VALID)>>9;
	    }
	  else
	    {
	      printf("%s: Invalid slave fiber port %d\n",
		     __func__, tiSlaveFiberIn);
	      return;
	    }
	}

      overflow  = (syncHistory & TI_SYNCHISTORY_TIMESTAMP_OVERFLOW)>>15;
      timestamp = (syncHistory & TI_SYNCHISTORY_TIMESTAMP_MASK)>>16;

/*       if(valid) */
	{
	  printf("%4d: 0x%08x   %d   %5d   0x%x    %d\n",
		 count, syncHistory,
		 overflow, timestamp, code, valid);
	}
      count++;
      if(count>1024)
	{
	  printf("%s: More than expected in the Sync History Buffer... exitting\n",
		 __FUNCTION__);
	  break;
	}
    }
}


/**
 * @ingroup MasterConfig
 * @brief Set the value of the syncronization event interval
 *
 *
 * @param  blk_interval
 *      Sync Event will occur in the last event of the set blk_interval (number of blocks)
 *
 * @return OK if successful, otherwise ERROR
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

  if(tiUseTsRev2)
    {
      printf("%s: ERROR: Sync Event Interval is set by TS rev 2.\n",
	     __func__);
      return ERROR;
    }

  if(blk_interval>TI_SYNCEVENTCTRL_NBLOCKS_MASK)
    {
      printf("%s: WARN: Value for blk_interval (%d) too large.  Setting to %d\n",
	     __FUNCTION__,blk_interval,TI_SYNCEVENTCTRL_NBLOCKS_MASK);
      blk_interval = TI_SYNCEVENTCTRL_NBLOCKS_MASK;
    }

  TILOCK;
  vmeWrite32(&TIp->syncEventCtrl, blk_interval);
  TIUNLOCK;

  return OK;
}

/**
 * @ingroup MasterStatus
 * @brief Get the SyncEvent Block interval
 * @return Block interval of the SyncEvent
 */
int
tiGetSyncEventInterval()
{
  int rval=0;
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

  if(tiUseTsRev2)
    {
      printf("%s: ERROR: Invalid usage with TS rev 2.\n",
	     __func__);
      return ERROR;
    }

  TILOCK;
  rval = vmeRead32(&TIp->syncEventCtrl) & TI_SYNCEVENTCTRL_NBLOCKS_MASK;
  TIUNLOCK;

  return rval;
}

/**
 * @ingroup MasterReadout
 * @brief Force a sync event (type = 0).
 * @return OK if successful, otherwise ERROR
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

  if(tiUseTsRev2)
    {
      printf("%s: ERROR: Invalid usage with TS rev 2.\n",
	     __func__);
      return ERROR;
    }

  TILOCK;
  vmeWrite32(&TIp->reset, TI_RESET_FORCE_SYNCEVENT);
  TIUNLOCK;

  return OK;
}

/**
 * @ingroup Readout
 * @brief Sync Reset Request is sent to TI-Master or TS.
 *
 *    This option is available for multicrate systems when the
 *    synchronization is suspect.  It should be exercised only during
 *    "sync events" where the requested sync reset will immediately
 *    follow all ROCs concluding their readout.
 *
 * @return OK if successful, otherwise ERROR
 */
int
tiSyncResetRequest()
{
  if(TIp == NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  if(tiUseTsRev2)
    {
      printf("%s: ERROR: Invalid usage with TS rev 2.\n",
	     __func__);
      return ERROR;
    }

  TILOCK;
  tiDoSyncResetRequest=1;
  TIUNLOCK;

  return OK;
}

/**
 * @ingroup MasterReadout
 * @brief Determine if a TI has requested a Sync Reset
 *
 * @return 1 if requested received, 0 if not, otherwise ERROR
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

  if(tiUseTsRev2)
    {
      printf("%s: ERROR: Invalid usage with TS rev 2.\n",
	     __func__);
      return ERROR;
    }

  TILOCK;
  request = (vmeRead32(&TIp->blockBuffer) & TI_BLOCKBUFFER_SYNCRESET_REQUESTED)>>30;
  TIUNLOCK;

  return request;
}

/**
 * @ingroup MasterConfig
 * @brief Configure which ports (and self) to enable response of a SyncReset request.
 * @param portMask Mask of ports to enable (port 1 = bit 0)
 * @param self 1 to enable self, 0 to disable
 *
 * @return OK if successful, otherwise ERROR
 */
int
tiEnableSyncResetRequest(unsigned int portMask, int self)
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

  if(tiUseTsRev2)
    {
      printf("%s: ERROR: Invalid usage with TS rev 2.\n",
	     __func__);
      return ERROR;
    }

  if(portMask > 0xFF)
    {
      printf("%s: ERROR: Invalid portMask (0x%x)\n",
	     __FUNCTION__, portMask);
      return ERROR;
    }

  /* Mask sure self is binary */
  if(self)
    self = 1;
  else
    self = 0;

  TILOCK;
  vmeWrite32(&TIp->rocEnable,
	   (vmeRead32(&TIp->rocEnable) & TI_ROCENABLE_MASK) |
	   (portMask << 11) | (self << 10) );
  TIUNLOCK;

  return OK;
}

/**
 * @ingroup MasterStatus
 * @brief Status of SyncReset Request received bits.
 * @param pflag Print to standard out if not 0
 * @return Port mask of SyncReset Request received (port 1 = bit 0, TI-Master = bit 8), otherwise ERROR;
 */
int
tiSyncResetRequestStatus(int pflag)
{
  int self = 0, rval = 0, ibit = 0;
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

  if(tiUseTsRev2)
    {
      printf("%s: ERROR: Invalid usage with TS rev 2.\n",
	     __func__);
      return ERROR;
    }

  TILOCK;
  rval = (int)(vmeRead32(&TIp->rocEnable) & TI_ROCENABLE_SYNCRESET_REQUEST_MONITOR_MASK);
  TIUNLOCK;

  /* Reorganize the bits */
  if(rval)
    {
      self = (rval & 0x1);
      rval = rval >> 1;
      rval = rval | (self<<8);
    }

  if(pflag)
    {
      if(rval)
	{
	  printf("    ***** SyncReset Requested from ");

	  for(ibit = 0; ibit < 8; ibit++)
	    {
	      printf("%d ", ibit + 1);
	    }

	  if(rval & (1 << 8))
	    {
	      printf("SELF ");
	    }

	  printf("*****\n");
	}
      else
	{
	  printf("    No SyncReset Requested\n");
	}
    }

  return rval;
}


/**
 * @ingroup MasterConfig
 * @brief Reset the registers that record the triggers enabled status of TI Slaves.
 *
 */
void
tiTriggerReadyReset()
{
  unsigned int syncsource=0;
  if(TIp == NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return;
    }

  if(!tiMaster)
    {
      printf("%s: ERROR: TI is not the TI Master.\n",__FUNCTION__);
      return;
    }

  if(tiUseTsRev2)
    {
      printf("%s: ERROR: Invalid usage with TS rev 2.\n",
	     __func__);
      return;
    }

  TILOCK;
  /* Reset Receiver */
  vmeWrite32(&TIp->reset, TI_RESET_MGT_RX_RESET);
  taskDelay(1);

  /* Get the current SyncReset Source */
  syncsource = vmeRead32(&TIp->sync) & TI_SYNC_SOURCEMASK;

  /* Set Loopback as Source */
  vmeWrite32(&TIp->sync, TI_SYNC_LOOPBACK);

  /* Send the Trigger Source Enabled Reset */
  vmeWrite32(&TIp->syncCommand,TI_SYNCCOMMAND_TRIGGER_READY_RESET);

  /* Restore original SyncReset Source */
  vmeWrite32(&TIp->sync, syncsource);

  TIUNLOCK;


}

/**
 * @ingroup MasterConfig
 * @brief Reset the registers that record bit errors recorded on the trigger link.
 *
 */
void
tiTriggerLinkErrorReset()
{
  if(TIp == NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return;
    }

  if(!tiMaster)
    {
      printf("%s: ERROR: TI is not the TI Master.\n",__FUNCTION__);
      return;
    }

  if(tiUseTsRev2)
    {
      printf("%s: ERROR: Invalid usage with TS rev 2.\n",
	     __func__);
      return;
    }

  TILOCK;
  vmeWrite32(&TIp->syncCommand,
	     TI_SYNCCOMMAND_GTP_STATUSB_RESET);
  TIUNLOCK;
}

/**
 * @ingroup Status
 * @brief Get the error status bits for the trigger link
 *
 * @param pflag
 *  - !0: Print to standard out
 *
 * @return Trigger Link bits if successful, ERROR otherwise
 */
unsigned int
tiGetTriggerLinkStatus(int pflag)
{
  unsigned int rval = 0, bitflags = 0;
  int ibit = 0;

  if(TIp == NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  if(tiUseTsRev2)
    {
      printf("%s: ERROR: Invalid usage with TS rev 2.\n",
	     __func__);
      return ERROR;
    }

  TILOCK;
  rval = vmeRead32(&TIp->GTPStatusB);
  TIUNLOCK;

  if(pflag)
    {
      printf("STATUS for Trigger Links\n");

      printf("      Connected    RX Data Error      Disparity    NON 8b/10b Data\n");
      printf("     (12345678)      (12345678)      (12345678)      (12345678)\n");
      printf("--------------------------------------------------------------------------------\n");

      printf("      ");
      bitflags = rval & TI_GTPSTATUSB_CHANNEL_BONDING_MASK;
      for(ibit = 0; ibit < 8; ibit++)
	{
	  if( (1<<ibit) & bitflags )
	    printf("%d", ibit+1);
	  else
	    printf("-");
	}

      printf("        ");
      bitflags = (rval & TI_GTPSTATUSB_DATA_ERROR_MASK) >> 8;
      for(ibit = 0; ibit < 8; ibit++)
	{
	  if( (1<<ibit) & bitflags )
	    printf("%d", ibit+1);
	  else
	    printf("-");
	}

      printf("        ");
      bitflags = (rval & TI_GTPSTATUSB_DISPARITY_ERROR_MASK) >> 16;
      for(ibit = 0; ibit < 8; ibit++)
	{
	  if( (1<<ibit) & bitflags )
	    printf("%d", ibit+1);
	  else
	    printf("-");
	}
      printf("        ");

      bitflags = (rval & TI_GTPSTATUSB_DATA_NOT_IN_TABLE_ERROR_MASK) >> 24;
      for(ibit = 0; ibit < 8; ibit++)
	{
	  if( (1<<ibit) & bitflags )
	    printf("%d", ibit+1);
	  else
	    printf("-");
	}

      printf("\n");
    }

  return rval;
}

/**
 * @ingroup MasterReadout
 * @brief Generate non-physics triggers until the current block is filled.
 *    This feature is useful for "end of run" situations.
 *
 * @return OK if successful, otherwise ERROR
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

/**
 * @ingroup MasterConfig
 * @brief Reset the MGT
 * @return OK if successful, otherwise ERROR
 */
int
tiResetMGT()
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
  vmeWrite32(&TIp->reset, TI_RESET_MGT);
  TIUNLOCK;
  taskDelay(1);

  return OK;
}

/**
 * @ingroup Config
 * @brief Reset the MGT Rx CDR
 */
int
tiResetMGTRx()
{
  if(TIp == NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  vmeWrite32(&TIp->reset, TI_RESET_MGT_RX_RESET);
  TIUNLOCK;
  taskDelay(1);

  return OK;
}

/**
 * @ingroup Config
 * @brief Reset the Fiber Tranceivers
 */
int
tiResetFiber()
{
  if(TIp == NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  vmeWrite32(&TIp->reset, TI_RESET_FIBER);
  TIUNLOCK;
  taskDelay(1);

  return OK;
}

/**
 * @ingroup Config
 * @brief Set the input delay for the specified front panel TSinput (1-6)
 * @param chan Front Panel TSInput Channel (1-6)
 * @param delay Delay in units of 4ns (0=8ns)
 * @return OK if successful, otherwise ERROR
 */
int
tiSetTSInputDelay(int chan, int delay)
{
  if(TIp == NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  if((chan<1) || (chan>6))
    {
      printf("%s: ERROR: Invalid chan (%d)\n",__FUNCTION__,
	     chan);
      return ERROR;
    }

  if((delay<0) || (delay>0x1ff))
    {
      printf("%s: ERROR: Invalid delay (%d)\n",__FUNCTION__,
	     delay);
      return ERROR;
    }

  TILOCK;
  chan--;
  vmeWrite32(&TIp->fpDelay[chan/3],
	     (vmeRead32(&TIp->fpDelay[chan/3]) & ~TI_FPDELAY_MASK(chan))
	     | delay<<(10*(chan%3)));
  TIUNLOCK;

  return OK;
}

/**
 * @ingroup Status
 * @brief Get the input delay for the specified front panel TSinput (1-6)
 * @param chan Front Panel TSInput Channel (1-6)
 * @return Channel delay (units of 4ns) if successful, otherwise ERROR
 */
int
tiGetTSInputDelay(int chan)
{
  int rval=0;
  if(TIp == NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  if((chan<1) || (chan>6))
    {
      printf("%s: ERROR: Invalid chan (%d)\n",__FUNCTION__,
	     chan);
      return ERROR;
    }

  TILOCK;
  chan--;
  rval = (vmeRead32(&TIp->fpDelay[chan/3]) & TI_FPDELAY_MASK(chan))>>(10*(chan%3));
  TIUNLOCK;

  return rval;
}

/**
 * @ingroup Status
 * @brief Print Front Panel TSinput Delays to Standard Out
 * @return OK if successful, otherwise ERROR
 */
int
tiPrintTSInputDelay()
{
  unsigned int reg[11];
  int ireg=0, ichan=0, delay=0;
  if(TIp == NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  for(ireg=0; ireg<11; ireg++)
    reg[ireg] = vmeRead32(&TIp->fpDelay[ireg]);
  TIUNLOCK;

  printf("%s: Front panel delays:", __FUNCTION__);
  for(ichan=0;ichan<5;ichan++)
    {
      delay = reg[ichan/3] & TI_FPDELAY_MASK(ichan)>>(10*(ichan%3));
      if((ichan%4)==0)
	{
	  printf("\n");
	}
      printf("Chan %2d: %5d   ",ichan+1,delay);
    }
  printf("\n");

  return OK;
}

/**
 * @ingroup Status
 * @brief Return value of buffer length from GTP
 * @return value of buffer length from GTP
 */
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

/**
 * @ingroup MasterStatus
 * @brief Returns the mask of fiber channels that report a "connected"
 *     status from a TI.
 *
 * @return Fiber Connected Mask
 */
int
tiGetConnectedFiberMask()
{
  int rval=0;
  if(TIp==NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  if(!tiMaster)
    {
      printf("%s: ERROR: TI is not the TI Master.\n",__FUNCTION__);
      return ERROR;
    }

  if(tiUseTsRev2)
    {
      printf("%s: ERROR: Invalid usage with TS rev 2.\n",
	     __func__);
      return ERROR;
    }

  TILOCK;
  rval = (vmeRead32(&TIp->fiber) & TI_FIBER_CONNECTED_MASK)>>16;
  TIUNLOCK;

  return rval;
}

/**
 * @ingroup MasterStatus
 * @brief Returns the mask of fiber channels that report a "connected"
 *     status from a TI has it's trigger source enabled.
 *
 * @return Trigger Source Enabled Mask
 */
int
tiGetTrigSrcEnabledFiberMask()
{
  int rval=0;
  if(TIp==NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  if(!tiMaster)
    {
      printf("%s: ERROR: TI is not the TI Master.\n",__FUNCTION__);
      return ERROR;
    }

  if(tiUseTsRev2)
    {
      printf("%s: ERROR: Invalid usage with TS rev 2.\n",
	     __func__);
      return ERROR;
    }

  TILOCK;
  rval = (vmeRead32(&TIp->fiber) & TI_FIBER_TRIGSRC_ENABLED_MASK)>>24;
  TIUNLOCK;

  return rval;
}

/**
 * @ingroup Status
 * @brief Return the value from the SWa fast link register
 * @param reg  Register to request
 * @return Value at specified register
 */
unsigned int
tiGetSWAStatus(int reg)
{
  unsigned int rval=0;
  if(TIp==NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  if(reg>=128)
    {
      printf("%s: ERROR: SWA reg (0x%x) out of range.\n",
	     __FUNCTION__,reg);
      return ERROR;
    }

  TILOCK;
  rval = vmeRead32(&TIp->SWA_status[reg]);
  TIUNLOCK;

  return rval;
}

/**
 * @ingroup Status
 * @brief Return the value from the SWB fast link register
 * @param reg  Register to request
 * @return Value at specified register
 */
unsigned int
tiGetSWBStatus(int reg)
{
  unsigned int rval=0;
  if(TIp==NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  if(reg>=128)
    {
      printf("%s: ERROR: SWB reg (0x%x) out of range.\n",
	     __FUNCTION__,reg);
      return ERROR;
    }

  TILOCK;
  rval = vmeRead32(&TIp->SWB_status[reg]);
  TIUNLOCK;

  return rval;
}

/**
 * @ingroup Status
 * @brief Return geographic address as provided from a VME-64X crate.
 * @return Geographic Address if successful, otherwise ERROR.  0 would indicate that the TI is not in a VME-64X crate.
 */

int
tiGetGeoAddress()
{
  int rval=0;
  if(TIp==NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  rval = (vmeRead32(&TIp->adr24) & TI_ADR24_GEOADDR_MASK)>>10;
  TIUNLOCK;

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

/**
 * @ingroup IntPoll
 * @brief Connect a user routine to the TI Interrupt or
 *    latched trigger, if polling.
 *
 * @param vector VME Interrupt Vector
 * @param routine Routine to call if block is available
 * @param arg argument to pass to routine
 *
 * @return OK if successful, otherwise ERROR
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
    case TI_READOUT_TSREV2_POLL:
      break;

    case TI_READOUT_TS_INT:
    case TI_READOUT_EXT_INT:
    case TI_READOUT_TSREV2_INT:
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

/**
 * @ingroup IntPoll
 * @brief Disable interrupts or kill the polling service thread
 *
 *
 * @return OK if successful, otherwise ERROR
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
    case TI_READOUT_TSREV2_INT:

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
    case TI_READOUT_TSREV2_POLL:
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

/**
 * @ingroup IntPoll
 * @brief Connect a user routine to be executed instead of the default
 *  TI interrupt/trigger latching acknowledge prescription
 *
 * @param routine Routine to call
 * @param arg argument to pass to routine
 * @return OK if successful, otherwise ERROR
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

/**
 * @ingroup IntPoll
 * @brief Acknowledge an interrupt or latched trigger.  This "should" effectively
 *  release the "Busy" state of the TI.
 *
 *  Execute a user defined routine, if it is defined.  Otherwise, use
 *  a default prescription.
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

      tiNReadoutEvents = 0;
      TIUNLOCK;
    }

}

/**
 * @ingroup IntPoll
 * @brief Enable interrupts or latching triggers (depending on set TI mode)
 *
 * @param iflag if = 1, trigger counter will be reset
 *
 * @return OK if successful, otherwise ERROR
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
    case TI_READOUT_TSREV2_POLL:
#ifndef VXWORKS
      tiStartPollingThread();
#endif
      break;

    case TI_READOUT_TS_INT:
    case TI_READOUT_EXT_INT:
    case TI_READOUT_TSREV2_INT:
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

  if(tiUseEvTypeScalers)
    tiClearEvTypeScalers();

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

/**
 * @ingroup IntPoll
 * @brief Disable interrupts or latching triggers
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

/**
 * @ingroup Status
 * @brief Return current readout count
 */
unsigned int
tiGetIntCount()
{
  unsigned int rval=0;

  TILOCK;
  rval = tiIntCount;
  TIUNLOCK;

  return(rval);
}

/**
 * @ingroup Status
 * @brief Return current acknowledge count
 */
unsigned int
tiGetAckCount()
{
  unsigned int rval=0;

  TILOCK;
  rval = tiAckCount;
  TIUNLOCK;

  return(rval);
}



/**
 * @ingroup Status
 * @brief Return status of Busy from SWB
 * @param pflag
 *   - >0: Print to standard out
 * @return
 *   - 1: Busy
 *   - 0: Not Busy
 *   - -1: Error
 */
int
tiGetSWBBusy(int pflag)
{
  unsigned int rval=0;
  if(TIp == NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  rval = vmeRead32(&TIp->busy) & (TI_BUSY_SWB<<16);

  TIUNLOCK;

  if(pflag)
    {
      printf("%s: SWB %s\n",
	     __FUNCTION__,
	     (rval)?"BUSY":"NOT BUSY");
    }

  return rval;
}

/**
 * @ingroup Status
 * @brief Return BUSY counter for specified Busy Source
 * @param busysrc
 *  - 0: SWA
 *  - 1: SWB
 *  - 2: P2
 *  - 3: FP-FTDC
 *  - 4: FP-FADC
 *  - 5: FP
 *  - 6: Unused
 *  - 7: Loopack
 *  - 8-15: Fiber 1-8
 * @return
 *   - Busy counter for specified busy source
 */
unsigned int
tiGetBusyCounter(int busysrc)
{
  unsigned int rval=0;

  if(TIp == NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  if(busysrc<7)
    rval = vmeRead32(&TIp->busy_scaler1[busysrc]);
  else
    rval = vmeRead32(&TIp->busy_scaler2[busysrc-7]);
  TIUNLOCK;

  return rval;
}

/**
 * @ingroup Status
 * @brief Print the BUSY counters for all busy sources
 * @return
 *   - OK if successful, otherwise ERROR;
 */
int
tiPrintBusyCounters()
{
  unsigned int counter[16];
  const char *scounter[16] =
    {
      "SWA    ",
      "SWB    ",
      "P2     ",
      "FP-FTDC",
      "FP-FADC",
      "FP     ",
      "Unused ",
      "Loopack",
      "Fiber 1",
      "Fiber 2",
      "Fiber 3",
      "Fiber 4",
      "Fiber 5",
      "Fiber 6",
      "Fiber 7",
      "Fiber 8"
    };
  int icnt=0;

  if(TIp == NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  for(icnt=0; icnt<16; icnt++)
    {
      if(icnt<7)
	counter[icnt] = vmeRead32(&TIp->busy_scaler1[icnt]);
      else
	counter[icnt] = vmeRead32(&TIp->busy_scaler2[icnt-7]);
    }
  TIUNLOCK;

  printf("\n\n");
  printf(" Busy Counters \n");
  printf("--------------------------------------------------------------------------------\n");
  for(icnt=0; icnt<16; icnt++)
    {
      printf("%s   0x%08x (%10d)\n",
	     scounter[icnt], counter[icnt], counter[icnt]);
    }
  printf("--------------------------------------------------------------------------------\n");
  printf("\n\n");

  return OK;
}


/**
 * @ingroup Status
 * @brief Read the fiber fifo from the TI
 *
 * @param   fiber - Fiber fifo to read. 1 and 5 only supported.
 * @param   data  - local memory address to place data
 * @param  maxwords - Maximum number of 32bit words to put into data array.
 *
 * @return Number of words transferred to data if successful, ERROR otherwise
 *
 */
int
tiReadFiberFifo(int fiber, volatile unsigned int *data, int maxwords)
{
  int nwords = 0;
  unsigned int word = 0;

  if(data==NULL)
    {
      printf("%s: ERROR: Invalid Destination address\n",
	     __func__);
      return(ERROR);
    }

  if((fiber != 1) && (fiber !=5))
    {
      printf("%s: Invalid fiber (%d)\n",
	     __func__, fiber);
      return ERROR;
    }

  TILOCK;
  while(nwords < maxwords)
    {
      if(fiber == 1)
	word = vmeRead32(&TIp->trigTable[12]);
      else
      	word = vmeRead32(&TIp->trigTable[13]);

      if(word & (1<<31))
	break;

      data[nwords++] = word;
    }
  TIUNLOCK;

  return nwords;
}


/**
 * @ingroup Status
 * @brief Read the fiber fifo from the TI and print to standard out.
 *
 * @param   fiber - Fiber fifo to read. 1 and 5 only supported.
 *
 * @return OK if successful, ERROR otherwise
 *
 */
int
tiPrintFiberFifo(int fiber)
{
  volatile unsigned int *data;
  int maxwords = 256, iword, rwords = 0;

  if((fiber != 1) && (fiber !=5))
    {
      printf("%s: Invalid fiber (%d)\n",
	     __func__, fiber);
      return ERROR;
    }

  data = (volatile unsigned int *)malloc(maxwords * sizeof(unsigned int));
  if(!data)
    {
      printf("%s: Unable to acquire memory\n",
	     __func__);
      return ERROR;
    }

  rwords = tiReadFiberFifo(fiber, data, maxwords);

  if(rwords == 0)
    {
      printf("%s: No data in fifo\n\n",
	     __func__);
      return OK;
    }
  else if(rwords == ERROR)
    {
      printf("%s: tiReadFiberFifo(..) returned ERROR\n",
	     __func__);
      return ERROR;
    }

  printf(" Fiber %d fifo (%d words)\n",
	 fiber, rwords);
  printf("      Timestamp     Data\n");
  printf("----------------------------\n");
  for(iword = 0; iword < rwords; iword++)
    {
      printf("%3d:    0x%04x     0x%04x\n",
	     iword,
	     (data[iword] & 0xFFFF0000)>>16,
	     (data[iword] & 0xFFFF));
    }
  printf("----------------------------\n");
  printf("\n");

  if(data)
    free((void *)data);

  return OK;
}

/**
 * @ingroup Config
 * @brief Turn on Token out test mode
 * @sa tiSetTokenOutTest
 * @return OK if successful, otherwise ERROR
 */
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

/**
 * @ingroup Config
 * @brief Set the level of the token out signal
 * @param level
 *   - >0: High
 *   - 0: Low
 * @sa tiSetTokenTestMode
 * @return OK if successful, otherwise ERROR
 */
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

/* Module TI Routines */
int
tiRocEnable(int roc)
{
  if(TIp == NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  if((roc<1) || (roc>8))
    {
      printf("%s: ERROR: Invalid roc (%d)\n",
	     __FUNCTION__,roc);
      return ERROR;
    }


  if(roc == 2) /* ROC acknowledge from SWA / VTP */
    {
      tiSetBusySource(TI_BUSY_SWA, 0);
    }

  TILOCK;
  vmeWrite32(&TIp->rocEnable, (vmeRead32(&TIp->rocEnable) & TI_ROCENABLE_MASK) |
	     TI_ROCENABLE_ROC(roc-1));
  TIUNLOCK;

  return OK;
}

int
tiRocEnableMask(int rocmask)
{
  if(TIp == NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  if(rocmask>TI_ROCENABLE_MASK)
    {
      printf("%s: ERROR: Invalid rocmask (0x%x)\n",
	     __FUNCTION__,rocmask);
      return ERROR;
    }

  if(rocmask & (1 << 1) ) /* ROC acknowledge from SWA / VTP */
    {
      tiSetBusySource(TI_BUSY_SWA, 0);
    }

  TILOCK;
  vmeWrite32(&TIp->rocEnable, rocmask);
  TIUNLOCK;

  return OK;
}

int
tiGetRocEnableMask()
{
  int rval=0;
  if(TIp == NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  rval = vmeRead32(&TIp->rocEnable) & TI_ROCENABLE_MASK;
  TIUNLOCK;

  return rval;
}

/**
 * @ingroup Status
 * @brief Read all the scalers into an array
 *
 * @param data  - local memory address to place scaler values
 *       element   value
 *         0     Live time
 *	   1     Busy time
 *	   2     TS input #1
 *	   3     TS input #2
 *	   4     TS input #3
 *	   5     TS input #4
 *	   6     TS input #5
 *	   7     TS input #6
 *	   8     All trigger sources, before busy
 *	   9     Top 16 bits of 48bit event number
 *	  10     Lower 32 bits of 48bit event number
 *	  11     Only TS inputs, before busy
 *
 * @param latch:
 *   -  0: Do not latch before readout
 *   -  1: Latch before readout
 *   -  2: Latch and reset before readout
 *
 *
 * @return Number of scaler cahnnels
 *
 *  If data is NULL, routine will return number of words that would have
 *   been transferred
 *
 */

int
tiReadScalers(volatile unsigned int *data, int latch)
{
  unsigned int rval = 12;
  int i;

  if(TIp == NULL)
    {
      logMsg("tiReadScalers: ERROR: TI not initialized\n",
	     1, 2, 3, 4, 5, 6);
      return ERROR;
    }

  if(data==NULL)
    {
      return rval;		/* Return # of scalers */
    }

  if((latch<0) || (latch>2))
    {
      logMsg("tiReadScalers: ERROR: Invalid latch (%d).\n",
	     latch,2,3,4,5,6);
      return ERROR;
    }

  TILOCK;

  switch(latch)
    {
    case 1:
      vmeWrite32(&TIp->reset,TI_RESET_SCALERS_LATCH);
      break;

    case 2:
      vmeWrite32(&TIp->reset,TI_RESET_SCALERS_LATCH | TI_RESET_SCALERS_RESET);
      break;
    }

  data[0] = vmeRead32(&TIp->livetime);
  data[1] = vmeRead32(&TIp->busytime);

  for(i=0;i<6;i++)
    {
      data[2+i] = vmeRead32(&TIp->ts_scaler[i]);
    }

  data[8] = vmeRead32(&TIp->inputCounter); /* All trigger sources */
  data[9] = (vmeRead32(&TIp->eventNumber_hi) >> 16) & 0xffff;
                                       /* Top 16 bits of event number */
  data[10] = vmeRead32(&TIp->eventNumber_lo); /* Bottom 32 bits of event num */
  data[11] = vmeRead32(&TIp->blank5[0]); /* Only TS inputs */

  TIUNLOCK;

  return rval;
}

/**
 * @ingroup Config
 * @brief Set control over the TS inputs scalers.
 *
 *  Provides a means for having counters disabled when trigger sources
 *  are disabled and disabled when the specific input is disabled.
 *
 * @param mode:
 *   -  0: Always count, regardless of trigger source enable
 *   -  1: Only count when trigger source is enabled.
 *
 * @param control:
 *   -  0: TS inputs scalers count according to 'mode' parameter.
 *   -  1: TS inputs scalers can be enabled/disabled with @tiEnableTSInput/@tiDisableTSInput
 *
 * @return OK if successful, otherwise ERROR
 *
 */

int
tiSetScalerMode(int mode, int control)
{
  unsigned int reg = 0;
  if(TIp == NULL)
    {
      printf("%s: ERROR: TI not initialized\n",
	     __FUNCTION__);
      return ERROR;
    }

  if((mode < 0) || (mode > 1))
    {
      printf("%s: Invalid mode (%d)\n", __FUNCTION__, mode);
      return ERROR;
    }

  if((control < 0) || (control > 1))
    {
      printf("%s: Invalid control (%d).\n",
	     __FUNCTION__,
	     control);
      return ERROR;
    }

  TILOCK;
  reg = vmeRead32(&TIp->vmeControl);
  vmeWrite32(&TIp->vmeControl,
	     (reg & ~(TI_VMECONTROL_COUNT_IN_GO_ENABLE |
		      TI_VMECONTROL_TS_COUNTER_CONTROL)) |
	     (mode << 27) | (control << 28) );
  TIUNLOCK;

  return OK;
}

/**
 * @ingroup Config
 * @brief Enable/disable recording of scalers associated with the bits in the event type.
 *
 *   Provides the means for counting the event type bits, for each event
 *   obtained through @tiReadBlock().
 *
 * @param enable:
 *   -  0: Scalers Disabled
 *   -  1: Scalers Enabled, event type decoded in @tiReadBLock()
 *
 * @return OK if successful, otherwise ERROR
 *
 */
int
tiSetEvTypeScalers(int enable)
{
  if((enable < 0) || (enable > 1))
    {
      printf("%s: ERROR: Invalid enable (%d)\n",
	     __FUNCTION__, enable);
      return ERROR;
    }

  TILOCK;
  tiUseEvTypeScalers = enable;
  TIUNLOCK;

  return OK;
}

static unsigned int evtype_scalers[6];
static unsigned int evtype_overflow;
static int nevtype_calls;

/**
 * @ingroup Config
 * @brief Clear the event type scalers.
 *
 * @return OK if successful, otherwise ERROR
 */
void
tiClearEvTypeScalers()
{
  int ibit, nbit=6;

  for(ibit = 0; ibit < nbit; ibit++)
    {
      evtype_scalers[ibit] = 0;
    }
  evtype_overflow = 0;
  nevtype_calls = 0;

}

static void
tiFillEvTypeScalers(unsigned int evtype)
{
  int ibit, nbit=6;

  nevtype_calls++;

  /* Count internal pulser event types (defaults, anyway) as overflow */
  if((evtype == 0xFD) || (evtype == 0xFE))
    {
      evtype_overflow++;
      return;
    }

  for(ibit = 0; ibit < nbit; ibit++)
    {
      if(evtype & (1 << ibit))
	evtype_scalers[ibit]++;
    }

  if(evtype & 0xFFFFFFC0)
    evtype_overflow++;
}

/**
 * @ingroup Status
 * @brief Returns an array providing the current values of the event type scalers.
 *
 * @param data - local memory location where to store the scaler values
 * @param maxwords - maximum amount of words to store at 'data'
 *
 * @return OK if successful, otherwise ERROR
 */
int
tiGetEvTypeScalers(unsigned int *data, int maxwords)
{
  int dCnt = 0;
  int iscaler;

  for(iscaler = 0; iscaler < 6; iscaler++)
    {
      data[dCnt++] = evtype_scalers[iscaler];
    }
  data[dCnt++] = evtype_overflow;
  data[dCnt++] = nevtype_calls;

  return dCnt;
}

/**
 * @ingroup Readout
 * @brief Scan the provided TI data array and fill the event type scalers.
 *
 * @param data - local memory location where to find TI data
 * @param nwords - number of words to scan from 'data'
 *
 * @return Number of event types found if successful, otherwise ERROR
 */
int
tiScanAndFillEvTypeScalers(volatile unsigned int *data, int nwords)
{
  int rval;
  unsigned int evtypes[256];
  int ievtype, nevtypes = 0;

  nevtypes = tiDecodeTriggerTypes(data, nwords, 256, (unsigned int *)&evtypes);

  if(nevtypes == ERROR)
    {
      logMsg("tiScanAndFillEvTypeScalers: ERROR: Failed to fill event type scalers\n",
	     0, 1, 2, 3, 4, 5);
      rval = ERROR;
    }
  else
    {
      for(ievtype = 0; ievtype < nevtypes; ievtype++)
	tiFillEvTypeScalers(evtypes[ievtype]);

      rval = nevtypes;
    }

  return rval;
}

/**
 * @ingroup Status
 * @brief Print, to standard out, the current values of the event type scalers
 *
 */
void
tiPrintEvTypeScalers()
{
  int isca, nsca = 6;

 printf("Event Type Scalers\n");
 printf("--------------------------------------------------------------------------------\n");

 printf("TS Input:    Counts\n");

 for(isca = 0; isca < nsca; isca++)
   {
     printf("      %2d:  %8d\n",
	    isca + 1, evtype_scalers[isca]);
   }

 printf("\n");
 printf("Overflow: %8d\n",
	evtype_overflow);
 printf("Events  : %8d\n",
	nevtype_calls);

}

void
tiUnload(int pflag)
{
  TILOCK;
  TIp = NULL;
  TIUNLOCK;

  if(pflag)
    printf("%s: INFO: TI pointer set to NULL\n",__func__);
}

/**
 * @ingroup Status
 *
 * @brief Check TI Ready for specified number of times after iodelay reset.
 *
 * @param nwait Number of times to poll register for ready state
 */
int
tiWaitForIODelayReset(int nwait)
{
  int iwait = 0, ready = 0;

  if(TIp == NULL)
    {
      printf("%s: ERROR: TI not initialized\n",
	     __func__);
      return ERROR;
    }

  if(nwait <= 0)
    nwait = 100;

  TILOCK;
  while((!ready) && (iwait++ < nwait))
    {
      ready = vmeRead32(&TIp->GTPtriggerBufferLength) & TI_GTPTRIGGERBUFFERLENGTH_IODELAY_READY;
    }
  TIUNLOCK;

  if(!ready)
    {
      printf("%s: ERROR: TI Not ready after nwait (%d)\n",
	     __func__, nwait);
      return ERROR;
    }

  return OK;
}
