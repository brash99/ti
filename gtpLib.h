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
 *     Status and Control library for the JLAB Global Trigger Processor
 *     (GTP) module using an i2c interface from the JLAB Trigger
 *     Interface/Distribution (TID) module.
 *
 * SVN: $Rev$
 *
 *----------------------------------------------------------------------------*/
#ifndef GTPLIB_H
#define GTPLIB_H

/* JLab GTP Register definitions (defined within TID register space) */
struct GTPStruct
{
  /* 0x0000 */          unsigned int blankGTP0[(0x3C00-0x0000)/4];
  /* 0x3C00 */ volatile unsigned int moduleID0;          /* Device 1,  Address 0x0 */
  /* 0x3C04 */ volatile unsigned int moduleID1;                     /* Address 0x1 */
  /* 0x3C08 */ volatile unsigned int version;                       /* Address 0x2 */
  /* 0x3C0C */ volatile unsigned int alarms;                        /* Address 0x3 */
  /* 0x3C10 */ volatile unsigned int alarmsMask;                    /* Address 0x4 */
  /* 0x3C14 */ volatile unsigned int temperature;                   /* Address 0x5 */
  /* 0x3C18 */ volatile unsigned int clock;                         /* Address 0x6 */
  /* 0x3C1C */ volatile unsigned int payload;                       /* Address 0x7 */
  /* 0x3C20 */ volatile unsigned int payloadLink;                   /* Address 0x8 */
  /* 0x3C24 */ volatile unsigned int receiveError;                  /* Address 0x9 */
  /* 0x3C28 */ volatile unsigned int fiberStatus0;                  /* Address 0xA */
  /* 0x3C2C */ volatile unsigned int fiberStatus1;                  /* Address 0xB */
  /* 0x3C30 */ volatile unsigned int blankGTP1[(0x3C40-0x3C30)/4];  /* Addresses 0xC-0xF */
  /* 0x3C40 */ volatile unsigned int status;                        /* Address 0x10 */
  /* 0x3C44 */ volatile unsigned int ipAddress0;                    /* Address 0x11 */
  /* 0x3C48 */ volatile unsigned int ipAddress1;                    /* Address 0x12 */
  /* 0x3C4C */ volatile unsigned int subnetMask0;                   /* Address 0x13 */
  /* 0x3C50 */ volatile unsigned int subnetMask1;                   /* Address 0x14 */
  /* 0x3C54 */ volatile unsigned int gateway0;                      /* Address 0x15 */
  /* 0x3C58 */ volatile unsigned int gateway1;                      /* Address 0x16 */
  /* 0x3C5C */ volatile unsigned int macAddress0;                   /* Address 0x17 */
  /* 0x3C60 */ volatile unsigned int macAddress1;                   /* Address 0x18 */
  /* 0x3C64 */ volatile unsigned int macAddress2;                   /* Address 0x19 */
  /* 0x3C68 */ volatile unsigned int blankGTP2[(0x10000-0x3C68)/4]; /* Address 0x1A */
};

/* Module ID = GTP! */
#define GTP_MODULEID    0x47545021

#define GTP_VERSION_PROJECT_MASK  0xF000
#define GTP_VERSION_MAJOR_MASK    0x0F80
#define GTP_VERSION_MINOR_MASK    0x007E
#define GTP_VERSION_BETA_MASK     0x0001

#define GTP_ALARMS_CLOCK_ERROR    (1<<15)
#define GTP_ALARMS_TEMP_HIGH      (1<<14)
#define GTP_ALARMS_PP_LINKDOWN    (1<<13)
#define GTP_ALARMS_PP_RX_ERROR    (1<<12)

#define GTP_TEMPERATURE_THRESHOLD_MASK    0x7F
#define GTP_TEMPERATURE_MASK              0x7F00

#define GTP_CLOCK_STATUS_MASK     (1<<8)

#define GTP_FIBERSTATUS0_MODULE_DETECT (1<<8)

/* GTP Function Prototypes */
int  gtpInit();
int  gtpSetIPAddress(char *ipaddr);
int  gtpGetIPAddress(char *ipaddr);
int  gtpSetSubnetMask(char *ipaddr);
int  gtpGetSubnetMask(char *ipaddr);
int  gtpSetGateway(char *ipaddr);
int  gtpGetGateway(char *ipaddr);
int  gtpGetMACAddress(char *macaddr);
int  gtpStatus();
int  gtpSetPayloadEnableMask(unsigned int enableMask);
int  gtpGetPayloadEnableMask();
int  gtpSetClockSource(int source);
int  gtpGetClockStatus();
int  gtpSetFiberTX(int flag);
int  gtpGetFiberStatus();
int  gtpSetTemperatureAlarmThreshold(int threshold);
int  gtpGetTemperature();
int  gtpSetAlarmsMask(int alarmsmask);
int  gtpGetAlarms(int pflag);

#endif /* GTPLIB_H */
