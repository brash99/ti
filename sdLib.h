/*----------------------------------------------------------------------------*
 *  Copyright (c) 2010        Southeastern Universities Research Association, *
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
 *     Status and Control library for the JLAB Signal Distribution
 *     (SD) module using an i2c interface from the JLAB Trigger
 *     Interface (TI) module.
 *
 *----------------------------------------------------------------------------*/
#ifndef SDLIB_H
#define SDLIB_H

#define SD_SUPPORTED_FIRMWARE  0xA5

#define TEST
/* JLab SD Register definitions (defined within TI register space) */
struct SDStruct
{
  /* 0x0000 */          unsigned int blankSD0[(0x3C00-0x0000)/4];
  /* 0x3C00 */ volatile unsigned int system;  /* Device 1,  Address 0x0 */
  /* 0x3C04 */ volatile unsigned int status;             /* Address 0x1 */
  /* 0x3C08 */ volatile unsigned int payloadPorts;       /* Address 0x2 */
  /* 0x3C0C */ volatile unsigned int tokenPorts;         /* Address 0x3 */
  /* 0x3C10 */ volatile unsigned int busyoutPorts;       /* Address 0x4 */
  /* 0x3C14 */ volatile unsigned int trigoutPorts;       /* Address 0x5 */
  /* 0x3C18 */          unsigned int RFU0;               /* Address 0x6 */
  /* 0x3C1C */ volatile unsigned int busyoutStatus;      /* Address 0x7 */
  /* 0x3C20 */ volatile unsigned int trigoutStatus;      /* Address 0x8 */
  /* 0x3C24 */ volatile unsigned int busyoutCounter[16]; /* Address 0x9-0x18 */
  /* 0x3C64 */ volatile unsigned int busyoutTest;        /* Address 0x19 */
  /* 0x3C68 */ volatile unsigned int sdLinkTest;         /* Address 0x1A */
  /* 0x3C6C */ volatile unsigned int tokenInTest;        /* Address 0x1B */
  /* 0x3C70 */ volatile unsigned int trigOutTest;        /* Address 0x1C */
  /* 0x3C74 */ volatile unsigned int tokenOutTest;       /* Address 0x1D */
  /* 0x3C78 */ volatile unsigned int statBitBTest;       /* Address 0x1E */
  /* 0x3C7C */ volatile unsigned int version;            /* Address 0x1F */
  /* 0x3C80 */ volatile unsigned int csrTest;            /* Address 0x20 */
  /* 0x3C84 */ volatile unsigned int trigoutCounter[16]; /* Address 0x21-0x30 */
  /* 0x3CC4 */ volatile unsigned int clkACounterTest;    /* Address 0x31 */
  /* 0x3CC8 */ volatile unsigned int clkBCounterTest;    /* Address 0x32 */
  /* 0x3CCC */          unsigned int blankSD1[(0x3D14-0x3CCC)/4];
  /* 0x3D14 */ volatile unsigned int memAddrLSB;         /* Address 0x45 */
  /* 0x3D18 */ volatile unsigned int memAddrMSB;         /* Address 0x46 */
  /* 0x3D1C */ volatile unsigned int memWriteCtrl;       /* Address 0x47 */
  /* 0x3D20 */ volatile unsigned int memReadCtrl;        /* Address 0x48 */
  /* 0x3D24 */ volatile unsigned int memCheckStatus;     /* Address 0x49 */
  /* 0x3D28 */          unsigned int blankSD2[(0x10000-0x3D28)/4];
};

/* Initialization flags */
#define SD_INIT_IGNORE_VERSION         (1<<0)

/* 0x0 system bits and masks */
#define SD_SYSTEM_CLKA_BYPASS_MODE     (1<<0)
#define SD_SYSTEM_CLKA_FREQUENCY_MASK  0x000C
#define SD_SYSTEM_CLKB_BYPASS_MODE     (1<<4)
#define SD_SYSTEM_CLKB_FREQUENCY_MASK  0x00C0
#define SD_SYSTEM_TI_LINK_ENABLE       (1<<9)
#define SD_SYSTEM_TEST_RESET           (1<<15)

/* 0x4 status bits and masks */
#define SD_STATUS_CLKB_LOSS_OF_SIGNAL   (1<<0)
#define SD_STATUS_CLKB_LOSS_OF_LOCK     (1<<1)
#define SD_STATUS_CLKA_LOSS_OF_SIGNAL   (1<<2)
#define SD_STATUS_CLKA_LOSS_OF_LOCK     (1<<3)
#define SD_STATUS_POWER_FAULT           (1<<4)
#define SD_STATUS_TRIGOUT               (1<<5)
#define SD_STATUS_BUSYOUT               (1<<6)
#define SD_STATUS_CLKA_DETECTED_MASK     0x300
#define SD_STATUS_CLKA_DETECTED_UNKNOWN (0<<8)
#define SD_STATUS_CLKA_DETECTED_31_25   (1<<8)
#define SD_STATUS_CLKA_DETECTED_125     (2<<8)
#define SD_STATUS_CLKA_DETECTED_250     (3<<8)
#define SD_STATUS_CLKB_DETECTED_MASK     0xC00
#define SD_STATUS_CLKB_DETECTED_UNKNOWN (0<<10)
#define SD_STATUS_CLKB_DETECTED_31_25   (1<<10)
#define SD_STATUS_CLKB_DETECTED_125     (2<<10)
#define SD_STATUS_CLKB_DETECTED_250     (3<<10)

