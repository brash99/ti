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
 *     Interface (TI) module.
 *
 *----------------------------------------------------------------------------*/
#ifndef CTPLIB_H
#define CTPLIB_H

struct CTP_FPGA_U1_Struct
{
  /* 0xn00 */ volatile unsigned int status0;   /* Address 0 */
  /* 0xn04 */ volatile unsigned int status1;   /* Address 1 */
  /* 0xn08 */ volatile unsigned int config0;   /* Address 2 */
  /* 0xn0C */ volatile unsigned int config1;   /* Address 3 */
  /* 0xn10 */ volatile unsigned int temp;      /* Address 4 */
  /* 0xn14 */ volatile unsigned int vint;      /* Address 5 */
  /* 0xn18 */ volatile unsigned int config2;   /* Address 6 */
  /* 0xn1C */ volatile unsigned int config3;   /* Address 7 */
  /* 0xn20 */ volatile unsigned int config4;   /* Address 8 */
  /* 0xn24 */ volatile unsigned int config5;   /* Address 9 */
  /* 0xn28 */ volatile unsigned int config6;   /* Address 10 */
  /* 0xn2C */ volatile unsigned int status3;   /* Address 11 */
  /* 0xn30 */          unsigned int blank0[(0x44-0x30)/4];   /* Address 12-16 */
  /* 0xn44 */ volatile unsigned int status2;   /* Address 17 */
};

struct CTP_FPGA_U3_Struct
{
  /* 0xn00 */ volatile unsigned int status0;   /* Address 0 */
  /* 0xn04 */ volatile unsigned int status1;   /* Address 1 */
  /* 0xn08 */ volatile unsigned int config0;   /* Address 2 */
  /* 0xn0C */ volatile unsigned int config1;   /* Address 3 */
  /* 0xn10 */ volatile unsigned int temp;      /* Address 4 */
  /* 0xn14 */ volatile unsigned int vint;      /* Address 5 */
  /* 0xn18 */ volatile unsigned int config2;   /* Address 6 */
  /* 0xn1C */ volatile unsigned int config3;   /* Address 7 */
  /* 0xn20 */          unsigned int blank0[(0x2C-0x20)/4];   /* Address 8-10 */
  /* 0xn2C */ volatile unsigned int status3;   /* Address 11 */
  /* 0xn30 */          unsigned int blank1[(0x44-0x30)/4];   /* Address 12-16 */
  /* 0xn44 */ volatile unsigned int status2;   /* Address 17 */
};

struct CTP_FPGA_U24_Struct
{
  /* 0xn00 */ volatile unsigned int status0;             /* Address 0 */
  /* 0xn04 */ volatile unsigned int status1;             /* Address 1 */
  /* 0xn08 */ volatile unsigned int config0;             /* Address 2 */
  /* 0xn0C */ volatile unsigned int config1;             /* Address 3 */
  /* 0xn10 */ volatile unsigned int temp;                /* Address 4 */
  /* 0xn14 */ volatile unsigned int vint;                /* Address 5 */
  /* 0xn18 */ volatile unsigned int config2;             /* Address 6 */
  /* 0xn1C */ volatile unsigned int config3;             /* Address 7 */
  /* 0xn20 */ volatile unsigned int sum_threshold_lsb;   /* Address 8 */
  /* 0xn24 */ volatile unsigned int sum_threshold_msb;   /* Address 9 */
  /* 0xn28 */ volatile unsigned int history_buffer_lsb;  /* Address 10 */
  /* 0xn2C */ volatile unsigned int history_buffer_msb;  /* Address 11 */
  /* 0xn30 */ volatile unsigned int scaler_ctrl;         /* Address 12 */
  /* 0xn34 */ volatile unsigned int clock_scaler;        /* Address 13 */
  /* 0xn38 */ volatile unsigned int sync_scaler;         /* Address 14 */
  /* 0xn3C */ volatile unsigned int trig1_scaler;        /* Address 15 */
  /* 0xn40 */ volatile unsigned int trig2_scaler;        /* Address 16 */
  /* 0xn44 */ volatile unsigned int status2;             /* Address 17 */
};

struct CTPStruct
{
  /* 0x0000 */          unsigned int blank0[(0x3C00-0x0000)/4];
  /* 0x3C00 */ struct   CTP_FPGA_U1_Struct fpga1;    /* I2C Device 1 */
  /* 0x3C18 */          unsigned int blank1[(0x5C00-0x3C48)/4];
  /* 0x5C00 */ struct   CTP_FPGA_U3_Struct fpga3;    /* I2C Device 2 */
  /* 0x5C18 */          unsigned int blank2[(0x7C00-0x5C48)/4];
  /* 0x7C00 */ struct   CTP_FPGA_U24_Struct fpga24;  /* I2C Device 3 */
  /* 0x7C48 */          unsigned int blank3[(0x10000-0x7C48)/4];
};

/* CTP Register bits and masks */
/* Lane_up mask shifts by 2 bits for each channel (two lanes/channel) */

/* Common bits and masks for all FPGAs */
#define CTP_FPGA_STATUS0_CHAN_UP(chan)    (1<<(chan+12))
#define CTP_FPGA_STATUS0_LANE0_UP(chan)   (1<<(2*chan))
#define CTP_FPGA_STATUS0_LANE1_UP(chan)   (1<<(2*chan+1))

