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
 *     Firmware update for the Signal Distribution (SD) module.
 *
 *----------------------------------------------------------------------------*/

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "unistd.h"
#ifdef VXWORKS
#include "vxCompat.h"
#else
#include "jvme.h"
#endif
#include "tiLib.h"

void Usage();
char bin_name[50];

#ifdef VXWORKS
int
sdFirmwareUpdate(char *arg_filename, unsigned int arg_serialnumber,
		 unsigned int arg_hall_board_version, unsigned int arg_firmware_version)
#else
int
main(int argc, char *argv[])
#endif
{
  int res=0;
  char firmware_filename[50];
#ifdef WRITESERIALNUMBER
  unsigned int serial_number=0;
  unsigned int hall_board_version=0;
  unsigned int firmware_version=0;
#endif
  int inputchar=10;

  printf("\nJLAB Signal Distribution (SD) Firmware Update\n");
  printf("------------------------------------------------\n");

#ifdef VXWORKS
  strncpy(bin_name,__FUNCTION__,50);
#else
  strncpy(bin_name,argv[0],50);

#ifdef WRITESERIALNUMBER
  if(argc<5)
    {
      printf(" ERROR: Must specify four (4) arguments\n");
      Usage();
      exit(-1);
    }
  else
    {
      strncpy(firmware_filename,argv[1],50);
      serial_number = (unsigned int) strtoll(argv[2],NULL,10)&0xffffffff;
      hall_board_version = (unsigned int) strtoll(argv[3],NULL,16)&0xffffffff;
      firmware_version = (unsigned int) strtoll(argv[4],NULL,16)&0xffffffff;
    }
#else
  if(argc<2)
    {
      printf(" ERROR: Must specify two (2) arguments\n");
      Usage();
      exit(-1);
    }
  else
    {
      strncpy(firmware_filename,argv[1],50);
    }
#endif
#endif // VXWORKS

#ifdef WRITESERIALNUMBER
  /* Check on provided items */
  if(serial_number<1 || serial_number>255)
    {
      printf(" ERROR: Invalid Serial Number (%d).  Must be 1-255\n",serial_number);
      exit(-1);
    }

  if(hall_board_version<0x1 || hall_board_version>0xFF)
    {
      printf(" ERROR: Invalid Assigned Hall and Board Version (0x%x).\n  Must be 0x01-0xFF\n"
	     ,hall_board_version);
      exit(-1);
    }

  if(firmware_version<0x1 || firmware_version>0xFF)
    {
      printf(" ERROR: Firmware Version (0x%x).  Must be 0x01-0xFF\n",firmware_version);
      exit(-1);
    }

  printf("Firmware File                   = %s\n",firmware_filename);
  printf("Serial Number (dec)             = %4d\n",serial_number);
  printf("Assigned Hall and Board Version = 0x%02X\n",hall_board_version);
  printf("Firmware Version                = 0x%02X\n",firmware_version);
  printf("\n");

  printf(" Please verify these items before continuing... \n");
#else
  printf("Firmware File                   = %s\n",firmware_filename);
#endif
  printf("\n");
  printf(" <ENTER> to continue... or q and <ENTER> to quit without update\n");

  inputchar = getchar();

  if((inputchar == 113) ||
     (inputchar == 81))
    {
      printf(" Exiting without update\n");
      res=1;
      goto CLOSE;
    }

#ifndef VXWORKS
  vmeSetQuietFlag(1);
  res = vmeOpenDefaultWindows();
  if(res!=OK)
    goto CLOSE;
#endif

  res = sdFirmwareLoadFile(firmware_filename);
  if(res!=OK)
    goto CLOSE;

  res = tiInit(21<<19,0,1);
  if(res!=OK)
    goto CLOSE;
  
  res = sdInit();
  if(res!=OK)
    goto CLOSE;

  res = sdFirmwareFlushFifo();
  if(res!=OK)
    goto CLOSE;

  res = sdFirmwareWriteToMemory();
  if(res!=OK)
    goto CLOSE;

  goto CLOSE;

/*   res = sdFirmwareVerifyMemory(); */
/*   if(res!=OK) */
/*     goto CLOSE; */

  sdFirmwareFlushFifo();
#ifdef WRITESERIALNUMBER
  sdFirmwareWriteSpecs(0x7F0000,serial_number,hall_board_version,firmware_version);
  sleep(3);
  sdFirmwarePrintSpecs();
#endif

 CLOSE:

  sdFirmwareFreeMemory();
#ifndef VXWORKS
  vmeCloseDefaultWindows();
#endif

  printf("\n");
  if(res==ERROR)
    printf(" ******** SD Update ended in ERROR! ******** \n");
  else if (res==OK)
    printf(" ++++++++ SD Update Successful! ++++++++\n");
  

  printf("\n");
  return OK;
}

void
Usage()
{
  printf("\n");
#ifdef WRITESERIALNUMBER
  printf("%s <firmware rbf file> <serial number (dec)> \\\n\t <assigned Hall & Version (hex)> <Firmware Version (hex)>\n",
	 bin_name);
  printf("\n");
  printf("  For Example:\n");
  printf("%s SD_A4_Production.rbf 8 B4 A4\n",bin_name);
  printf("  ... OR ...\n");
  printf("%s SD_A4_Production.rbf 8 0xB4 0xA4\n",bin_name);
  printf("\n");
#else
  printf("%s <firmware rbf file>\n",
	 bin_name);
  printf("\n");
  printf("  For Example:\n");
  printf("%s SD_A4_Production.rbf\n",bin_name);
  printf("\n");

#endif

}
