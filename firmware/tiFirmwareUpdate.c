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
 *     Firmware update for the Pipeline Trigger Interface (TI) module.
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


#ifdef VXWORKS
extern  int sysBusToLocalAdrs(int, char *, char **);
extern STATUS vxMemProbe(char *, int, int, char*);
extern UINT32 sysTimeBaseLGet();
extern STATUS taskDelay(int);
#ifdef TEMPE
extern unsigned int sysTempeSetAM(unsigned int, unsigned int);
#else
extern unsigned int sysUnivSetUserAM(int, int);
extern unsigned int sysUnivSetLSI(unsigned short, unsigned short);
#endif /*TEMPE*/
#endif


extern volatile struct TI_A24RegStruct *TIp;
unsigned int BoardSerialNumber = 0;
unsigned int firmwareInfo;
char *programName;

int tiMasterID(int sn);
void tiFirmwareEMload(char *filename);
#ifndef VXWORKS
static void tiFirmwareUsage();
#endif

int
#ifdef VXWORKS
tiFirmwareUpdate(unsigned int arg_vmeAddr, char *arg_filename)
#else
main(int argc, char *argv[])
#endif
{
  int stat = 0, badInit = 0;
  int BoardNumber;
  char *filename;
  int inputchar=10;
  unsigned int vme_addr=0;
  unsigned long laddr=0;
  int geo = 0;

  printf("\nTI firmware update via VME\n");
  printf("----------------------------\n");

#ifdef VXWORKS
  programName = __FUNCTION__;

  vme_addr = arg_vmeAddr;
  filename = arg_filename;
#else
  programName = argv[0];

  if(argc<3)
    {
      printf(" ERROR: Must specify two arguments\n");
      tiFirmwareUsage();
      return(-1);
    }
  else
    {
      vme_addr = (unsigned int) strtoll(argv[1],NULL,16)&0xffffffff;
      if(vme_addr <= 21) vme_addr = vme_addr << 19;
      filename = argv[2];
    }

  vmeSetQuietFlag(1);
  stat = vmeOpenDefaultWindows();
  if(stat != OK)
    goto CLOSE;
#endif

  stat = tiInit(vme_addr,TI_READOUT_EXT_POLL,TI_INIT_SKIP_FIRMWARE_CHECK | TI_INIT_NO_INIT);
  if(stat != OK)
    {
      printf("\n");
      printf("*** Failed to initialize TI ***\nThis may indicate (either):\n");
      printf("   a) an incorrect VME Address provided\n");
      printf("   b) TI is unresponsive and needs firmware reloaded\n");
      printf("\n");
      printf("Proceed with the update with the provided VME address (0x%x)?\n", vme_addr);
    REPEAT:
      printf(" (y/n): ");
      inputchar = getchar();

      if((inputchar == 'n') || (inputchar == 'N'))
	{
	  printf("--- Exiting without update ---\n");
	  goto CLOSE;
	}
      else if((inputchar == 'y') || (inputchar == 'Y'))
	{
	  printf("--- Continuing update, assuming VME address (0x%x) is correct ---\n", vme_addr);
	  printf("\n");
	  badInit = 1;
	}
      else
	{
	  goto REPEAT;
	}
    }

  if(badInit == 0)
    {
      /* Read out the board serial number first */
      BoardSerialNumber = tiGetSerialNumber(NULL);

      /* Check if this board should be relabled as a TIMaster */
      if( ((BoardSerialNumber&0xF800)==0) && (tiMasterID(BoardSerialNumber)!=0) )
	{
	  BoardSerialNumber |= tiMasterID(BoardSerialNumber);
	}

      if(BoardSerialNumber & 0xF800) /* TIMaster */
	{
	  printf("\n Board Serial Number from PROM usercode is: 0x%08x (TIM-%d  TI-%d) \n",
		 BoardSerialNumber,
		 (BoardSerialNumber&0xF000)>>12,
		 BoardSerialNumber&0x7FF);
	}
      else
	{
	  printf("\n Board Serial Number from PROM usercode is: 0x%08x (%d) \n", BoardSerialNumber,
		 BoardSerialNumber&0xffff);
	}

      firmwareInfo = tiGetFirmwareVersion();
      if(firmwareInfo>0)
	{
	  printf("\n  User ID: 0x%x \tFirmware (version - revision): 0x%X - 0x%03X\n",
		 (firmwareInfo&0xFFFF0000)>>16, (firmwareInfo&0xF000)>>12, firmwareInfo&0xFFF);
	  printf("\n");
	}
      else
	{
	  printf("  Error reading Firmware Version\n");
	}
    }
  else
    {
      BoardSerialNumber = 0;
    }


  /* Check the serial number and ask for input if necessary */
  /* Force this program to only work for TI (not TD or TS) */
  if (!((BoardSerialNumber&0xffff0000) == 0x71000000))
    {
      printf(" This TI has an invalid serial number (0x%08x)\n",BoardSerialNumber);
      printf("\n");
      printf (" Enter a new board number (0-4095), or -1 to quit: ");

      scanf("%d",&BoardNumber);

      if(BoardNumber == -1)
	{
	  printf("--- Exiting without update ---\n");
	  goto CLOSE;
	}

      /* Add the TI board ID in the MSB */
      BoardSerialNumber = 0x71000000 | (BoardNumber&0x7ff) | (tiMasterID(BoardNumber) & 0xF800);
      if(BoardSerialNumber & 0xF800)
	{ /* TIMaster */
	  printf(" The board serial number will be set to: 0x%08x (TIM-%d  TI-%d)\n",
		 BoardSerialNumber,
		 (BoardSerialNumber&0xF800)>>12,
		 BoardSerialNumber&0x7ff);
	}
      else
	{
	  printf(" The board serial number will be set to: 0x%08x (%d)\n",
		 BoardSerialNumber,
		 BoardSerialNumber&0x7ff);
	}
    }


  printf("Press y to load firmware (%s) to the TI via VME...\n",
	 filename);
  printf("\t or n to quit without update\n");

 REPEAT2:
  printf("(y/n): ");
  inputchar = getchar();

  if((inputchar == 'n') ||
     (inputchar == 'N'))
    {
      printf("--- Exiting without update ---\n");
      goto CLOSE;
    }
  else if((inputchar == 'y') ||
     (inputchar == 'Y'))
    {
    }
  else
    goto REPEAT2;

  /* Check to see if the TI is in a VME-64X crate or Trying to recover corrupted firmware */
  if(badInit == 0)
    geo = tiGetGeoAddress();
  else
    geo = -1;

  if(geo <= 0)
    {
      if(geo == 0)
	{
	  printf("  ...Detected non VME-64X crate...\n");

	  /* Need to reset the Address to 0 to communicate with the emergency loading AM */
	  vme_addr = 0;
	}

#ifdef VXWORKS
      stat = sysBusToLocalAdrs(0x39,(char *)vme_addr,(char **)&laddr);
      if (stat != 0)
	{
	  printf("%s: ERROR: Error in sysBusToLocalAdrs res=%d \n",__FUNCTION__,stat);
	  goto CLOSE;
	}
#else
      stat = vmeBusToLocalAdrs(0x39,(char *)(unsigned long)vme_addr,(char **)&laddr);
      if (stat != 0)
	{
	  printf("%s: ERROR: Error in vmeBusToLocalAdrs res=%d \n",__FUNCTION__,stat);
	  goto CLOSE;
	}
#endif
      TIp = (struct TI_A24RegStruct *)laddr;
    }


  tiFirmwareEMload(filename);

 CLOSE:

#ifndef VXWORKS
  vmeCloseDefaultWindows();
#endif
  printf("\n");

  return OK;
}

