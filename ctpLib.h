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
 *     Status and Control library for the JLAB Crate Trigger Processor
 *     (CTP) module using an i2c interface from the JLAB Trigger
 *     Interface/Distribution (TID) module.
 *
 * SVN: $Rev$
 *
 *----------------------------------------------------------------------------*/
#ifndef CTPLIB_H
#define CTPLIB_H

/* Structure to handle the similarities between the FPGA registers 
   The VLX110 has extra registers and will be handled separately */
struct CTP_FPGAStruct
{
  /* 0xn00 */ volatile unsigned int status0;   /* Address 0 */
  /* 0xn04 */ volatile unsigned int status1;   /* Address 1 */
  /* 0xn08 */ volatile unsigned int config0;   /* Address 2 */
  /* 0xn0C */ volatile unsigned int config1;   /* Address 3 */
  /* 0xn10 */ volatile unsigned int temp;      /* Address 4 */
  /* 0xn14 */ volatile unsigned int vint;      /* Address 5 */
};

struct CTPStruct
{
  /* 0x0000 */          unsigned int blankCTP0[(0x3C00-0x0000)/4];
  /* 0x3C00 */ struct   CTP_FPGAStruct fpga1;  /* Device 1 */
  /* 0x3C18 */          unsigned int blankCTP1[(0x5C00-0x3C18)/4];
  /* 0x5C00 */ struct   CTP_FPGAStruct fpga2;  /* Device 2 */
  /* 0x5C18 */          unsigned int blankCTP2[(0x7C00-0x5C18)/4];
  /* 0x7C00 */ struct   CTP_FPGAStruct fpga3;  /* Device 3 */
  /* 0x7C18 */          unsigned int RFU[2];             /* Address 6-7 */
  /* 0x7C20 */ volatile unsigned int sum_threshold_lsb;  /* Address 8 */
  /* 0x7C24 */ volatile unsigned int sum_threshold_msb;  /* Address 9 */
  /* 0x7C28 */ volatile unsigned int history_buffer_lsb; /* Address 10 */
  /* 0x7C2C */ volatile unsigned int history_buffer_msb; /* Address 11 */
  /* 0x7C30 */ volatile unsigned int testCSR;            /* Address 12 */
  /* 0x7C34 */ volatile unsigned int testClockFreq;      /* Address 13 */
  /* 0x7C38 */ volatile unsigned int testSyncCount;      /* Address 14 */
  /* 0x7C3C */ volatile unsigned int testTrig1Count;     /* Address 15 */
  /* 0x7C40 */ volatile unsigned int testTrig2Count;     /* Address 16 */
  /* 0x7C44 */ volatile unsigned int fiberReset;         /* Address 17 */
  /* 0x7C48 */          unsigned int blankCTP3[(0x10000-0x7C48)/4];
};

/* CTP Register bits and masks */
/* Lane_up mask shifts by 2 bits for each channel (two lanes/channel) */
#define CTP_FPGA_STATUS0_LANE_UP_MASK            0x3

#define CTP_FPGA_STATUS1_FIRMWARE_VERSION_MASK   0xFE00
#define CTP_FPGA_STATUS0_FADC_CHANUP(chan)       (1<<(12+chan))
#define CTP_FPGA_STATUS1_FADC4_CHANUP            (1<<0)
#define CTP_FGPA_STATUS1_ALLCHANUP               (1<<1)
#define CTP_FPGA_STATUS1_FADC5_CHANUP            (1<<2)
#define CTP_FPGA_STATUS1_ERROR_LATCH_FS          (1<<8)

/* History buffer only for VLX110 */
#define CTP_FPGA3_CONFIG1_ARM_HISTORY_BUFFER      (1<<0)
#define CTP_FPGA3_STATUS1_HISTORY_BUFFER_READY    (1<<3)

#define CTP_FPGA3_CONFIG1_RESET_ALL_GTP          (1<<1)
#define CTP_FPGA3_CONFIG1_RESET_SSP_MGT          (1<<2)

#define CTP_DATA_MASK                            0xFFFFF

#define CTP_TESTCSR_RESET_SYNC_COUNTER           (0<<1)
#define CTP_TESTCSR_RESET_TRIG1_COUNTER          (1<<1)
#define CTP_TESTCSR_RESET_TRIG2_COUNTER          (2<<1)

int  ctpInit();
int  ctpStatus();
int  ctpSetFinalSumThreshold(unsigned int threshold, int arm);
int  ctpGetFinalSumThreshold();
int  ctpSetPayloadEnableMask(int enableMask);
int  ctpSetVmeSlotEnableMask(unsigned int vmemask);
int  ctpEnableSlotMask(unsigned int inMask);
int  ctpResetGTP();
int  ctpGetAllChanUp();
int  ctpGetErrorLatchFS(int pflag);
int  ctpArmHistoryBuffer();
int  ctpDReady();
int  ctpReadEvent(volatile unsigned int *data, int nwrds);
void ctpFiberReset();
void ctpPayloadReset();
int  ctpTestResetCounter(int type);
int  ctpTestGetClockFreq();
int  ctpTestGetSyncCount();
int  ctpTestGetTrig1Count();
int  ctpTestGetTrig2Count();

#endif /* CTPLIB_H */
