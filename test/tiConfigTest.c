/*
 * File:
 *    tiConfigTest.c
 *
 * Description:
 *    Test the ti library ini config file loading
 *
 *
 */


#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "jvme.h"
#include "tiLib.h"
#include "tiConfig.h"

int
main(int argc, char *argv[])
{

  printf("\nJLAB TI Library Tests\n");
  printf("----------------------------\n");

  tiConfigInitGlobals();

  if(argc == 2)
    tiConfig(argv[1]);

  tiConfigFree();

  tiConfigPrintParameters();

  exit(0);
}

/*
  Local Variables:
  compile-command: "make -k tiConfigTest "
  End:
*/
