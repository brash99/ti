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
#ifndef TILIB_H
#define TILIB_H

#include <sdLib.h>
#include <ctpLib.h>
#ifndef VXWORKS
#include <gtpLib.h>
#endif

#ifndef VXWORKS
#include <pthread.h>

pthread_mutex_t tiISR_mutex=PTHREAD_MUTEX_INITIALIZER;
#else
/* #include <intLib.h> */
extern int intLock();
extern int intUnlock();
#endif

#ifdef VXWORKS
int intLockKeya;
#define INTLOCK {				\
    intLockKeya = intLock();			\
}

#define INTUNLOCK {				\
    intUnlock(intLockKeya);			\
}
#else
#define INTLOCK {				\
    vmeBusLock();				\
}
#define INTUNLOCK {				\
    vmeBusUnlock();				\
}
#endif

struct TI_A24RegStruct
{
  /* 0x00000 */ volatile unsigned int boardID;
  /* 0x00004 */ volatile unsigned int fiber;
  /* 0x00008 */ volatile unsigned int intsetup;
  /* 0x0000C */ volatile unsigned int trigDelay;
  /* 0x00010 */ volatile unsigned int adr32;
  /* 0x00014 */ volatile unsigned int blocklevel;
  /* 0x00018 */ volatile unsigned int dataFormat;
  /* 0x0001C */ volatile unsigned int vmeControl;
  /* 0x00020 */ volatile unsigned int trigsrc;
  /* 0x00024 */ volatile unsigned int sync;
  /* 0x00028 */ volatile unsigned int busy;
  /* 0x0002C */ volatile unsigned int clock;
  /* 0x00030 */ volatile unsigned int trig1Prescale;
  /* 0x00034 */ volatile unsigned int blockBuffer;
  /* 0x00038 */ volatile unsigned int triggerRule;
  /* 0x0003C */ volatile unsigned int triggerWindow;
  /* 0x00040 */          unsigned int blank0[(0x48-0x40)/4];
#ifdef TSONLY
  /* 0x00040 */ volatile unsigned int GTPtrigger;
  /* 0x00044 */ volatile unsigned int fpInput;
#endif
  /* 0x00048 */ volatile unsigned int tsInput;
  /* 0x0004C */ volatile unsigned int output;
  /* 0x00050 */ volatile unsigned int fiberSyncDelay;
#ifdef TSONLY
  /* 0x00054 */ volatile unsigned int GTPprescale[4];
  /* 0x00064 */ volatile unsigned int fpInputPrescale[4];
  /* 0x00074 */ volatile unsigned int genInputPrescale;
#endif
  /* 0x00054 */          unsigned int blank_prescale[(0x78-0x54)/4];
  /* 0x00078 */ volatile unsigned int syncCommand;
  /* 0x0007C */ volatile unsigned int syncDelay;
  /* 0x00080 */ volatile unsigned int syncWidth;
  /* 0x00084 */ volatile unsigned int triggerCommand;
  /* 0x00088 */ volatile unsigned int randomPulser;
  /* 0x0008C */ volatile unsigned int fixedPulser1;
  /* 0x00090 */ volatile unsigned int fixedPulser2;
  /* 0x00094 */ volatile unsigned int nblocks;
  /* 0x00098 */ volatile unsigned int syncHistory;
  /* 0x0009C */ volatile unsigned int runningMode;
  /* 0x000A0 */ volatile unsigned int fiberLatencyMeasurement;
  /* 0x000A4 */ volatile unsigned int fiberAlignment;
  /* 0x000A8 */ volatile unsigned int livetime;
  /* 0x000AC */ volatile unsigned int busytime;
  /* 0x000B0 */ volatile unsigned int GTPStatusA;
  /* 0x000B4 */ volatile unsigned int GTPStatusB;
  /* 0x000B8 */ volatile unsigned int GTPtriggerBufferLength;
  /* 0x000BC */ volatile unsigned int inputCounter;
  /* 0x000C0 */ volatile unsigned int blockStatus[4];
  /* 0x000D0 */ volatile unsigned int adr24;
  /* 0x000D4 */ volatile unsigned int syncEventCtrl;
  /* 0x000D8 */          unsigned int blank2[(0xFC-0xD8)/4];
  /* 0x000FC */ volatile unsigned int scalerCtrl;
  /* 0x00100 */ volatile unsigned int reset;
  /* 0x00104 */          unsigned int blank3[(0x8C0-0x104)/4];
  /* 0x008C0 */ volatile unsigned int trigTable[(0x900-0x8C0)/4];
  /* 0x00900 */          unsigned int blank4[(0xFFFC-0x900)/4];
  /* 0x0FFFC */ volatile unsigned int eJTAGLoad;
  /* 0x10000 */ volatile unsigned int JTAGPROMBase[(0x20000-0x10000)/4];
  /* 0x20000 */ volatile unsigned int JTAGFPGABase[(0x30000-0x20000)/4];
  /* 0x30000 */ volatile unsigned int SWA[(0x40000-0x30000)/4];
  /* 0x40000 */ volatile unsigned int SWB[(0x50000-0x40000)/4];


};

