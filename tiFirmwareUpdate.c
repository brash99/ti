/*
  tidEMload.c
   - "Emergency" JTAG loading via VME for the TID
*/


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
unsigned int BoardSerialNumber;
unsigned int firmwareInfo;
char *programName;

int tiEMInit();
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
  int stat;
  int BoardNumber;
  char *filename;
  int inputchar=10;
  unsigned int vme_addr=0;
  
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
      filename = argv[2];
    }

  vmeSetQuietFlag(1);
  stat = vmeOpenDefaultWindows();
  if(stat != OK)
    goto CLOSE;
#endif

  stat = tiEMInit(vme_addr);
  if(stat != OK)
    {
      printf("\n");
      printf("*** Failed to initialize TI ***\nThis may indicate (either):\n");
      printf("   a) an incorrect VME Address provided\n");
      printf("   b) new firmware must be loaded at provided VME address\n");
      printf("\n");
      printf("Proceed with the update with the provided VME address?\n");
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
	  printf("--- Continuing update, assuming VME address is correct ---\n");
	}
      else
	{
	  goto REPEAT;
	}
    }

  /* Read out the board serial number first */
  BoardSerialNumber = tiGetSerialNumber(NULL);
  printf(" Board Serial Number from PROM usercode is: 0x%08x (%d) \n", BoardSerialNumber,
	 BoardSerialNumber&0xffff);

  firmwareInfo = tiGetFirmwareVersion();
  if(firmwareInfo>0)
    {
      printf("  User ID: 0x%x \tFirmware (version - revision): 0x%X - 0x%03X\n",
	     (firmwareInfo&0xFFFF0000)>>16, (firmwareInfo&0xF000)>>12, firmwareInfo&0xFFF);
    }
  else
    {
      printf("  Error reading Firmware Version\n");
    }


  /* Check the serial number and ask for input if necessary */
  /* Force this program to only work for TI (not TD or TS) */
  if (!((BoardSerialNumber&0xffff0000) == 0x71000000))
    { 
      printf(" This TI has an invalid serial number (0x%08x)\n",BoardSerialNumber);
      printf (" Enter a new board number (0-4095), or -1 to quit: ");

      scanf("%d",&BoardNumber);

      if(BoardNumber == -1)
	{
	  printf("--- Exiting without update ---\n");
	  goto CLOSE;
	}

      /* Add the TI board ID in the MSB */
      BoardSerialNumber = 0x71000000 | (BoardNumber&0xfff);
      printf(" The board serial number will be set to: 0x%08x (%d)\n",BoardSerialNumber,
	     BoardSerialNumber&0xffff);
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


  tiFirmwareEMload(filename);

 CLOSE:

#ifndef VXWORKS
  vmeCloseDefaultWindows();
#endif
  printf("\n");

  return OK;
}

/* Replacement TI initialization routine for that from the tiLib */
int
tiEMInit(unsigned int tAddr)
{
  unsigned int laddr;
  unsigned int rval;
  int stat;

  /* Check VME address */
  if(tAddr<0 || tAddr>0xffffff)
    {
      printf("%s: ERROR: Invalid VME Address (%d)\n",__FUNCTION__,
	     tAddr);
    }

#ifdef VXWORKS
  stat = sysBusToLocalAdrs(0x39,(char *)tAddr,(char **)&laddr);
  if (stat != 0) 
    {
      printf("%s: ERROR: Error in sysBusToLocalAdrs res=%d \n",__FUNCTION__,stat);
      return ERROR;
    } 
  else 
    {
      printf("TI address = 0x%x\n",laddr);
    }
#else
  stat = vmeBusToLocalAdrs(0x39,(char *)tAddr,(char **)&laddr);
  if (stat != 0) 
    {
      printf("%s: ERROR: Error in vmeBusToLocalAdrs res=%d \n",__FUNCTION__,stat);
      return ERROR;
    } 
#endif

  /* Set Up pointer */
  TIp = (struct TI_A24RegStruct *)laddr;

  /* Check if TI board is readable */
#ifdef VXWORKS
  stat = vxMemProbe((char *)(&TIp->boardID),0,4,(char *)&rval);
#else
  stat = vmeMemProbe((char *)(&TIp->boardID),4,(char *)&rval);
#endif

  if (stat != 0) 
    {
      printf("%s: ERROR: TI card not addressable\n",__FUNCTION__);
      return(-1);
    }
  else
    {
      /* Check that it is a TI */
      if(((rval&TI_BOARDID_TYPE_MASK)>>16) != TI_BOARDID_TYPE_TI) 
	{
	  printf("%s: ERROR: Invalid Board ID: 0x%x (rval = 0x%08x)\n",
		 __FUNCTION__,
		 (rval&TI_BOARDID_TYPE_MASK)>>16,rval);
	  /*  Not setting the TIp to NULL here... just in case we're
	      re-programming the firmware */
	  return(ERROR);
	}
    }

  return OK;
}