#define CTP_FPGA_STATUS1_CHANUP_EXTRA1           (1<<0)
#define CTP_FGPA_STATUS1_ALLCHANUP               (1<<1)
#define CTP_FPGA_STATUS1_CHANUP_EXTRA2           (1<<2)

#define CTP_FGPA_CONFIG1_INIT_ALL_MGT            (1<<1)

#define CTP_FPGA_STATUS2_FIRMWARE_VERSION_MASK   0x7FFF

/* U1: config2 bits */
#define CTP_FPGA1_CONFIG2_PROG_DATA_U1    (0x0<<3)
#define CTP_FPGA1_CONFIG2_PROG_DATA_U3    (0x1<<3)
#define CTP_FPGA1_CONFIG2_PROG_DATA_U24   (0x2<<3)
#define CTP_FPGA1_CONFIG2_READ_DATA_U1    (0x3<<3)
#define CTP_FPGA1_CONFIG2_READ_DATA_U3    (0x4<<3)
#define CTP_FPGA1_CONFIG2_READ_DATA_U24   (0x5<<3)
#define CTP_FPGA1_CONFIG2_READ_EPROM_U1   (0x6<<3)
#define CTP_FPGA1_CONFIG2_READ_EPROM_U3   (0x7<<3)
#define CTP_FPGA1_CONFIG2_READ_EPROM_U24  (0x8<<3)
#define CTP_FPGA1_CONFIG2_ERASE_EPROM_U1  (0x9<<3)
#define CTP_FPGA1_CONFIG2_ERASE_EPROM_U3  (0xA<<3)
#define CTP_FPGA1_CONFIG2_ERASE_EPROM_U24 (0xB<<3)
#define CTP_FPGA1_CONFIG2_SRAM_WRITE        (1<<7)
#define CTP_FPGA1_CONFIG2_EXEC_OPCODE       (1<<8)
#define CTP_FPGA1_CONFIG2_U1_CONFIG       (0x1<<9)
#define CTP_FPGA1_CONFIG2_U3_CONFIG       (0x2<<9)
#define CTP_FPGA1_CONFIG2_U24_CONFIG      (0x3<<9)

/* U1: config3 masks */
#define CTP_FPGA1_CONFIG3_ERASE_ROM_BLOCK_NUM  0x3FF

/* U1: config4 masks */
#define CTP_FPGA1_CONFIG4_SRAM_DATA_MASK      0xFFFF

/* U1: config5 masks */
#define CTP_FPGA1_CONFIG5_SRAM_ADDR_MASK      0xFFFF

/* U1: config6 bits and masks */
#define CTP_FPGA1_CONFIG6_SRAM_ADDR_MASK       0x3F
#define CTP_FPGA1_CONFIG6_SRAM_READ         (1<<14)
#define CTP_FPGA1_CONFIG6_SRAM_WRITE        (1<<15)

/* U1: status3 masks */
#define CTP_FPGA1_STATUS3_SRAM_DATA_MASK    0xFFFF

/* U1: status2 bits */
#define CTP_FPGA1_STATUS2_READY_FOR_OPCODE      (1<<15) 

/* U3: config2 bits and masks */
#define CTP_FPGA3_CONFIG2_SROM_ADDR_MASK    0x3FF
#define CTP_FPGA3_CONFIG2_SROM_READ       (1<<15)

/* U3: config3 bits */
#define CTP_FPGA3_CONFIG3_REBOOT_ALL_FPGA  (5<<0)

/* U3: status3 bits and masks */
#define CTP_FPGA3_STATUS3_SROM_DATA_MASK     0xFF
#define CTP_FPGA3_STATUS3_SROM_DATA_VALID (1<<15)

/* U24: status1 bits */
#define CTP_FPGA24_STATUS1_HISTORY_BUFFER_READY   (1<<3)
#define CTP_FPGA24_STATUS1_FIBER_LANE_CHAN_ALIGN  (1<<4)
#define CTP_FPGA24_STATUS1_FIBER_LANE_BYTE_ALIGN  (1<<5)
#define CTP_FPGA24_STATUS1_FIBER_LANE_REMOTE_UP   (1<<6)
#define CTP_FPGA24_STATUS1_FIBER_CHAN_READY       (1<<7)
#define CTP_FPGA24_STATUS1_ERROR_LATCH_FS         (1<<8)

/* U24: config1 bits */
#define CTP_FPGA24_CONFIG1_RESET_FIBER_MGT         (1<<2)
#define CTP_FPGA24_CONFIG1_ARM_HISTORY_BUFFER      (1<<0)

/* U24: Scaler bits and masks */
#define CTP_SCALER_CTRL_RESET_SYNC               (1<<0)
#define CTP_SCALER_CTRL_RESET_TRIG1              (1<<1)
#define CTP_SCALER_CTRL_RESET_TRIG2              (1<<2)

#define CTP_CLOCK_SCALER_COUNT_MASK              0xFF
#define CTP_SCALER_COUNT_MASK                    0xFFFF

#define CTP_DATA_MASK                            0xFFFFF


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

int  ctpResetScalers();
int  ctpResetSyncScaler();
int  ctpResetTrig1Scaler();
int  ctpResetTrig2Scaler();
int  ctpGetClockScaler();
int  ctpGetSyncScaler();
int  ctpGetTrig1Scaler();
int  ctpGetTrig2Scaler();

int  ctpGetSerialNumber(char **rval);

#endif /* CTPLIB_H */