/* Define TI Modes of operation:     Ext trigger - Interrupt mode   0
                                     TS  trigger - Interrupt mode   1
                                     Ext trigger - polling  mode    2 
                                     TS  trigger - polling  mode    3  */
#define TI_READOUT_EXT_INT    0
#define TI_READOUT_TS_INT     1
#define TI_READOUT_EXT_POLL   2
#define TI_READOUT_TS_POLL    3

/* Supported firmware version */
#define TI_SUPPORTED_FIRMWARE 0x102

/* boardID bits and masks */
#define TI_BOARDID_TYPE_TIDS         0x71D5
#define TI_BOARDID_TYPE_TI           0x7100
#define TI_BOARDID_TYPE_TS           0x7500
#define TI_BOARDID_TYPE_TD           0x7D00
#define TI_BOARDID_TYPE_MASK     0xFF000000
#define TI_BOARDID_PROD_MASK     0x00FF0000
#define TI_BOARDID_GEOADR_MASK   0x00001F00
#define TI_BOARDID_CRATEID_MASK  0x000000FF

/* fiber bits and masks */
#define TI_FIBER_1 (1<<0)
#define TI_FIBER_2 (1<<1)
#define TI_FIBER_3 (1<<2)
#define TI_FIBER_4 (1<<3)
#define TI_FIBER_5 (1<<4)
#define TI_FIBER_6 (1<<5)
#define TI_FIBER_7 (1<<6)
#define TI_FIBER_8 (1<<7)

/* intsetup bits and masks */
#define TI_INTSETUP_VECTOR_MASK   0x000000FF
#define TI_INTSETUP_LEVEL_MASK    0x00000F00
#define TI_INTSETUP_ENABLE        (1<<16)

/* trigDelay bits and masks */
#define TI_TRIGDELAY_TRIG1_DELAY_MASK 0x000000FF
#define TI_TRIGDELAY_TRIG1_WIDTH_MASK 0x0000FF00
#define TI_TRIGDELAY_TRIG2_DELAY_MASK 0x00FF0000
#define TI_TRIGDELAY_TRIG2_WIDTH_MASK 0xFF000000

/* adr32 bits and masks */
#define TI_ADR32_MBLK_ADDR_MAX_MASK  0x000003FE
#define TI_ADR32_MBLK_ADDR_MIN_MASK  0x003FC000
#define TI_ADR32_BASE_MASK       0xFF800000

/* blocklevel bits and masks */
#define TI_BLOCKLEVEL_MASK           0x000000FF

/* dataFormat bits and masks */
#define TI_DATAFORMAT_TWOBLOCK_PLACEHOLDER (1<<0)
#define TI_DATAFORMAT_TIMING_WORD          (1<<1)
#define TI_DATAFORMAT_STATUS_WORD          (1<<2)

