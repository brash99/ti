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
 *   This file is "included" in the tidLib.c
 *
 * SVN: $Rev$
 *
 *----------------------------------------------------------------------------*/

#include <arpa/inet.h>

/* This is the GTP base relative to the TI base VME address */
#define GTPBASE 0x30000 

/* Global Variables */
volatile struct GTPStruct  *GTPp=NULL;    /* pointer to GTP memory map */

/*
  gtpInit
  - Initialize the Global Trigger Processor module
*/
int
gtpInit()
{
  unsigned long tiBase=0, gtpBase=0;
  unsigned int modID=0, version=0;

  if(TIp==NULL)
    {
      printf("%s: ERROR: TI not initialized\n",__FUNCTION__);
      return ERROR;
    }

  /* Verify that the ctp registers are in the correct space for the TI I2C */
  tiBase = (unsigned long)TIp;
  gtpBase = (unsigned long)&(TIp->SWA[0]);
  
  if( (gtpBase-tiBase) != GTPBASE)
    {
      printf("%s: ERROR: GTP memory structure not in correct VME Space!\n",
	     __FUNCTION__);
      printf("   current base = 0x%lx   expected base = 0x%lx\n",
	     gtpBase-tiBase, (unsigned long)GTPBASE);
      return ERROR;
    }

  GTPp = (struct GTPStruct *)(&TIp->SWA[0]);

  /* Check the module ID, to confirm we've got a GTP in there */
  TILOCK;
  modID = 
    (vmeRead32(&GTPp->moduleID1)<<16) |
    (vmeRead32(&GTPp->moduleID0)&0xFFFF);
    
  version = vmeRead32(&GTPp->version);
  TIUNLOCK;

  if(modID != (GTP_MODULEID))
    {
      printf("%s: ERROR: Invalid GTP Module ID (0x%08x)\n",
	     __FUNCTION__,modID);
      return ERROR;
    }

  if(version == 0xffff)
    {
      printf("%s: ERROR: Unable to read GTP version (returned 0x%x)\n",
	     __FUNCTION__,version);
      return ERROR;
    }

  if(version & GTP_VERSION_BETA_MASK)
    {
      printf("%s: GTP (BETA version %d %d.%d) initialized at Local Base address 0x%lx\n",
	     __FUNCTION__,
	     (version & GTP_VERSION_PROJECT_MASK)>>12,
	     (version & GTP_VERSION_MAJOR_MASK)>>7,
	     (version & GTP_VERSION_MINOR_MASK)>>1,
	     gtpBase);
    }
  else
    {
      printf("%s: GTP (version %d %d.%d) initialized at Local Base address 0x%lx\n",
	     __FUNCTION__,
	     (version & GTP_VERSION_PROJECT_MASK)>>12,
	     (version & GTP_VERSION_MAJOR_MASK)>>7,
	     (version & GTP_VERSION_MINOR_MASK)>>1,
	     gtpBase);
    }

  return OK;
}

int
gtpSetIPAddress(char *ipaddr)
{
  struct in_addr addr;

  if(GTPp==NULL)
    {
      printf("%s: ERROR: GTP not initialized\n",__FUNCTION__);
      return ERROR;
    }

  if (inet_aton(ipaddr, &addr) == 0) 
    {
      perror("inet_aton");
      return ERROR;
    }

#ifdef DEBUG
  printf("%s\n", inet_ntoa(addr));
  
  printf("%d.%d.%d.%d\n",
	 addr.s_addr&0xff,
	 (addr.s_addr&0xff00)>>8,
	 (addr.s_addr&0xff0000)>>16,
	 (addr.s_addr&0xff000000)>>24
	 );
#endif

  TILOCK;
  vmeWrite32(&GTPp->ipAddress0, ((addr.s_addr&0xff)<<8) | (addr.s_addr&0xff00));
  vmeWrite32(&GTPp->ipAddress1, ((addr.s_addr&0xff0000)<<8) | (addr.s_addr&0xff000000));
  TIUNLOCK;

  return OK;
}

