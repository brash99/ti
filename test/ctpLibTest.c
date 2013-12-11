/*
 * File:
 *    ctpLibTest.c
 *
 * Description:
 *    Test Vme TI interrupts with GEFANUC Linux Driver
 *    and TI library
 *
 *
 */


#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "jvme.h"
#include "tiLib.h"
#include "ctpLib.h"

int 
main(int argc, char *argv[]) {

    int stat;
    char sn[20];
    vmeOpenDefaultWindows();

    stat = ctpGetSerialNumber(&sn);

    printf("stat = %d     sn = %s\n",stat, sn);

 CLOSE:

    vmeCloseDefaultWindows();

    exit(0);
}

