
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "jvme.h"
#include "tiLib.h"

void Usage();
char bin_name[50];

int
main(int argc, char *argv[])
{
  int res;
  char firmware_filename[50];
  unsigned int serial_number=0;
  unsigned int hall_board_version=0;
  unsigned int firmware_version=0;
  int inputchar=10;

  printf("\nJLAB Signal Distribution (SD) Firmware Update CHECK\n");
  printf("------------------------------------------------\n");

  strncpy(bin_name,argv[0],50);

/*   if(argc<2) */
/*     { */
/*       printf(" ERROR: Must specify one (1) argument\n"); */
/*       Usage(); */
/*       exit(-1); */
/*     } */
/*   else */
/*     { */
/*       strncpy(firmware_filename,argv[1],50); */
/* /\*       serial_number = (unsigned int) strtoll(argv[2],NULL,10)&0xffffffff; *\/ */
/* /\*       hall_board_version = (unsigned int) strtoll(argv[3],NULL,16)&0xffffffff; *\/ */
/* /\*       firmware_version = (unsigned int) strtoll(argv[4],NULL,16)&0xffffffff; *\/ */
/*     } */

  /* Check on provided items */
/*   if(serial_number<1 || serial_number>255) */
/*     { */
/*       printf(" ERROR: Invalid Serial Number (%d).  Must be 1-255\n",serial_number); */
/*       exit(-1); */
/*     } */

/*   if(hall_board_version<0x1 || hall_board_version>0xFF) */
/*     { */
/*       printf(" ERROR: Invalid Assigned Hall and Board Version (0x%x).\n  Must be 0x01-0xFF\n" */
/* 	     ,hall_board_version); */
/*       exit(-1); */
/*     } */

/*   if(firmware_version<0x1 || firmware_version>0xFF) */
/*     { */
/*       printf(" ERROR: Firmware Version (0x%x).  Must be 0x01-0xFF\n",firmware_version); */
/*       exit(-1); */
/*     } */

/*   printf("Firmware File                   = %s\n",firmware_filename); */
/* /\*   printf("Serial Number (dec)             = %4d\n",serial_number); *\/ */
/* /\*   printf("Assigned Hall and Board Version = 0x%02X\n",hall_board_version); *\/ */
/* /\*   printf("Firmware Version                = 0x%02X\n",firmware_version); *\/ */
/*   printf("\n"); */

/*   printf(" Please verify these items before continuing... \n"); */
/*   printf("\n"); */
/*   printf(" <ENTER> to continue... or q and <ENTER> to quit without update\n"); */

/*   inputchar = getchar(); */

/*   if((inputchar == 113) || */
/*      (inputchar == 81)) */
/*     { */
/*       printf(" Exitting without update\n"); */
/*       goto CLOSE; */
/*     } */

  vmeSetQuietFlag(1);
  res = vmeOpenDefaultWindows();
  if(res!=OK)
    goto CLOSE;

/*   res = sdFirmwareLoadFile(firmware_filename); */
/*   if(res!=OK) */
/*     goto CLOSE; */

  res = tiInit(21<<19,0,1);
  if(res!=OK)
    goto CLOSE;
  
  res = sdInit();
  if(res!=OK)
    goto CLOSE;

/*   res = sdFirmwareFlushFifo(); */
/*   if(res!=OK) */
/*     goto CLOSE; */

/*   res = sdFirmwareWriteToMemory(); */
/*   if(res!=OK) */
/*     goto CLOSE; */


/*   res = sdFirmwareVerifyMemory(); */
/*   if(res!=OK) */
/*     goto CLOSE; */

  sdFirmwareFlushFifo();
/*   sdFirmwareWriteSpecs(0xfc0000,serial_number,hall_board_version,firmware_version); */
  sleep(1);
  sdFirmwarePrintSpecs();

 CLOSE:

/*   sdFirmwareFreeMemory(); */
  vmeCloseDefaultWindows();

  printf("\n");
  if(res!=OK)
    printf(" ******** SD Update Check ended in ERROR! ******** \n");
  else
    printf(" ++++++++ SD Update Check Successful! ++++++++\n");

  printf("\n");
  return OK;
}

void
Usage()
{
  printf("\n");
  printf("%s\n",
	 bin_name);
/*   printf("%s <firmware rbf file> \n", */
/* 	 bin_name); */
  printf("\n");

}