#define SD_CSRTEST_CLKA_PLL_BYPASS      (1<<0)
#define SD_CSRTEST_CLKA_TEST_STATUS     (1<<1)
#define SD_CSRTEST_CLKA_FREQ            ((1<<2)|(1<<3))
#define SD_CSRTEST_CLKB_PLL_BYPASS      (1<<4)
#define SD_CSRTEST_CLKB_TEST_STATUS     (1<<5)
#define SD_CSRTEST_CLKB_FREQ            ((1<<6)|(1<<7))
#define SD_CSRTEST_TI_BUSYOUT           (1<<8)
#define SD_CSRTEST_TI_TOKENIN           (1<<9)
#define SD_CSRTEST_TI_GTPLINK           (1<<10)
#define SD_CSRTEST_SWA_LOOPBACK0        (1<<11)
#define SD_CSRTEST_SWA_LOOPBACK1        (1<<12)
#define SD_CSRTEST_SWA_LOOPBACK2        (1<<13)
#define SD_CSRTEST_SWA_LOOPBACK_MASK    (SD_CSRTEST_SWA_LOOPBACK0|SD_CSRTEST_SWA_LOOPBACK1|SD_CSRTEST_SWA_LOOPBACK2)
#define SD_CSRTEST_TEST_RESET           (1<<15)

/* Bits and Masks used for Remote Programming */
#define SD_MEMADDR_LSB_MASK             0xFFFF
#define SD_MEMADDR_MSB_MASK             0x00FF
#define SD_MEMWRITECTRL_DATA_MASK       0x00FF
#define SD_MEMWRITECTRL_WRITE           (1<<8)
#define SD_MEMWRITECTRL_WREN            (1<<9)
#define SD_MEMWRITECTRL_SHIFT_BYTES     (1<<10)
#define SD_MEMWRITECTRL_SECTOR_ERASE    (1<<12)
#define SD_MEMWRITECTRL_SECTOR_PROTECT  (1<<13)
#define SD_MEMREADCTRL_DATA_MASK        0x00FF
#define SD_MEMREADCTRL_READ             (1<<8)
#define SD_MEMREADCTRL_RDEN             (1<<9)
#define SD_MEMREADCTRL_READ_STATUS      (1<<10)
#define SD_MEMREADCTRL_NEGATE_BUSY      (1<<11)


/* SD routine prototypes */
int  sdInit(int flag);
int  sdStatus();
int  sdGetFirmwareVersion(int pflag);
int  sdSetPLLClockFrequency(int iclk, int ifreq);
int  sdGetPLLClockFrequency(int iclk);
int  sdGetClockFrequency(int iclk, int pflag);
int  sdSetClockMode(int iclk, int imode);
int  sdGetClockMode(int iclk);
int  sdReset();
int  sdSetActivePayloadPorts(unsigned int imask);
int  sdSetActiveVmeSlots(unsigned int vmemask);
int  sdGetActivePayloadPorts();
int  sdGetBusyoutCounter(int ipayload);
int  sdPrintBusyoutCounters();
int  sdGetTrigoutCounter(int ipayload);
int  sdPrintTrigoutCounters();
int  sdGetBusyoutStatus(int pflag);

#ifndef VXWORKSPPC
int  sdFirmwareFlushFifo();
int  sdFirmwareLoadFile(char *filename);
void sdFirmwareFreeMemory();
int  sdFirmwareVerifyPage(unsigned int mem_addr);
int  sdFirmwareVerifyPageZero(unsigned int mem_addr);
void sdFirmwareWritePage(unsigned int mem_addr);
int  sdFirmwareWriteToMemory();
int  sdFirmwareVerifyMemory();
int  sdFirmwareReadStatus();
void sdFirmwareWriteSpecs(unsigned int addr, unsigned int serial_number,
			  unsigned int hall_board_version, unsigned int firmware_version);
#endif

int  sdFirmwareReadAddr(unsigned int addr);
unsigned int sdGetSerialNumber(char *rSN);

/* SD Test Mode routines */
int  sdTestGetBusyout();
int  sdTestGetSdLink();
int  sdTestGetTokenIn();
int  sdTestGetTrigOut();
void sdTestSetTokenOutMask(int mask);
void sdTestSetStatBitBMask(int mask);
void sdTestSetClkAPLL(int mode);
int  sdTestGetClockAStatus();
int  sdTestGetClockAFreq();
void sdTestSetClkBPLL(int mode);
int  sdTestGetClockBStatus();
int  sdTestGetClockBFreq();
void sdTestSetTIBusyOut(int level);
int  sdTestGetTITokenIn();
void sdTestSetTIGTPLink(int level);
unsigned int sdTestGetClkACounter();
unsigned int sdTestGetClkBCounter();
unsigned int sdTestGetSWALoopback();

#endif /* SDLIB_H */
