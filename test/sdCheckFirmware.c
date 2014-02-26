
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
  vmeSetQuietFlag(1);
  res = vmeOpenDefaultWindows();
  if(res!=OK)
    goto CLOSE;

  res = tiInit(21<<19,0,1);
  if(res!=OK)
    goto CLOSE;
  
  res = sdInit(0);
  if(res!=OK)
    goto CLOSE;

  sdReset();

  sdSetActivePayloadPorts(0xffff);

/*   sdSetPLLClockFrequency(2, 3); */
  sdStatus(1);
  sdPrintBusyoutCounters();
  sdPrintTrigoutCounters();

  sdFirmwareFlushFifo();
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