#ifdef VXWORKS
static void 
cpuDelay(unsigned long delays)
{
  unsigned long time_0, time_1, time, diff;
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

static void 
Emergency(unsigned int jtagType, unsigned int numBits, unsigned long *jtagData)
{
/*   unsigned long *laddr; */
  unsigned int iloop, iword, ibit;
  unsigned long shData;

#ifdef DEBUG
  int numWord, i;
  printf("type: %x, num of Bits: %x, data: \n",jtagType, numBits);
  numWord = (numBits-1)/32+1;
  for (i=0; i<numWord; i++)
    {
      printf("%08x",jtagData[numWord-i-1]);
    }
  printf("\n");
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
  unsigned long ShiftData[64], lineRead;
/*   unsigned int jtagType, jtagBit, iloop; */
  FILE *svfFile;
/*   int byteRead; */
  char bufRead[1024],bufRead2[256];
  unsigned int sndData[256];
  char *Word[16], *lastn;
  unsigned int nbits, nbytes, extrType, i, Count, nWords;// nlines;

  //A24 Address modifier redefined
#ifdef VXWORKS
#ifdef TEMPE
  sysTempeSetAM(2,0x19);
#else /* Universe */
  sysUnivSetUserAM(0x19,0);
  sysUnivSetLSI(2,6);
#endif /*TEMPE*/
#else
  vmeSetA24AM(0x19);
#endif
  printf("%s: A24 memory map is set to AM = 0x19 \n",__FUNCTION__);

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
#endif
      return;
    }
  printf("\n File is open \n");

  //PROM JTAG reset/Idle
  Emergency(0,0,ShiftData);
  printf("%s: Emergency PROM JTAG reset IDLE \n",__FUNCTION__);
  taskDelay(1);

  //Another PROM JTAG reset/Idle
  Emergency(0,0,ShiftData);
  printf("%s: Emergency PROM JTAG reset IDLE \n",__FUNCTION__);
  taskDelay(1);


  //initialization
  extrType = 0;
  lineRead=0;

  //  for (nlines=0; nlines<200; nlines++)
  while (fgets(bufRead,256,svfFile) != NULL)
    { 
      lineRead +=1;
      if (lineRead%1000 ==0) printf(" Lines read: %d out of 787000 \n",(int)lineRead);
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
#ifdef VXWORKS
	      cpuDelay(nbits*45);   //delay, assuming that the CPU is at 45 MHz
#else
	      usleep(nbits/2);
#endif
/* 	      int time = (nbits/1000)+1; */
/* 	      taskDelay(time);   //delay, assuming that the CPU is at 45 MHz */
	    }
	  else if (strcmp(Word[0],"STATE") == 0)
	    {
	      if (strcmp(Word[1],"RESET") == 0) Emergency(0,0,ShiftData);
	    }
	  else if (strcmp(Word[0],"ENDIR") == 0)
	    {
	      if (strcmp(Word[1],"IDLE") ==0 )
		{
		  extrType = 0;
		  printf(" ExtraType: %d \n",extrType);
		}
	      else if (strcmp(Word[1],"IRPAUSE") ==0)
		{
		  extrType = 2;
		  printf(" ExtraType: %d \n",extrType);
		}
	      else
		{
		  printf(" Unknown ENDIR type %s\n",Word[1]);
		}
	    }
	  else
	    {
	      printf(" Command type ignored: %s \n",Word[0]);
	    }

	}  //end of if (comment statement)
    } //end of while

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
#endif
  printf("\n A24 memory map is set back to its default \n");
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