/* vmeControl bits and masks */
#define TI_VMECONTROL_BERR           (1<<0)
#define TI_VMECONTROL_TOKEN_TESTMODE (1<<1)
#define TI_VMECONTROL_MBLK           (1<<2)
#define TI_VMECONTROL_A32M           (1<<3)
#define TI_VMECONTROL_A32            (1<<4)
#define TI_VMECONTROL_ERROR_INT      (1<<7)
#define TI_VMECONTROL_I2CDEV_HACK    (1<<8)
#define TI_VMECONTROL_TOKENOUT_HI    (1<<9)
#define TI_VMECONTROL_FIRST_BOARD    (1<<10)
#define TI_VMECONTROL_LAST_BOARD     (1<<11)
#define TI_VMECONTROL_BUFFER_DISABLE (1<<15)

/* trigsrc bits and masks */
#define TI_TRIGSRC_SOURCEMASK       0x0000F3FF
#define TI_TRIGSRC_P0               (1<<0)
#define TI_TRIGSRC_HFBR1            (1<<1)
#define TI_TRIGSRC_LOOPBACK         (1<<2)
#define TI_TRIGSRC_FPTRG            (1<<3)
#define TI_TRIGSRC_VME              (1<<4)
#define TI_TRIGSRC_TSINPUTS         (1<<5)
#define TI_TRIGSRC_TSREV2           (1<<6)
#define TI_TRIGSRC_PULSER           (1<<7)
#define TI_TRIGSRC_ENABLE           (1<<8)
#define TI_TRIGSRC_P2BUSY           (1<<9)
#define TI_TRIGSRC_PART_1           (1<<12)
#define TI_TRIGSRC_PART_2           (1<<13)
#define TI_TRIGSRC_PART_3           (1<<14)
#define TI_TRIGSRC_PART_4           (1<<15)
#define TI_TRIGSRC_MONITOR_MASK     0xFFFF0000

/* sync bits and masks */
#define TI_SYNC_SOURCEMASK              0x000000FF
#define TI_SYNC_P0                      (1<<0)
#define TI_SYNC_HFBR1                   (1<<1)
#define TI_SYNC_HFBR5                   (1<<2)
#define TI_SYNC_FP                      (1<<3)
#define TI_SYNC_LOOPBACK                (1<<4)
#define TI_SYNC_USER_SYNCRESET_ENABLED  (1<<7)
#define TI_SYNC_HFBR1_CODE_MASK         0x00000F00
#define TI_SYNC_HFBR5_CODE_MASK         0x0000F000
#define TI_SYNC_LOOPBACK_CODE_MASK      0x000F0000
#define TI_SYNC_HISTORY_FIFO_MASK       0x00700000
#define TI_SYNC_HISTORY_FIFO_EMPTY      (1<<20)
#define TI_SYNC_HISTORY_FIFO_HALF_FULL  (1<<21)
#define TI_SYNC_HISTORY_FIFO_FULL       (1<<22)
#define TI_SYNC_MONITOR_MASK            0xFF000000

/* busy bits and masks */
#define TI_BUSY_SOURCEMASK      0x0000FFFF
#define TI_BUSY_SWA              (1<<0)
#define TI_BUSY_SWB              (1<<1)
#define TI_BUSY_P2               (1<<2)
#define TI_BUSY_FP_FTDC          (1<<3)
#define TI_BUSY_FP_FADC          (1<<4)
#define TI_BUSY_FP               (1<<5)
#define TI_BUSY_P2_TRIGGER_INPUT (1<<6)
#define TI_BUSY_LOOPBACK         (1<<7)
#define TI_BUSY_HFBR1            (1<<8)
#define TI_BUSY_HFBR2            (1<<9)
#define TI_BUSY_HFBR3            (1<<10)
#define TI_BUSY_HFBR4            (1<<11)
#define TI_BUSY_HFBR5            (1<<12)
#define TI_BUSY_HFBR6            (1<<13)
#define TI_BUSY_HFBR7            (1<<14)
#define TI_BUSY_HFBR8            (1<<15)
#define TI_BUSY_MONITOR_MASK     0xFFFF0000

