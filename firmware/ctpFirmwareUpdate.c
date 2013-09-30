/*----------------------------------------------------------------------------*
 *  Copyright (c) 2013        Southeastern Universities Research Association, *
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
 *     Firmware update for the Crate Trigger Processor (CTP) module.
 *
 *----------------------------------------------------------------------------*/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef VXWORKS
#include "vxCompat.h"
#else
#include "jvme.h"
#endif
#include "tiLib.h"


char *programName;

#ifndef VXWORKS
static void tiFirmwareUsage();
#endif

int
#ifdef VXWORKS
ctpFirmwareUpdate(unsigned int arg_vmeAddr, char *arg_filename, arg_ifpga)
#else
main(int argc, char *argv[])
#endif
{
  int stat;
  int ifpga;
  char *filename;
  int inputchar=10;
  unsigned int vme_addr=0;
  int reboot=0;
  
  printf("\nTI firmware update via VME\n");
  printf("----------------------------\n");

#ifdef VXWORKS
  programName = __FUNCTION__;

  vme_addr = arg_vmeAddr;
  filename = arg_filename;
  ifpga    = arg_ifpga;
#else
  programName = argv[0];

  if(argc<4)
    {
      printf(" ERROR: Must specify three arguments\n");
      tiFirmwareUsage();
      return(-1);
    }
  else
    {
      vme_addr = (unsigned int) strtoll(argv[1],NULL,16)&0xffffffff;
      filename = argv[2];
      ifpga = (unsigned int) strtoll(argv[3],NULL,10);
    }

  vmeSetQuietFlag(1);
  stat = vmeOpenDefaultWindows();
  if(stat != OK)
    goto CLOSE;
#endif

  stat = tiInit(vme_addr,0,1);
  if(stat != OK)
    {
      printf("\n");
      printf("*** Failed to initialize TI ***\n");
      goto CLOSE;
    }

  stat = ctpInit();
  if(stat != OK)
    {
      printf("\n");
      printf("*** Failed to initialize CTP ***\n");
      goto CLOSE;
    }

  stat = ctpFirmwareUpload(filename,ifpga,reboot);
  if(stat != OK)
    {
      printf("\n");
      printf("*** CTP Firmware Update Failed ***\n");
      goto CLOSE;
    }
  else
    {
      printf("\n");
      printf("--- CTP Firmware Update Succeeded ---\n");
    }
 CLOSE:

#ifndef VXWORKS
  vmeCloseDefaultWindows();
#endif
  printf("\n");

  return OK;
}

#ifndef VXWORKS
static void
tiFirmwareUsage()
{
  printf("\n");
  printf("%s <TI VME Address (A24)> <firmware mcs file> <FPGA number>\n",programName);
  printf("\n");

}
#endif
