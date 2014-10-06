/******************************************************************************
*
* Header file for use General USER defined rols with CODA crl (version 2.0)
* 
*   This file implements use of the JLAB TI (pipeline) Module as a trigger interface
*
*                             Bryan Moffit  December 2012
*
*******************************************************************************/
#ifndef __GEN_ROL__
#define __GEN_ROL__

static int GEN_handlers,GENflag;
static int GEN_isAsync;
static unsigned int *GENPollAddr = NULL;
static unsigned int GENPollMask;
static unsigned int GENPollValue;
static unsigned long GEN_prescale = 1;
static unsigned long GEN_count = 0;


/* Put any global user defined variables needed here for GEN readout */
#include "tiLib.h"
extern int tiDoAck;

void
GEN_int_handler()
{
  theIntHandler(GEN_handlers);                   /* Call our handler */
  tiDoAck=0; /* Make sure the library doesn't automatically ACK */
}



/*----------------------------------------------------------------------------
  gen_trigLib.c -- Dummy trigger routines for GENERAL USER based ROLs

 File : gen_trigLib.h

 Routines:
           void gentriglink();       link interrupt with trigger
	   void gentenable();        enable trigger
	   void gentdisable();       disable trigger
	   char genttype();          return trigger type 
	   int  genttest();          test for trigger  (POLL Routine)
------------------------------------------------------------------------------*/


static void
gentriglink(int code, VOIDFUNCPTR isr)
{
  int stat=0;

  tiIntConnect(TI_INT_VEC,isr,0);

}

static void 
gentenable(int code, int card)
{
  int iflag = 1; /* Clear Interrupt scalers */
  int lockkey;

  if(GEN_isAsync==0)
    {
      GENflag = 1;
    }
  
  tiIntEnable(1); 
}

static void 
gentdisable(int code, int card)
{
  int iwait=0, bready=0, iread=0;
  extern unsigned int tiIntCount;

  if(GEN_isAsync==0)
    {
      GENflag = 0;
    }

  /* Disable triggers & interrupts and disconnect ISR */
  tiIntDisable();
  tiIntDisconnect();

  taskDelay(1);

  while(iwait<100)
    {
      iwait++;
      bready=tiBReady();
      
      if(bready)
	{
	  printf("bready = %d\n",bready);
	  for(iread=0; iread<bready; iread++)
	    {
	      tiIntCount++;
	      GEN_int_handler();
	    }
	}
      else
	{
	  tiBlockStatus(0,1);
	  break;
	}
    }

  if(bready!=0)
    {
      printf("WARNING: Events left on TI\n");
    }

}

static unsigned int
genttype(int code)
{
  unsigned int tt=0;

  if(code == 2) {
    tt = 1 ;
  } else {
    tt = 1;
  }

  return(tt);
}

static int 
genttest(int code)
{
  unsigned int ret=0;
  unsigned int tidata=0;

  tidata = tiBReady();

  if(tidata!=-1)
    {
      if(tidata)
	ret = 1;
      else 
	ret = 0;
    }
  else
    {
      ret = 0;
    }

  return ret;
}

static inline void 
gentack(int code, unsigned int intMask)
{
    {
      tiIntAck();
    }
}


/* Define CODA readout list specific Macro routines/definitions */

#define GEN_TEST  genttest

#define GEN_INIT { GEN_handlers =0;GEN_isAsync = 0;GENflag = 0;}

#define GEN_ASYNC(code,id)  {printf("linking async GEN trigger to id %d \n",id); \
			       GEN_handlers = (id);GEN_isAsync = 1;gentriglink(code,GEN_int_handler);}

#define GEN_SYNC(code,id)   {printf("linking sync GEN trigger to id %d \n",id); \
			       GEN_handlers = (id);GEN_isAsync = 0;}

#define GEN_SETA(code) GENflag = code;

#define GEN_SETS(code) GENflag = code;

#define GEN_ENA(code,val) gentenable(code, val);

#define GEN_DIS(code,val) gentdisable(code, val);

#define GEN_CLRS(code) GENflag = 0;

#define GEN_GETID(code) GEN_handlers

#define GEN_TTYPE genttype

#define GEN_START(val)	 {;}

#define GEN_STOP(val)	 {;}

#define GEN_ENCODE(code) (code)

#define GEN_ACK(code,val)   gentack(code,val);

#endif