/* clock bits and mask  */
#define TI_CLOCK_INTERNAL    (0)
#define TI_CLOCK_HFBR5       (1)
#define TI_CLOCK_HFBR1       (2)
#define TI_CLOCK_FP          (3)
#define TI_CLOCK_MASK        0x0000000F

/* trig1Prescale bits and masks */
#define TI_TRIG1PRESCALE_MASK          0x0000FFFF

/* blockBuffer bits and masks */
#define TI_BLOCKBUFFER_BUFFERLEVEL_MASK      0x000000FF
#define TI_BLOCKBUFFER_BLOCKS_READY_MASK     0x0000FF00
#define TI_BLOCKBUFFER_TRIGGERS_IN_BLOCK     0x00FF0000
#define TI_BLOCKBUFFER_BLOCKS_NEEDACK_MASK   0x7F000000
#define TI_BLOCKBUFFER_SYNCEVENT             (1<<31)

/* triggerRule bits and masks */
#define TI_TRIGGERRULE_RULE1_MASK 0x000000FF
#define TI_TRIGGERRULE_RULE2_MASK 0x0000FF00
#define TI_TRIGGERRULE_RULE3_MASK 0x00FF0000
#define TI_TRIGGERRULE_RULE4_MASK 0xFF000000

/* triggerWindow bits and masks */
#define TI_TRIGGERWINDOW_COINC_MASK 0x0000FFFF

/* tsInput bits and masks */
#define TI_TSINPUT_MASK      0x0000003F
#define TI_TSINPUT_1         (1<<0)
#define TI_TSINPUT_2         (1<<1)
#define TI_TSINPUT_3         (1<<2)
#define TI_TSINPUT_4         (1<<3)
#define TI_TSINPUT_5         (1<<4)
#define TI_TSINPUT_6         (1<<5)
#define TI_TSINPUT_ALL       (0x3F)


/* output bits and masks */
#define TI_OUTPUT_MASK                 0x0000FFFF
#define TI_OUTPUT_BLOCKS_READY_MASK    0x00FF0000
#define TI_OUTPUT_EVENTS_IN_BLOCK_MASK 0xFF000000

/* fiberSyncDelay bits and masks */
#define TI_FIBERSYNCDELAY_HFBR1_SYNCPHASE_MASK    0x000000FF
#define TI_FIBERSYNCDELAY_HFBR1_SYNCDELAY_MASK    0x0000FF00
#define TI_FIBERSYNCDELAY_LOOPBACK_SYNCDELAY_MASK 0x00FF0000
#define TI_FIBERSYNCDELAY_HFBR5_SYNCDELAY_MASK    0xFF000000

/* syncCommand bits and masks */
#define TI_SYNCCOMMAND_VME_CLOCKRESET      0x11
#define TI_SYNCCOMMAND_CLK250_RESYNC       0x22
#define TI_SYNCCOMMAND_AD9510_RESYNC       0x33
#define TI_SYNCCOMMAND_GTP_STATUSB_RESET   0x44
#define TI_SYNCCOMMAND_TRIGGERLINK_ENABLE  0x55
#define TI_SYNCCOMMAND_TRIGGERLINK_DISABLE 0x77
#define TI_SYNCCOMMAND_SYNCRESET_HIGH      0x99
#define TI_SYNCCOMMAND_SYNCRESET_LOW       0xCC
#define TI_SYNCCOMMAND_SYNCRESET           0xDD
#define TI_SYNCCOMMAND_SYNCCODE_MASK       0x000000FF

/* syncDelay bits and masks */
#define TI_SYNCDELAY_MASK              0x0000007F

/* syncWidth bits and masks */
#define TI_SYNCWIDTH_MASK              0x7F
#define TI_SYNCWIDTH_LONGWIDTH_ENABLE  (1<<7)

/* triggerCommand bits and masks */
#define TI_TRIGGERCOMMAND_CODE_MASK    0x00000FFF

