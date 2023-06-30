#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
  /* routine prototypes */
  int32_t tiConfigInitGlobals();
  int32_t tiConfig(const char *filename);
  int32_t tiConfigFree();
  void    tiConfigPrintParameters();

  int32_t tiConfigEnablePulser();
  int32_t tiConfigDisablePulser();
  int32_t writeIni(const char *filename);
#ifdef __cplusplus
}
#endif
