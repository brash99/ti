/*
 * File:
 *    tid_sendSync.c
 *
 * Description:
 *    Just send a sync... nothing else
 *
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "jvme.h"
#include "tiLib.h"


int
main(int argc, char *argv[]) {

/*     int stat; */

    printf("\nJLAB TID Library Tests\n");
    printf("----------------------------\n");

    vmeOpenDefaultWindows();

    tiInit(21<<19,0,1);

/*     printf("Press <Enter> to send the SYNC\n"); */
/*     getchar(); */
    tiSyncReset(1);
    printf("SENT\n");


    goto CLOSE;

 CLOSE:

    vmeCloseDefaultWindows();

    exit(0);
}