/* randomPulser bits and masks */
#define TI_RANDOMPULSER_TRIG1_RATE_MASK 0x00000007
#define TI_RANDOMPULSER_TRIG1_ENABLE    (1<<3)
#define TI_RANDOMPULSER_TRIG2_RATE_MASK 0x00000700
#define TI_RANDOMPULSER_TRIG2_ENABLE    (1<<11)

/* fixedPulser1 bits and masks */
#define TI_FIXEDPULSER1_NTRIGGERS_MASK 0x0000FFFF
#define TI_FIXEDPULSER1_PERIOD_MASK    0x7FFF0000
#define TI_FIXEDPULSER1_PERIOD_RANGE   (1<<31)

/* fixedPulser2 bits and masks */
#define TI_FIXEDPULSER2_NTRIGGERS_MASK 0x0000FFFF
#define TI_FIXEDPULSER2_PERIOD_MASK    0x7FFF0000
#define TI_FIXEDPULSER2_PERIOD_RANGE   (1<<31)

/* nblocks bits and masks */
#define TI_NBLOCKS_BLOCK_COUNT_MASK    0x00FFFFFF

/* syncHistory bits and masks */
#define TI_SYNCHISTORY_HFBR1_CODE_MASK     0x0000000F
#define TI_SYNCHISTORY_HFBR1_CODE_VALID    (1<<4)
#define TI_SYNCHISTORY_HFBR5_CODE_MASK     0x000001E0
#define TI_SYNCHISTORY_HFBR5_CODE_VALID    (1<<9)
#define TI_SYNCHISTORY_LOOPBACK_CODE_MASK  0x00003C00
#define TI_SYNCHISTORY_LOOPBACK_CODE_VALID (1<<14)
#define TI_SYNCHISTORY_TIMESTAMP_OVERFLOW  (1<<15)
#define TI_SYNCHISTORY_TIMESTAMP_MASK      0xFFFF0000

/* runningMode settings */
#define TI_RUNNINGMODE_ENABLE          0x71
#define TI_RUNNINGMODE_DISABLE         0x0

/* fiberLatencyMeasurement bits and masks */
#define TI_FIBERLATENCYMEASUREMENT_CARRYCHAIN_MASK 0x0000FFFF
#define TI_FIBERLATENCYMEASUREMENT_IODELAY_MASK    0x00FF0000
#define TI_FIBERLATENCYMEASUREMENT_DATA_MASK       0xFF000000

/* fiberAlignment bits and masks */
#define TI_FIBERALIGNMENT_HFBR1_IODELAY_MASK   0x000000FF
#define TI_FIBERALIGNMENT_HFBR1_SYNCDELAY_MASK 0x0000FF00
#define TI_FIBERALIGNMENT_HFBR5_IODELAY_MASK   0x00FF0000
#define TI_FIBERALIGNMENT_HFBR5_SYNCDELAY_MASK 0xFF000000

#ifdef TSONLY
/* GTPStatusA bits and masks */
#define TI_GTPSTATUSA_RESET_DONE_MASK 0x000000FF
#define TI_GTPSTATUSA_PLL_LOCK_MASK   0x00000F00

/* GTPStatusB bits and masks */
#define TI_GTPSTATUSB_CHANNEL_BONDING_MASK         0x000000FF
#define TI_GTPSTATUSB_DATA_ERROR_MASK              0x0000FF00
#define TI_GTPSTATUSB_DISPARITY_ERROR_MASK         0x00FF0000
#define TI_GTPSTATUSB_DATA_NOT_IN_TABLE_ERROR_MASK 0xFF000000

/* GTPtriggerBufferLength bits and masks */
#define TI_GTPTRIGGERBUFFERLENGTH_GLOBAL_LENGTH_MASK 0x000007FF
#define TI_GTPTRIGGERBUFFERLENGTH_SUBSYS_LENGTH_MASK 0x07FF0000
#define TI_GTPTRIGGERBUFFERLENGTH_HFBR1_MGT_ERROR    (1<<28)
#define TI_GTPTRIGGERBUFFERLENGTH_CLK250_DCM_LOCK    (1<<29)
#define TI_GTPTRIGGERBUFFERLENGTH_CLK125_DCM_LOCK    (1<<30)
#define TI_GTPTRIGGERBUFFERLENGTH_VMECLK_DCM_LOCK    (1<<31)
#endif /* TSONLY */