/* Routine to provide the serial number addition for TIs configured with
   more than 2 optical transceivers... so-called TIMasters */
int
tiMasterID(int sn)
{
  int i=0;
  int rval=0;
  unsigned int themap[18][2] =
    {
      { 251, 0x5800},
      { 252, 0x6800},
      { 255, 0x7800},
      { 259, 0x8800},
      { 225, 0x1800},
      { 236, 0x2800},
      { 262, 0x3800},
      { 275, 0x4800},
      { 296, 0x1000},
      { 300, 0x2000},
      { 305, 0x3000},
      { 307, 0x4000},
      { 309, 0x5000},
      { 311, 0x6000},
      { 314, 0x7000},
      { 330, 0x8000},
      { 336, 0x9000},
      { 341, 0xa000}
    };

  for(i=0; i<18; i++)
    {
      if(themap[i][0] == sn)
	{
	  rval = themap[i][1];
	  break;
	}
    }

  return rval;
}

#ifdef VXWORKS
static void
cpuDelay(unsigned int delays)
{
  unsigned int time_0, time_1, time, diff;
  time_0 = sysTimeBaseLGet();
  do
    {
      time_1 = sysTimeBaseLGet();
      time = sysTimeBaseLGet();
#ifdef DEBUG
      printf("Time base: %x , next call: %x , second call: %x \n",time_0,time_1, time);
#endif
      diff = time-time_0;
    } while (diff <delays);
}
#endif