int
gtpGetIPAddress(char *ipaddr)
{
/*   struct in_addr addr; */
  unsigned int ipAddress0, ipAddress1;

  if(GTPp==NULL)
    {
      printf("%s: ERROR: GTP not initialized\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  ipAddress0 = vmeRead32(&GTPp->ipAddress0);
  ipAddress1 = vmeRead32(&GTPp->ipAddress1);
  TIUNLOCK;

  sprintf(ipaddr,"%d.%d.%d.%d",
	  (ipAddress0&0xff00)>>8, 
	  (ipAddress0&0xff), 
	  (ipAddress1*0xff00)>>8,
	  (ipAddress1&0xff));
  
#ifdef DEBUG
  printf("%s: IP address = %s\n",
	 __FUNCTION__,ipaddr);
#endif

  return OK;
}

int
gtpSetSubnetMask(char *ipaddr)
{
  struct in_addr addr;

  if(GTPp==NULL)
    {
      printf("%s: ERROR: GTP not initialized\n",__FUNCTION__);
      return ERROR;
    }

  if (inet_aton(ipaddr, &addr) == 0) 
    {
      perror("inet_aton");
      return ERROR;
    }

#ifdef DEBUG
  printf("%s\n", inet_ntoa(addr));
  
  printf("%d.%d.%d.%d\n",
	 addr.s_addr&0xff,
	 (addr.s_addr&0xff00)>>8,
	 (addr.s_addr&0xff0000)>>16,
	 (addr.s_addr&0xff000000)>>24
	 );
#endif

  TILOCK;
  vmeWrite32(&GTPp->subnetMask0, ((addr.s_addr&0xff)<<8) | (addr.s_addr&0xff00));
  vmeWrite32(&GTPp->subnetMask1, ((addr.s_addr&0xff0000)<<8) | (addr.s_addr&0xff000000));
  TIUNLOCK;

  return OK;
}

int
gtpGetSubnetMask(char *ipaddr)
{
/*   struct in_addr addr; */
  unsigned int subnetMask0, subnetMask1;

  if(GTPp==NULL)
    {
      printf("%s: ERROR: GTP not initialized\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  subnetMask0 = vmeRead32(&GTPp->subnetMask0);
  subnetMask1 = vmeRead32(&GTPp->subnetMask1);
  TIUNLOCK;

  sprintf(ipaddr,"%d.%d.%d.%d",
	  (subnetMask0&0xff00)>>8, 
	  (subnetMask0&0xff), 
	  (subnetMask1*0xff00)>>8,
	  (subnetMask1&0xff));
  
#ifdef DEBUG
  printf("%s: IP address = %s\n",
	 __FUNCTION__,ipaddr);
#endif

  return OK;
}

int
gtpSetGateway(char *ipaddr)
{
  struct in_addr addr;

  if(GTPp==NULL)
    {
      printf("%s: ERROR: GTP not initialized\n",__FUNCTION__);
      return ERROR;
    }

  if (inet_aton(ipaddr, &addr) == 0) 
    {
      perror("inet_aton");
      return ERROR;
    }

#ifdef DEBUG
  printf("%s\n", inet_ntoa(addr));
  
  printf("%d.%d.%d.%d\n",
	 addr.s_addr&0xff,
	 (addr.s_addr&0xff00)>>8,
	 (addr.s_addr&0xff0000)>>16,
	 (addr.s_addr&0xff000000)>>24
	 );
#endif

  TILOCK;
  vmeWrite32(&GTPp->gateway0, ((addr.s_addr&0xff)<<8) | (addr.s_addr&0xff00));
  vmeWrite32(&GTPp->gateway1, ((addr.s_addr&0xff0000)<<8) | (addr.s_addr&0xff000000));
  TIUNLOCK;

  return OK;
}

int
gtpGetGateway(char *ipaddr)
{
/*   struct in_addr addr; */
  unsigned int gateway0, gateway1;

  if(GTPp==NULL)
    {
      printf("%s: ERROR: GTP not initialized\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  gateway0 = vmeRead32(&GTPp->gateway0);
  gateway1 = vmeRead32(&GTPp->gateway1);
  TIUNLOCK;

  sprintf(ipaddr,"%d.%d.%d.%d",
	  (gateway0&0xff00)>>8, 
	  (gateway0&0xff), 
	  (gateway1*0xff00)>>8,
	  (gateway1&0xff));
  
#ifdef DEBUG
  printf("%s: IP address = %s\n",
	 __FUNCTION__,ipaddr);
#endif

  return OK;
}

int
gtpGetMACAddress(char *macaddr)
{
/*   struct in_addr addr; */
  unsigned int macAddress0, macAddress1, macAddress2;

  if(GTPp==NULL)
    {
      printf("%s: ERROR: GTP not initialized\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  macAddress0 = vmeRead32(&GTPp->macAddress0);
  macAddress1 = vmeRead32(&GTPp->macAddress1);
  macAddress2 = vmeRead32(&GTPp->macAddress2);
  TIUNLOCK;

  sprintf(macaddr,"%02x:%02x:%02x:%02x:%02x:%02x",
	  (macAddress0&0xff00)>>8, 
	  (macAddress0&0xff), 
	  (macAddress1*0xff00)>>8,
	  (macAddress1&0xff),
	  (macAddress2*0xff00)>>8,
	  (macAddress2&0xff));
  
#ifdef DEBUG
  printf("%s: MAC address = %s\n",
	 __FUNCTION__,macaddr);
#endif

  return OK;
}

int
gtpStatus()
{
  unsigned int version, alarms, alarmsMask, temperature, clock;
  unsigned int payload, payloadLink, receiveError, fiberStatus0, fiberStatus1;
  unsigned int status;
  char ipaddress[20], subnetmask[20], gateway[20];
  char macaddress[20];

  if(GTPp==NULL)
    {
      printf("%s: ERROR: GTP not initialized\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  version      = vmeRead32(&GTPp->version);
  alarms       = vmeRead32(&GTPp->alarms);
  alarmsMask   = vmeRead32(&GTPp->alarmsMask);
  temperature  = vmeRead32(&GTPp->temperature);
  clock        = vmeRead32(&GTPp->clock);
  payload      = vmeRead32(&GTPp->payload);
  payloadLink  = vmeRead32(&GTPp->payloadLink);
  receiveError = vmeRead32(&GTPp->receiveError);
  fiberStatus0 = vmeRead32(&GTPp->fiberStatus0);
  fiberStatus1 = vmeRead32(&GTPp->fiberStatus1);
  status       = vmeRead32(&GTPp->status);
  TIUNLOCK;
  gtpGetIPAddress((char *)&ipaddress);
  gtpGetSubnetMask((char *)&subnetmask);
  gtpGetGateway((char *)&gateway);
  gtpGetMACAddress((char *)&macaddress);

  /* Now printout what we've got */
  printf("*** Global Trigger Processor Module Status ***\n");
  printf(" Raw Registers\n");
  printf("       version = 0x%04x         alarms = 0x%04x   alarmsMask = 0x%04x\n",
	 version, alarms, alarmsMask);
  printf("   temperature = 0x%04x          clock = 0x%04x\n",
	 temperature, clock);
  printf("   payloadLink = 0x%04x   receiveError = 0x%04x      payload = 0x%04x\n\n",
	 payloadLink, receiveError, payload);
  printf("  fiberStatus0 = 0x%04x   fiberStatus1 = 0x%04x       status = 0x%04x\n",
	 fiberStatus0, fiberStatus1, status);

  if(alarms)
    gtpGetAlarms(1);

  printf(" Network Settings\n");
  printf(" MAC Address: %s",macaddress);
  printf(" IP: %s    Subnet Mask: %s   Gateway: %s",
	 ipaddress, subnetmask, gateway);

  return OK;
}

int
gtpSetPayloadEnableMask(unsigned int enableMask)
{
  if(GTPp==NULL)
    {
      printf("%s: ERROR: GTP not initialized\n",__FUNCTION__);
      return ERROR;
    }

  if( enableMask >= (1<<(17-1)) ) /* 16 = Maximum Payload port number */
    {
      printf("%s: ERROR: Invalid enableMask (0x%x).  Includes payload port > 16.\n",
	     __FUNCTION__,enableMask);
      return ERROR;
    }
  
  TILOCK;
  vmeWrite32(&GTPp->payload, enableMask);
  TIUNLOCK;
  
  return OK;
}


int
gtpGetPayloadEnableMask()
{
  unsigned int rval=0;

  if(GTPp==NULL)
    {
      printf("%s: ERROR: GTP not initialized\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  rval = vmeRead32(&GTPp->payload);
  TIUNLOCK;
  
  return rval;
}

int
gtpSetClockSource(int source)
{
  if(GTPp==NULL)
    {
      printf("%s: ERROR: GTP not initialized\n",__FUNCTION__);
      return ERROR;
    }

  if( source>3 ) 
    {
      printf("%s: ERROR: Invalid clock source (%d).  Must be less than 3.\n",
	     __FUNCTION__,source);
      return ERROR;
    }

  
  
  TILOCK;
  vmeWrite32(&GTPp->clock, source);
  TIUNLOCK;
  
  return OK;

}

int
gtpGetClockStatus()
{
  unsigned int rval=0;

  if(GTPp==NULL)
    {
      printf("%s: ERROR: GTP not initialized\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  rval = vmeRead32(&GTPp->clock)&GTP_CLOCK_STATUS_MASK;
  TIUNLOCK;

  if(rval)
    rval=1;  /* Just return 1, if clock present */
  
  return rval;
}

int
gtpSetFiberTX(int flag)
{
  if(GTPp==NULL)
    {
      printf("%s: ERROR: GTP not initialized\n",__FUNCTION__);
      return ERROR;
    }

  if(flag) 
    flag=1;

  
  
  TILOCK;
  vmeWrite32(&GTPp->fiberStatus0, flag);
  TIUNLOCK;
  
  return OK;
}

int
gtpGetFiberStatus()
{
  unsigned int rval=0;

  if(GTPp==NULL)
    {
      printf("%s: ERROR: GTP not initialized\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  rval = vmeRead32(&GTPp->fiberStatus0)&GTP_FIBERSTATUS0_MODULE_DETECT;
  TIUNLOCK;

  if(rval)
    rval=1;  /* Just return 1, if module detected */
  
  return rval;
}

int
gtpSetTemperatureAlarmThreshold(int threshold)
{
  if(GTPp==NULL)
    {
      printf("%s: ERROR: GTP not initialized\n",__FUNCTION__);
      return ERROR;
    }

  if(threshold>GTP_TEMPERATURE_THRESHOLD_MASK) 
    {
      printf("%s: ERROR: Invalid Threshold (%d).  Must be less than %d",
	     __FUNCTION__, threshold, GTP_TEMPERATURE_THRESHOLD_MASK);
      return ERROR;
    }

  
  
  TILOCK;
  vmeWrite32(&GTPp->temperature, threshold);
  TIUNLOCK;
  
  return OK;
}

int
gtpGetTemperature()
{
  unsigned int rval=0;

  if(GTPp==NULL)
    {
      printf("%s: ERROR: GTP not initialized\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  rval = (vmeRead32(&GTPp->temperature)&GTP_TEMPERATURE_MASK)>>8;
  TIUNLOCK;

  return rval;
}

int
gtpSetAlarmsMask(int alarmsmask)
{
  if(GTPp==NULL)
    {
      printf("%s: ERROR: GTP not initialized\n",__FUNCTION__);
      return ERROR;
    }

  if(alarmsmask>0xFFFF)
    {
      printf("%s: ERROR: Invalid Alarms Mask (0x%x).  Must be less than 0xFFFF",
	     __FUNCTION__, alarmsmask);
      return ERROR;
    }

  TILOCK;
  vmeWrite32(&GTPp->alarmsMask, alarmsmask);
  TIUNLOCK;
  
  return OK;
}

int
gtpGetAlarms(int pflag)
{
  unsigned int rval=0;

  if(GTPp==NULL)
    {
      printf("%s: ERROR: GTP not initialized\n",__FUNCTION__);
      return ERROR;
    }

  TILOCK;
  rval = vmeRead32(&GTPp->alarms);
  TIUNLOCK;

  if(pflag)
    {
      if(rval & GTP_ALARMS_CLOCK_ERROR)
	{
	  printf("%s: ALARM: 250 CLOCK ERROR\n",__FUNCTION__);
	}
      if(rval & GTP_ALARMS_TEMP_HIGH)
	{
	  printf("%s: ALARM: TEMPERATURE HIGH\n",__FUNCTION__);
	}
      if(rval & GTP_ALARMS_PP_LINKDOWN)
	{
	  printf("%s: ALARM: PAYLOAD PORT LINK DOWN\n",__FUNCTION__);
	}
      if(rval & GTP_ALARMS_PP_RX_ERROR)
	{
	  printf("%s: ALARM: PAYLOAD PORT RECEIVE ERROR\n",__FUNCTION__);
	}
    }

  return rval;
}
