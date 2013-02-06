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
 *     Interface/Distribution (TID) module.
 *
 * SVN: $Rev$
 *
 *----------------------------------------------------------------------------*/
#ifndef SDLIB_H
#define SDLIB_H

#define TEST
/* JLab SD Register definitions (defined within TID register space) */
struct SDStruct
{
  /* 0x0000 */          unsigned int blankSD0[(0x3C00-0x0000)/4];
  /* 0x3C00 */ volatile unsigned int system;  /* Device 1,  Address 0 */
  /* 0x3C04 */ volatile unsigned int status;             /* Address 1 */
  /* 0x3C08 */ volatile unsigned int payloadPorts;       /* Address 2 */
  /* 0x3C0C */ volatile unsigned int tokenPorts;         /* Address 3 */
  /* 0x3C10 */ volatile unsigned int busyoutPorts;       /* Address 4 */
  /* 0x3C14 */ volatile unsigned int trigoutPorts;       /* Address 5 */
  /* 0x3C18 */ volatile unsigned int busyoutStatus;      /* Address 6 */
  /* 0x3C1C */ volatile unsigned int trigoutStatus;      /* Address 7 */
  /* 0x3C20 */ volatile unsigned int busyoutCounter[16]; /* Address 8-23 */
#ifdef TEST
  /* 0x3C60 */          unsigned int RFU;                /* Address 24 */
  /* 0x3C64 */ volatile unsigned int busyoutTest;        /* Address 25 */
  /* 0x3C68 */ volatile unsigned int sdLinkTest;         /* Address 26 */
  /* 0x3C6C */ volatile unsigned int tokenInTest;        /* Address 27 */
  /* 0x3C70 */ volatile unsigned int trigOutTest;        /* Address 28 */
  /* 0x3C74 */ volatile unsigned int tokenOutTest;       /* Address 29 */
  /* 0x3C78 */ volatile unsigned int statBitBTest;       /* Address 30 */
  /* 0x3C7C */ volatile unsigned int version;            /* Address 31 */
  /* 0x3C80 */ volatile unsigned int csrTest;            /* Address 32 */
  /* 0x3C84 */ volatile unsigned int clkACounterTest;    /* Address 33 */
  /* 0x3C88 */ volatile unsigned int clkBCounterTest;    /* Address 34 */
  /* 0x3C8C */          unsigned int blankSD1[(0x3D14-0x3C8C)/4];
#else
  /* 0x3C60 */          unsigned int RFU[7];             /* Address 24-30 */
  /* 0x3C7C */ volatile unsigned int version;            /* Address 31 */
  /* 0x3C80 */          unsigned int blankSD1[(0x3D14-0x3C80)/4];
#endif
  /* 0x3D14 */          unsigned int memAddrLSB;         /* Address 69 (0x45) */
  /* 0x3D18 */          unsigned int memAddrMSB;         /* Address 70 (0x46) */
  /* 0x3D1C */          unsigned int memWriteCtrl;       /* Address 71 (0x47) */
  /* 0x3D20 */          unsigned int memReadCtrl;        /* Address 72 (0x48) */
  /* 0x3D24 */          unsigned int memCheckStatus;     /* Address 73 */
  /* 0x3D28 */          unsigned int blankSD2[(0x10000-0x3D28)/4];
};

/* SD status bits and masks - not meaningful at the moment? */
#define SD_STATUS_CLKA_BYPASS_MODE     (1<<0)
#define SD_STATUS_CLKA_RESET           (1<<1)
#define SD_STATUS_CLKA_FREQUENCY_MASK  0x000C
#define SD_STATUS_CLKB_BYPASS_MODE     (1<<4)
#define SD_STATUS_CLKB_RESET           (1<<5)
#define SD_STATUS_CLKB_FREQUENCY_MASK  0x00C0
#define SD_STATUS_RESET                (1<<15)

/* SD status2 bits and masks */
#define SD_STATUS2_CLKB_LOSS_OF_SIGNAL  (1<<0)
#define SD_STATUS2_CLKB_LOSS_OF_LOCK    (1<<1)
#define SD_STATUS2_CLKA_LOSS_OF_SIGNAL  (1<<2)
#define SD_STATUS2_CLKA_LOSS_OF_LOCK    (1<<3)
#define SD_STATUS2_POWER_FAULT          (1<<4)
#define SD_STATUS2_TRIGOUT              (1<<5)
#define SD_STATUS2_BUSYOUT              (1<<6)
#define SD_STATUS2_LAST_TOKEN_ADDR_MASK (0x1F00)

#ifdef TEST
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
#endif

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
int  sdInit();
int  sdStatus();
int  sdSetClockFrequency(int iclk, int ifreq);
int  sdGetClockFrequency(int iclk);
int  sdSetClockMode(int iclk, int imode);
int  sdGetClockMode(int iclk);
int  sdResetPLL(int iclk);
int  sdReset();
int  sdSetActivePayloadPorts(unsigned int imask);
int  sdSetActiveVmeSlots(unsigned int vmemask);
int  sdGetActivePayloadPorts();
int  sdGetBusyoutCounter(int ipayload);

unsigned int sdGetSerialNumber(char *rSN);

#ifdef TEST
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
#endif

#endif /* SDLIB_H */