static int
Emergency(unsigned int jtagType, unsigned int numBits, unsigned int *jtagData)
{
/*   unsigned long *laddr; */
  unsigned int iloop, iword, ibit;
  unsigned int shData;
  int rval=OK;

/* #define DEBUG */
#ifdef DEBUG
  int numWord, i;
  printf("type: %x, num of Bits: %x, data: \n",jtagType, numBits);
  numWord = numBits ? ((numBits-1)/32 + 1 ) : 0;
  for (i=0; i<numWord; i++)
    {
      printf("%lx",jtagData[numWord-i-1]);
    }
  printf("\n");

  return OK;
#endif

  if (jtagType == 0) //JTAG reset, TMS high for 5 clcoks, and low for 1 clock;
    {
      for (iloop=0; iloop<5; iloop++)
	{
	  vmeWrite32(&TIp->eJTAGLoad,1);
	}

      vmeWrite32(&TIp->eJTAGLoad,0);
    }
  else if (jtagType == 1) // JTAG instruction shift
    {
      // Shift_IR header:
      vmeWrite32(&TIp->eJTAGLoad,0);
      vmeWrite32(&TIp->eJTAGLoad,1);
      vmeWrite32(&TIp->eJTAGLoad,1);
      vmeWrite32(&TIp->eJTAGLoad,0);
      vmeWrite32(&TIp->eJTAGLoad,0);

      for (iloop =0; iloop <numBits; iloop++)
	{
	  iword = iloop/32;
	  ibit = iloop%32;
	  shData = ((jtagData[iword] >> ibit )<<1) &0x2;
	  if (iloop == numBits -1) shData = shData +1;  //set the TMS high for last bit to exit Shift_IR
	  vmeWrite32(&TIp->eJTAGLoad, shData);
	}

      // shift _IR tail
      vmeWrite32(&TIp->eJTAGLoad,1);
      vmeWrite32(&TIp->eJTAGLoad,0);
    }
  else if (jtagType == 2)  // JTAG data shift
    {
      //shift_DR header
      vmeWrite32(&TIp->eJTAGLoad,0);
      vmeWrite32(&TIp->eJTAGLoad,1);
      vmeWrite32(&TIp->eJTAGLoad,0);
      vmeWrite32(&TIp->eJTAGLoad,0);

      for (iloop =0; iloop <numBits; iloop++)
	{
	  iword = iloop/32;
	  ibit = iloop%32;
	  shData = ((jtagData[iword] >> ibit )<<1) &0x2;
	  if (iloop == numBits -1) shData = shData +1;  //set the TMS high for last bit to exit Shift_DR
	  vmeWrite32(&TIp->eJTAGLoad, shData);
	}

      // shift _DR tail
      vmeWrite32(&TIp->eJTAGLoad,1);  // update Data_Register
      vmeWrite32(&TIp->eJTAGLoad,0);  // back to the Run_test/Idle
    }
  else if (jtagType == 3) // JTAG instruction shift, stop at IR-PAUSE state, though, it started from IDLE
    {
      // Shift_IR header:
      vmeWrite32(&TIp->eJTAGLoad,0);
      vmeWrite32(&TIp->eJTAGLoad,1);
      vmeWrite32(&TIp->eJTAGLoad,1);
      vmeWrite32(&TIp->eJTAGLoad,0);
      vmeWrite32(&TIp->eJTAGLoad,0);

      for (iloop =0; iloop <numBits; iloop++)
	{
	  iword = iloop/32;
	  ibit = iloop%32;
	  shData = ((jtagData[iword] >> ibit )<<1) &0x2;
	  if (iloop == numBits -1) shData = shData +1;  //set the TMS high for last bit to exit Shift_IR
	  vmeWrite32(&TIp->eJTAGLoad, shData);
	}

      // shift _IR tail
      vmeWrite32(&TIp->eJTAGLoad,0);  // update instruction register
      vmeWrite32(&TIp->eJTAGLoad,0);  // back to the Run_test/Idle
    }
  else if (jtagType == 4)  // JTAG data shift, start from IR-PAUSE, end at IDLE
    {
      //shift_DR header
      vmeWrite32(&TIp->eJTAGLoad,1);  //to EXIT2_IR
      vmeWrite32(&TIp->eJTAGLoad,1);  //to UPDATE_IR
      vmeWrite32(&TIp->eJTAGLoad,1);  //to SELECT-DR_SCAN
      vmeWrite32(&TIp->eJTAGLoad,0);
      vmeWrite32(&TIp->eJTAGLoad,0);

      for (iloop =0; iloop <numBits; iloop++)
	{
	  iword = iloop/32;
	  ibit = iloop%32;
	  shData = ((jtagData[iword] >> ibit )<<1) &0x2;
	  if (iloop == numBits -1) shData = shData +1;  //set the TMS high for last bit to exit Shift_DR
	  vmeWrite32(&TIp->eJTAGLoad, shData);
	}

      // shift _DR tail
      vmeWrite32(&TIp->eJTAGLoad,1);  // update Data_Register
      vmeWrite32(&TIp->eJTAGLoad,0);  // back to the Run_test/Idle
    }
  else
    {
      printf( "\n JTAG type %d unrecognized \n",jtagType);
    }

  return rval;
}