/* blockStatus bits and masks */
#define TI_BLOCKSTATUS_NBLOCKS_READY0    0x000000FF
#define TI_BLOCKSTATUS_NBLOCKS_NEEDACK0  0x0000FF00
#define TI_BLOCKSTATUS_NBLOCKS_READY1    0x00FF0000
#define TI_BLOCKSTATUS_NBLOCKS_NEEDACK1  0xFF000000

/* adr24 bits and masks */
#define TI_ADR24_ADDRESS_MASK         0x0000001F
#define TI_ADR24_HARDWARE_SET_MASK    0x000003E0
#define TI_ADR24_TM_NBLOCKS_READY1    0x00FF0000
#define TI_ADR24_TM_NBLOCKS_NEEDACK1  0xFF000000

/* syncEventCtrl bits and masks */
#define TI_SYNCEVENTCTRL_NBLOCKS_MASK 0x0000FFFF
#define TI_SYNCEVENTCTRL_ENABLE       0x005A0000

/* scalerLatchControl bits and masks */
#define TI_SCALERCTRL_FP_LATCH_ENABLE  (1<<0)
#define TI_SCALERCTRL_FP_RESET_ENABLE  (1<<1)

/* reset bits and masks */
#define TI_RESET_I2C                  (1<<1)
#define TI_RESET_JTAG                 (1<<2)
#define TI_RESET_SFM                  (1<<3)
#define TI_RESET_SOFT                 (1<<4)
#define TI_RESET_SYNC_HISTORY         (1<<6)
#define TI_RESET_BUSYACK              (1<<7)
#define TI_RESET_CLK250               (1<<8)
#define TI_RESET_CLK200               (1<<8)
#define TI_RESET_CLK125               (1<<9)
#define TI_RESET_MGT                  (1<<10)
#define TI_RESET_AUTOALIGN_HFBR1_SYNC (1<<11)
#define TI_RESET_AUTOALIGN_HFBR5_SYNC (1<<12)
#define TI_RESET_RAM_WRITE            (1<<12)
#define TI_RESET_FIBER_AUTO_ALIGN     (1<<13)
#define TI_RESET_IODELAY              (1<<14)
#define TI_RESET_MEASURE_LATENCY      (1<<15)
#define TI_RESET_TAKE_TOKEN           (1<<16)
#define TI_RESET_BLOCK_READOUT        (1<<17)
#define TI_RESET_FORCE_SYNCEVENT      (1<<20)
#define TI_RESET_SCALERS_LATCH        (1<<24)
#define TI_RESET_SCALERS_RESET        (1<<25)

/* Trigger Sources, used by tiSetTriggerSource  */
#define TI_TRIGGER_P0        0
#define TI_TRIGGER_HFBR1     1
#define TI_TRIGGER_FPTRG     2
#define TI_TRIGGER_TSINPUTS  3
#define TI_TRIGGER_TSREV2    4
#define TI_TRIGGER_RANDOM    5
#define TI_TRIGGER_PULSER    5

/* Define default Interrupt vector and level */
#define TI_INT_VEC      0xec
/* #define TI_INT_VEC      0xc8 */
#define TI_INT_LEVEL    5

/* i2c data masks - 16bit data default */
#define TI_I2C_DATA_MASK             0x0000ffff
#define TI_I2C_8BIT_DATA_MASK        0x000000ff

/* Data buffer bits and masks */
#define TI_DUMMY_DATA                      0xDECEA5ED
#define TI_EMPTY_FIFO                      0xF0BAD0F0
#define TI_BLOCK_HEADER_CRATEID_MASK       0xFF000000
#define TI_BLOCK_HEADER_SLOTS_MASK         0x001F0000
#define TI_BLOCK_TRAILER_CRATEID_MASK      0x00FF0000
#define TI_BLOCK_TRAILER_SLOTS_MASK        0x1F000000
#define TI_DATA_BLKNUM_MASK                0x0000FF00
#define TI_DATA_BLKLEVEL_MASK              0x000000FF

/* Function prototypes */
int  tiInit(unsigned int tAddr, unsigned int mode, int force);
unsigned int tiFind();
int  tiCheckAddresses();
void tiStatus();
int  tiGetFirmwareVersion();
int  tiReload();
unsigned int tiGetSerialNumber(char **rSN);
int  tiClockResync();
int  tiReset();
int  tiSetCrateID(unsigned int crateID);
int  tiSetBlockLevel(unsigned int blockLevel);
int  tiSetTriggerSource(int trig);
int  tiSetTriggerSourceMask(int trigmask);
int  tiEnableTriggerSource();
int  tiDisableTriggerSource();
int  tiSetSyncSource(unsigned int sync);
int  tiSetEventFormat(int format);
int  tiSoftTrig(int trigger, unsigned int nevents, unsigned int period_inc, int range);
int  tiSetRandomTrigger(int trigger, int setting);
int  tiDisableRandomTrigger();
int  tiReadBlock(volatile unsigned int *data, int nwrds, int rflag);
int  tiEnableFiber(unsigned int fiber);
int  tiDisableFiber(unsigned int fiber);
int  tiSetBusySource(unsigned int sourcemask, int rFlag);
void tiEnableBusError();
void tiDisableBusError();
int  tiPayloadPort2VMESlot(int payloadport);
int  tiPayloadPortMask2VMESlotMask(int payloadport_mask);
int  tiVMESlot2PayloadPort(int vmeslot);
int  tiVMESlotMask2PayloadPortMask(int vmeslot_mask);
int  tiSetPrescale(int prescale);
int  tiGetPrescale();
int  tiSetTriggerPulse(int trigger, int delay, int width);
void tiSetSyncDelayWidth(unsigned int delay, unsigned int width, int widthstep);
void tiTrigLinkReset();
void tiSyncReset();
void tiClockReset();
int  tiSetAdr32(unsigned int a32base);
int  tiDisableA32();
unsigned int  tiBReady();
int  tiGetSyncEventFlag();
int  tiGetSyncEventReceived();
int  tiSetBlockBufferLevel(unsigned int level);
int  tiEnableTSInput(unsigned int inpMask);
int  tiDisableTSInput(unsigned int inpMask);
int  tiSetOutputPort(unsigned int set1, unsigned int set2, unsigned int set3, unsigned int set4);
int  tiSetClockSource(unsigned int source);
int  tiGetClockSource();
void  tiSetFiberDelay(unsigned int delay, unsigned int offset);
int  tiAddSlave(unsigned int fiber);
int  tiSetTriggerHoldoff(int rule, unsigned int value, int timestep);
int  tiGetTriggerHoldoff(int rule);

int  tiDisableDataReadout();
int  tiEnableDataReadout();
void tiResetBlockReadout();

int  tiLoadTriggerTable();
unsigned int tiGetLiveTime();
unsigned int tiGetBusyTime();

unsigned int tiGetDaqStatus();
int  tiVmeTrigger1();
int  tiVmeTrigger2();

int  tiGetSWBBusy();
int  tiSetTokenTestMode(int mode);
int  tiSetTokenOutTest(int level);

int  tiSetUserSyncResetReceive(int enable);
int  tiGetLastSyncCodes(int pflag);
int  tiGetSyncHistoryBufferStatus(int pflag);
void tiResetSyncHistory();
void tiUserSyncReset(int enable);
void tiPrintSyncHistory();

/* Library Interrupt/Polling routine prototypes */
int  tiIntConnect(unsigned int vector, VOIDFUNCPTR routine, unsigned int arg);
int  tiIntDisconnect();
int  tiAckConnect(VOIDFUNCPTR routine, unsigned int arg);
void tiIntAck();
int  tiIntEnable(int iflag);
void tiIntDisable();
unsigned int  tiGetIntCount();


#endif /* TILIB_H */