static void
Parse(char *buf,unsigned int *Count,char **Word)
{
  *Word = buf;
  *Count = 0;
  while(*buf != '\0')
    {
      while ((*buf==' ') || (*buf=='\t') || (*buf=='\n') || (*buf=='"')) *(buf++)='\0';
      if ((*buf != '\n') && (*buf != '\0'))
	{
	  Word[(*Count)++] = buf;
	}
      while ((*buf!=' ')&&(*buf!='\0')&&(*buf!='\n')&&(*buf!='\t')&&(*buf!='"'))
	{
	  buf++;
	}
    }
  *buf = '\0';
}

void
tiFirmwareEMload(char *filename)
{
  unsigned int ShiftData[64], lineRead;
/*   unsigned int jtagType, jtagBit, iloop; */
  FILE *svfFile;
/*   int byteRead; */
  char bufRead[1024],bufRead2[256];
  unsigned int sndData[256];
  char *Word[16], *lastn;
  unsigned int nbits, nbytes, extrType, i, Count, nWords, nlines=0;
#ifdef CHECKREAD
  unsigned int rval=0;
  int stat=0;
#endif

  //A24 Address modifier redefined
#ifdef VXWORKS
#ifdef TEMPE
  printf("Set A24 mod\n");
  sysTempeSetAM(2,0x19);
#else /* Universe */
  sysUnivSetUserAM(0x19,0);
  sysUnivSetLSI(2,6);
#endif /*TEMPE*/
#else
  printf("\n");
  vmeBusLock();
  vmeSetA24AM(0x19);
#endif

#ifdef DEBUGFW
  printf("%s: A24 memory map is set to AM = 0x19 \n",__FUNCTION__);
#endif

  /* Check if TI board is readable */
#ifdef CHECKREAD
#ifdef VXWORKS
  stat = vxMemProbe((char *)(&TIp->boardID),0,4,(char *)&rval);
#else
  stat = vmeMemProbe((char *)(&TIp->boardID),4,(char *)&rval);
#endif
  if (stat != 0)
    {
      printf("%s: ERROR: TI card not addressable\n",__FUNCTION__);
      TIp=NULL;
      // A24 address modifier reset
#ifdef VXWORKS
#ifdef TEMPE
      sysTempeSetAM(2,0);
#else
      sysUnivSetLSI(2,1);
#endif /*TEMPE*/
#else
      vmeSetA24AM(0);
      vmeBusUnlock();
#endif
      return;
    }
#endif

  //open the file:
  svfFile = fopen(filename,"r");
  if(svfFile==NULL)
    {
      perror("fopen");
      printf("%s: ERROR: Unable to open file %s\n",__FUNCTION__,filename);

      // A24 address modifier reset
#ifdef VXWORKS
#ifdef TEMPE
      sysTempeSetAM(2,0);
#else
      sysUnivSetLSI(2,1);
#endif /*TEMPE*/
#else
      vmeSetA24AM(0);
      vmeBusUnlock();
#endif
      return;
    }

#ifdef DEBUGFW
  printf("\n File is open \n");
#endif

  //PROM JTAG reset/Idle
  Emergency(0,0,ShiftData);
#ifdef DEBUGFW
  printf("%s: Emergency PROM JTAG reset IDLE \n",__FUNCTION__);
#endif
  taskDelay(1);

  //Another PROM JTAG reset/Idle
  Emergency(0,0,ShiftData);
#ifdef DEBUGFW
  printf("%s: Emergency PROM JTAG reset IDLE \n",__FUNCTION__);
#endif
  taskDelay(1);


  //initialization
  extrType = 0;
  lineRead=0;

  printf("\n");
  fflush(stdout);

  /* Count the total number of lines */
  while (fgets(bufRead,256,svfFile) != NULL)
    {
      nlines++;
    }

  rewind(svfFile);

  while (fgets(bufRead,256,svfFile) != NULL)
    {
      lineRead +=1;
      if((lineRead%((int)(nlines/40))) ==0)
	{
#ifdef VXWORKS
	  /* This is pretty filthy... but at least shows some output when it's updating */
	  printf("     ");
	  printf("\b\b\b\b\b");
#endif
	  printf(".");
	  fflush(stdout);
	}
      //    fgets(bufRead,256,svfFile);
      if (((bufRead[0] == '/')&&(bufRead[1] == '/')) || (bufRead[0] == '!'))
	{
	  //	printf(" comment lines: %c%c \n",bufRead[0],bufRead[1]);
	}
      else
	{
	  if (strrchr(bufRead,';') ==0)
	    {
	      do
		{
		  lastn =strrchr(bufRead,'\n');
		  if (lastn !=0) lastn[0]='\0';
		  if (fgets(bufRead2,256,svfFile) != NULL)
		    {
		      strcat(bufRead,bufRead2);
		    }
		  else
		    {
		      printf("\n \n  !!! End of file Reached !!! \n \n");

		      // A24 address modifier reset
#ifdef VXWORKS
#ifdef TEMPE
		      sysTempeSetAM(2,0);
#else
		      sysUnivSetLSI(2,1);
#endif /*TEMPE*/
#else
		      vmeSetA24AM(0);
		      vmeBusUnlock();
#endif
		      return;
		    }
		}
	      while (strrchr(bufRead,';') == 0);  //do while loop
	    }  //end of if

	  // begin to parse the data bufRead
	  Parse(bufRead,&Count,&(Word[0]));
	  if (strcmp(Word[0],"SDR") == 0)
	    {
	      sscanf(Word[1],"%d",&nbits);
	      nbytes = (nbits-1)/8+1;
	      if (strcmp(Word[2],"TDI") == 0)
		{
		  for (i=0; i<nbytes; i++)
		    {
		      sscanf (&Word[3][2*(nbytes-i-1)+1],"%2x",&sndData[i]);
		      //  printf("Word: %c%c, data: %x \n",Word[3][2*(nbytes-i)-1],Word[3][2*(nbytes-i)],sndData[i]);
		    }
		  nWords = (nbits-1)/32+1;
		  for (i=0; i<nWords; i++)
		    {
		      ShiftData[i] = ((sndData[i*4+3]<<24)&0xff000000) + ((sndData[i*4+2]<<16)&0xff0000) + ((sndData[i*4+1]<<8)&0xff00) + (sndData[i*4]&0xff);
		    }
		  // hijacking the PROM usercode:
		  if ((nbits == 32) && (ShiftData[0] == 0x71d55948)) {ShiftData[0] = BoardSerialNumber;}

		  //printf("Word[3]: %s \n",Word[3]);
		  //printf("sndData: %2x %2x %2x %2x, ShiftData: %08x \n",sndData[3],sndData[2],sndData[1],sndData[0], ShiftData[0]);
		  Emergency(2+extrType,nbits,ShiftData);
		}
	    }
	  else if (strcmp(Word[0],"SIR") == 0)
	    {
	      sscanf(Word[1],"%d",&nbits);
	      nbytes = (nbits-1)/8+1;
	      if (strcmp(Word[2],"TDI") == 0)
		{
		  for (i=0; i<nbytes; i++)
		    {
		      sscanf (&Word[3][2*(nbytes-i)-1],"%2x",&sndData[i]);
		      //  printf("Word: %c%c, data: %x \n",Word[3][2*(nbytes-i)-1],Word[3][2*(nbytes-i)],sndData[i]);
		    }
		  nWords = (nbits-1)/32+1;
		  for (i=0; i<nWords; i++)
		    {
		      ShiftData[i] = ((sndData[i*4+3]<<24)&0xff000000) + ((sndData[i*4+2]<<16)&0xff0000) + ((sndData[i*4+1]<<8)&0xff00) + (sndData[i*4]&0xff);
		    }
		  //printf("Word[3]: %s \n",Word[3]);
		  //printf("sndData: %2x %2x %2x %2x, ShiftData: %08x \n",sndData[3],sndData[2],sndData[1],sndData[0], ShiftData[0]);
		  Emergency(1+extrType,nbits,ShiftData);
		}
	    }
	  else if (strcmp(Word[0],"RUNTEST") == 0)
	    {
	      sscanf(Word[1],"%d",&nbits);
	      //	    printf("RUNTEST delay: %d \n",nbits);
	      if(nbits>100000)
		{
		  printf("Erasing (%.1f seconds): ..",((float)nbits)/2./1000000.);
		  fflush(stdout);
		}
#ifdef VXWORKS
	      cpuDelay(nbits*45);   //delay, assuming that the CPU is at 45 MHz
#else
	      usleep(nbits/2);
#endif
	      if(nbits>100000)
		{
		  printf("Done\n");
		  fflush(stdout);
		  printf("          ----------------------------------------\n");
		  printf("Updating: ");
		  fflush(stdout);
		}
/* 	      int time = (nbits/1000)+1; */
/* 	      taskDelay(time);   //delay, assuming that the CPU is at 45 MHz */
	    }
	  else if (strcmp(Word[0],"STATE") == 0)
	    {
	      if (strcmp(Word[1],"RESET") == 0) Emergency(0,0,ShiftData);
	    }
	  else if (strcmp(Word[0],"ENDIR") == 0)
	    {
	      if (strncmp(Word[1], "IDLE", 4) == 0)
		{
		  extrType = 0;
#ifdef DEBUGFW
		  printf(" ExtraType: %d \n",extrType);
#endif
		}
	      else if (strncmp(Word[1], "IRPAUSE", 7) == 0)
		{
		  extrType = 2;
#ifdef DEBUGFW
		  printf(" ExtraType: %d \n",extrType);
#endif
		}
	      else
		{
		  printf(" Unknown ENDIR type %s\n", Word[1]);
		}
	    }
	  else
	    {
#ifdef DEBUGFW
	      printf(" Command type ignored: %s \n",Word[0]);
#endif
	    }

	}  //end of if (comment statement)
    } //end of while

  printf("Done\n");

  printf("** Firmware Update Complete **\n");

  //close the file
  fclose(svfFile);

  // A24 address modifier reset
#ifdef VXWORKS
#ifdef TEMPE
  sysTempeSetAM(2,0);
#else
  sysUnivSetLSI(2,1);
#endif /*TEMPE*/
#else
  vmeSetA24AM(0);
  vmeBusUnlock();
#endif

#ifdef DEBUGFW
  printf("\n A24 memory map is set back to its default \n");
#endif
}


#ifndef VXWORKS
static void
tiFirmwareUsage()
{
  printf("\n");
  printf("%s <VME Address (A24)> <firmware svf file>\n",programName);
  printf("\n");

}
#endif
