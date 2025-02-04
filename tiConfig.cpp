#include <cstring>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <string>
#include <sstream>
#include <memory>
#include "tiConfig.h"
#include "INIReader.h"

#ifdef __cplusplus
extern "C" {
#include "jvme.h"
#include "tiLib.h"
}
#endif


// place to store the ini INIReader instance
INIReader *ir;

typedef std::map<std::string, int32_t> ti_param_map;

const ti_param_map ti_general_def
  {
    { "CRATE_ID", -1 },
    { "BLOCK_LEVEL", -1 },
    { "BLOCK_BUFFER_LEVEL", -1 },

    { "INSTANT_BLOCKLEVEL_ENABLE", -1 },
    { "BROADCAST_BUFFER_LEVEL_ENABLE", -1 },

    { "BLOCK_LIMIT", -1 },

    { "TRIGGER_SOURCE", -1 },

    { "SYNC_SOURCE", -1 },
    { "SYNC_RESET_TYPE", -1 },

    { "BUSY_SOURCE_SWA", -1 },
    { "BUSY_SOURCE_SWB", -1 },
    { "BUSY_SOURCE_P2", -1 },
    { "BUSY_SOURCE_FP_TDC", -1 },
    { "BUSY_SOURCE_FP_ADC", -1 },
    { "BUSY_SOURCE_FP", -1 },
    { "BUSY_SOURCE_LOOPBACK", -1 },
    { "BUSY_SOURCE_FIBER1", -1 },
    { "BUSY_SOURCE_FIBER2", -1 },
    { "BUSY_SOURCE_FIBER3", -1 },
    { "BUSY_SOURCE_FIBER4", -1 },
    { "BUSY_SOURCE_FIBER5", -1 },
    { "BUSY_SOURCE_FIBER6", -1 },
    { "BUSY_SOURCE_FIBER7", -1 },
    { "BUSY_SOURCE_FIBER8", -1 },

    { "CLOCK_SOURCE", -1 },

    { "PRESCALE", -1 },
    { "EVENT_FORMAT", -1 },
    { "FP_INPUT_READOUT_ENABLE", -1 },

    { "GO_OUTPUT_ENABLE", -1 },

    { "TRIGGER_WINDOW", -1 },
    { "TRIGGER_INHIBIT_WINDOW", -1 },

    { "TRIGGER_LATCH_ON_LEVEL_ENABLE", -1 },

    { "TRIGGER_OUTPUT_DELAY", -1 },
    { "TRIGGER_OUTPUT_DELAYSTEP", -1 },
    { "TRIGGER_OUTPUT_WIDTH", -1 },

    { "PROMPT_TRIGGER_WIDTH", -1 },

    { "SYNCRESET_DELAY", -1 },
    { "SYNCRESET_WIDTH", -1 },
    { "SYNCRESET_WIDTHSTEP", -1 },

    { "EVENTTYPE_SCALERS_ENABLE", -1 },
    { "SCALER_MODE", -1 },
    { "SCALER_MODE_CONTROL", -1 },
    { "SYNCEVENT_INTERVAL", -1 },

    { "TRIGGER_TABLE", -1 },

    { "FIXED_PULSER_EVENTTYPE", -1 },
    { "RANDOM_PULSER_EVENTTYPE", -1 },

    { "FIBER_SYNC_DELAY", -1 }
  };
static ti_param_map ti_general_ini = ti_general_def, ti_general_readback = ti_general_def;

const ti_param_map ti_slaves_def
  {
    { "ENABLE_FIBER_1", -1},
    { "ENABLE_FIBER_2", -1},
    { "ENABLE_FIBER_3", -1},
    { "ENABLE_FIBER_4", -1},
    { "ENABLE_FIBER_5", -1},
    { "ENABLE_FIBER_6", -1},
    { "ENABLE_FIBER_7", -1},
    { "ENABLE_FIBER_8", -1},
  };
static ti_param_map ti_slaves_ini = ti_slaves_def, ti_slaves_readback = ti_slaves_def;

const ti_param_map ti_tsinputs_def
  {
    { "ENABLE_TS1", -1 },
    { "ENABLE_TS2", -1 },
    { "ENABLE_TS3", -1 },
    { "ENABLE_TS4", -1 },
    { "ENABLE_TS5", -1 },
    { "ENABLE_TS6", -1 },

    { "PRESCALE_TS1", -1 },
    { "PRESCALE_TS2", -1 },
    { "PRESCALE_TS3", -1 },
    { "PRESCALE_TS4", -1 },
    { "PRESCALE_TS5", -1 },
    { "PRESCALE_TS6", -1 },

    { "DELAY_TS1", -1 },
    { "DELAY_TS2", -1 },
    { "DELAY_TS3", -1 },
    { "DELAY_TS4", -1 },
    { "DELAY_TS5", -1 },
    { "DELAY_TS6", -1 }
  };
static ti_param_map ti_tsinputs_ini = ti_tsinputs_def, ti_tsinputs_readback = ti_tsinputs_def;

const ti_param_map ti_rules_def =
  {
    { "RULE_1", -1 },
    { "RULE_TIMESTEP_1", -1 },
    { "RULE_MIN_1", -1 },
    { "RULE_2", -1 },
    { "RULE_TIMESTEP_2", -1 },
    { "RULE_MIN_2", -1 },
    { "RULE_3", -1 },
    { "RULE_TIMESTEP_3", -1 },
    { "RULE_MIN_3", -1 },
    { "RULE_4", -1 },
    { "RULE_TIMESTEP_4", -1 },
    { "RULE_MIN_4", -1 },
  };
static ti_param_map ti_rules_ini = ti_rules_def, ti_rules_readback = ti_rules_def;

const ti_param_map ti_pulser_def =
  {
    { "FIXED_ENABLE", -1 },
    { "FIXED_NUMBER", -1 },
    { "FIXED_PERIOD", -1 },
    { "FIXED_RANGE", -1 },
    { "RANDOM_ENABLE", -1 },
    { "RANDOM_PRESCALE", -1 },
  };
static ti_param_map ti_pulser_ini = ti_pulser_def;



int32_t
tiConfigInitGlobals()
{
#ifdef DEBUG
  std::cout << __func__ << ": INFO: here" << std::endl;
#endif

  return 0;
}


/**
 * @brief Write the Ini values to the local ti_param_map's
 */
void
parseIni()
{
#ifdef DEBUG
  std::cout << __func__ << ": INFO: here" << std::endl;
#endif

  if(ir == NULL)
    return;

  ti_param_map::const_iterator pos = ti_general_def.begin();

  while(pos != ti_general_def.end())
    {
      ti_general_ini[pos->first] = ir->GetInteger("general", pos->first, pos->second);
      ++pos;
    }

  pos = ti_slaves_def.begin();
  while(pos != ti_slaves_def.end())
    {
      ti_slaves_ini[pos->first] = ir->GetInteger("slaves", pos->first, pos->second);
      ++pos;
    }

  pos = ti_tsinputs_def.begin();
  while(pos != ti_tsinputs_def.end())
    {
      ti_tsinputs_ini[pos->first] = ir->GetInteger("tsinputs", pos->first, pos->second);
      ++pos;
    }

  pos = ti_rules_def.begin();
  while(pos != ti_rules_def.end())
    {
      ti_rules_ini[pos->first] = ir->GetInteger("trigger_rules", pos->first, pos->second);
      ++pos;
    }

  pos = ti_pulser_def.begin();
  while(pos != ti_pulser_def.end())
    {
      ti_pulser_ini[pos->first] = ir->GetInteger("pulser", pos->first, pos->second);
      ++pos;
    }


}

/**
 * @brief Print the values stored in the local structure
 */
void
tiConfigPrintParameters()
{
#ifdef DEBUG
  std::cout << __func__ << ": INFO: HERE" << std::endl;
#endif

  ti_param_map::const_iterator pos = ti_general_ini.begin();

  printf("[general]\n");
  while(pos != ti_general_ini.end())
    {
      printf("  %28.24s = 0x%08x (%d)\n", pos->first.c_str(), pos->second, pos->second);
      ++pos;
    }

  pos = ti_slaves_ini.begin();

  printf("[slaves]\n");
  while(pos != ti_slaves_ini.end())
    {
      printf("  %28.24s = 0x%08x (%d)\n", pos->first.c_str(), pos->second, pos->second);
      ++pos;
    }

  pos = ti_tsinputs_ini.begin();

  printf("[tsinputs]\n");
  while(pos != ti_tsinputs_ini.end())
    {
      printf("  %28.24s = 0x%08x (%d)\n", pos->first.c_str(), pos->second, pos->second);
      ++pos;
    }

  pos = ti_rules_ini.begin();

  printf("[trigger rules]\n");
  while(pos != ti_rules_ini.end())
    {
      printf("  %28.24s = 0x%08x (%d)\n", pos->first.c_str(), pos->second, pos->second);
      ++pos;
    }

  pos = ti_pulser_ini.begin();

  printf("[pulser]\n");
  while(pos != ti_pulser_ini.end())
    {
      printf("  %28.24s = 0x%08x (%d)\n", pos->first.c_str(), pos->second, pos->second);
      ++pos;
    }


}

/**
 * @brief Write the ini parameters to the module
 * @return 0
 */
int32_t
param2ti()
{
#ifdef DEBUG
  std::cout << __func__ << ": INFO: here" << std::endl;
#endif

  int32_t param_val = 0, ti_rval = OK, rval = OK;
  ti_param_map::const_iterator pos;

#define CHECK_PARAM(__ini, __key)					\
  pos = __ini.find(__key);						\
  if(pos == __ini.end()) {						\
    std::cerr << __func__ << ": ERROR finding " << __key << std::endl;	\
    return ERROR;							\
  }									\
  param_val = pos->second;

  /////////////////
  // GENERAL
  /////////////////

  CHECK_PARAM(ti_general_ini, "CRATE_ID");
  if(param_val > 0)
    {
      ti_rval = tiSetCrateID(param_val);
      if(ti_rval != OK)
	rval = ERROR;
    }
  if(rval!=0)
    {
      std::cerr << __func__ << ": ERROR writing to Parameters to TI" << std::endl;
      return rval;
    }

  CHECK_PARAM(ti_general_ini, "BLOCK_LEVEL");
  if(param_val > 0)
    {
      ti_rval = tiSetBlockLevel(param_val);
      if(ti_rval != OK)
	rval = ERROR;
    }

  CHECK_PARAM(ti_general_ini, "BLOCK_BUFFER_LEVEL");
  if(param_val > 0)
    {
      ti_rval = tiSetBlockBufferLevel(param_val);
      if(ti_rval != OK)
	rval = ERROR;
    }

  CHECK_PARAM(ti_general_ini, "INSTANT_BLOCKLEVEL_ENABLE");
  if(param_val > 0)
    {
      ti_rval = tiSetInstantBlockLevelChange(param_val);
      if(ti_rval != OK)
	rval = ERROR;
    }

  CHECK_PARAM(ti_general_ini, "BROADCAST_BUFFER_LEVEL_ENABLE");
  if(param_val > 0)
    {
      ti_rval = tiUseBroadcastBufferLevel(param_val);
      if(ti_rval != OK)
	rval = ERROR;
    }

  CHECK_PARAM(ti_general_ini, "BLOCK_LIMIT");
  if(param_val > 0)
    {
      ti_rval = tiSetBlockLimit(param_val);
      if(ti_rval != OK)
	rval = ERROR;
    }

  CHECK_PARAM(ti_general_ini, "TRIGGER_SOURCE");
  if(param_val > 0)
    {
      ti_rval = tiSetTriggerSource(param_val);
      if(ti_rval != OK)
	rval = ERROR;
    }

  CHECK_PARAM(ti_general_ini, "SYNC_SOURCE");
  if(param_val > 0)
    {
      uint32_t sync_set = 0;
      // Encode the selection for the routine input
      switch(param_val)
	{
	case 0:
	  sync_set = TI_SYNC_P0;
	  break;
	case 1:
	  sync_set = TI_SYNC_HFBR1;
	  break;
	case 2:
	  sync_set = TI_SYNC_HFBR5;
	  break;
	case 3:
	  sync_set = TI_SYNC_FP;
	  break;
	case 4:
	default:
	  sync_set = TI_SYNC_LOOPBACK;
	}

      ti_rval = tiSetSyncSource(sync_set);
      if(ti_rval != OK)
	rval = ERROR;
    }

  CHECK_PARAM(ti_general_ini, "SYNC_RESET_TYPE");
  if(param_val > 0)
    {
      ti_rval = tiSetSyncResetType(param_val);
      if(ti_rval != OK)
	rval = ERROR;
    }

  /* Busy Source, build a busy source mask */
  uint32_t busy_source_mask = 0;

  CHECK_PARAM(ti_general_ini, "BUSY_SOURCE_SWA");
  if(param_val > 0)
    busy_source_mask |= TI_BUSY_SWA;

  CHECK_PARAM(ti_general_ini, "BUSY_SOURCE_SWB");
  if(param_val > 0)
    busy_source_mask |= TI_BUSY_SWB;

  CHECK_PARAM(ti_general_ini, "BUSY_SOURCE_FP_TDC");
  if(param_val > 0)
    busy_source_mask |= TI_BUSY_FP_FTDC;

  CHECK_PARAM(ti_general_ini, "BUSY_SOURCE_FP_ADC");
  if(param_val > 0)
    busy_source_mask |= TI_BUSY_FP_FADC;

  CHECK_PARAM(ti_general_ini, "BUSY_SOURCE_FP");
  if(param_val > 0)
    busy_source_mask |= TI_BUSY_FP;

  CHECK_PARAM(ti_general_ini, "BUSY_SOURCE_LOOPBACK");
  if(param_val > 0)
    busy_source_mask |= TI_BUSY_LOOPBACK;

  CHECK_PARAM(ti_general_ini, "BUSY_SOURCE_FIBER1");
  if(param_val > 0)
    busy_source_mask |= (TI_BUSY_HFBR1 << 0);

  CHECK_PARAM(ti_general_ini, "BUSY_SOURCE_FIBER2");
  if(param_val > 0)
    busy_source_mask |= (TI_BUSY_HFBR1 << 1);

  CHECK_PARAM(ti_general_ini, "BUSY_SOURCE_FIBER3");
  if(param_val > 0)
    busy_source_mask |= (TI_BUSY_HFBR1 << 2);

  CHECK_PARAM(ti_general_ini, "BUSY_SOURCE_FIBER4");
  if(param_val > 0)
    busy_source_mask |= (TI_BUSY_HFBR1 << 3);

  CHECK_PARAM(ti_general_ini, "BUSY_SOURCE_FIBER5");
  if(param_val > 0)
    busy_source_mask |= (TI_BUSY_HFBR1 << 4);

  CHECK_PARAM(ti_general_ini, "BUSY_SOURCE_FIBER6");
  if(param_val > 0)
    busy_source_mask |= (TI_BUSY_HFBR1 << 5);

  CHECK_PARAM(ti_general_ini, "BUSY_SOURCE_FIBER7");
  if(param_val > 0)
    busy_source_mask |= (TI_BUSY_HFBR1 << 6);

  CHECK_PARAM(ti_general_ini, "BUSY_SOURCE_FIBER8");
  if(param_val > 0)
    busy_source_mask |= (TI_BUSY_HFBR1 << 7);

  if(busy_source_mask != 0)
    {
      ti_rval = tiSetBusySource(busy_source_mask, 1);
      if(ti_rval != OK)
	rval = ERROR;
    }


  CHECK_PARAM(ti_general_ini, "CLOCK_SOURCE");
  if(param_val > 0)
    {
      ti_rval = tiSetClockSource(param_val);
      if(ti_rval != OK)
	rval = ERROR;
    }

  CHECK_PARAM(ti_general_ini, "PRESCALE");
  if(param_val > 0)
    {
      ti_rval = tiSetPrescale(param_val);
      if(ti_rval != OK)
	rval = ERROR;
    }

  CHECK_PARAM(ti_general_ini, "EVENT_FORMAT");
  if(param_val > 0)
    {
      ti_rval = tiSetEventFormat(param_val);
      if(ti_rval != OK)
	rval = ERROR;
    }

  CHECK_PARAM(ti_general_ini, "FP_INPUT_READOUT_ENABLE");
  if(param_val > 0)
    {
      ti_rval = tiSetFPInputReadout(param_val);
      if(ti_rval != OK)
	rval = ERROR;
    }

  CHECK_PARAM(ti_general_ini, "GO_OUTPUT_ENABLE");
  if(param_val > 0)
    {
      ti_rval = tiSetGoOutput(param_val);
      if(ti_rval != OK)
	rval = ERROR;
    }

  CHECK_PARAM(ti_general_ini, "TRIGGER_WINDOW");
  if(param_val > 0)
    {
      ti_rval = tiSetTriggerWindow(param_val);
      if(ti_rval != OK)
	rval = ERROR;
    }

  CHECK_PARAM(ti_general_ini, "TRIGGER_INHIBIT_WINDOW");
  if(param_val > 0)
    {
      ti_rval = tiSetTriggerInhibitWindow(param_val);
      if(ti_rval != OK)
	rval = ERROR;
    }

  CHECK_PARAM(ti_general_ini, "TRIGGER_LATCH_ON_LEVEL_ENABLE");
  if(param_val > 0)
    {
      ti_rval = tiSetTriggerLatchOnLevel(param_val);
      if(ti_rval != OK)
	rval = ERROR;
    }

  CHECK_PARAM(ti_general_ini, "TRIGGER_OUTPUT_DELAY");
  if(param_val > 0)
    {
      int32_t delay = param_val, width = 0, delaystep = 0;

      CHECK_PARAM(ti_general_ini, "TRIGGER_OUTPUT_WIDTH");
      width = param_val;

      CHECK_PARAM(ti_general_ini, "TRIGGER_OUTPUT_DELAYSTEP");
      delaystep = param_val;

      ti_rval = tiSetTriggerPulse(1, delay, width, delaystep);
      if(ti_rval != OK)
	rval = ERROR;
    }

  CHECK_PARAM(ti_general_ini, "PROMPT_TRIGGER_WIDTH");
  if(param_val > 0)
    {
      int32_t width = param_val;

      ti_rval = tiSetPromptTriggerWidth(width);
      if(ti_rval != OK)
	rval = ERROR;
    }

  CHECK_PARAM(ti_general_ini, "SYNCRESET_DELAY");
  if(param_val > 0)
    {
      int32_t delay = param_val, width = 0, widthstep = 0;

      CHECK_PARAM(ti_general_ini, "SYNCRESET_WIDTH");
      width = param_val;

      CHECK_PARAM(ti_general_ini, "SYNCRESET_WIDTHSTEP");
      widthstep = param_val;

      tiSetSyncDelayWidth(delay, width, widthstep);
    }

  CHECK_PARAM(ti_general_ini, "EVENTTYPE_SCALERS_ENABLE");
  if(param_val > 0)
    {
      ti_rval = tiSetEvTypeScalers(param_val);
      if(ti_rval != OK)
	rval = ERROR;
    }

  CHECK_PARAM(ti_general_ini, "SCALER_MODE");
  if(param_val > 0)
    {
      int32_t control = 0;

      CHECK_PARAM(ti_general_ini, "SCALER_MODE_CONTROL");
      control = param_val;

      ti_rval = tiSetScalerMode(param_val, control);
      if(ti_rval != OK)
	rval = ERROR;

    }

  CHECK_PARAM(ti_general_ini, "SYNCEVENT_INTERVAL");
  if(param_val > 0)
    {
      ti_rval = tiSetSyncEventInterval(param_val);
      if(ti_rval != OK)
	rval = ERROR;

    }

  CHECK_PARAM(ti_general_ini, "TRIGGER_TABLE");
  if(param_val > 0)
    {
      ti_rval = tiTriggerTablePredefinedConfig(param_val);
      if(ti_rval != OK)
	rval = ERROR;

    }

  CHECK_PARAM(ti_general_ini, "FIXED_PULSER_EVENTTYPE");
  if(param_val > 0)
    {
      int32_t fixed = param_val, random = 0;

      CHECK_PARAM(ti_general_ini, "RANDOM_PULSER_EVENTTYPE");
      random = param_val;

      ti_rval = tiDefinePulserEventType(fixed, random);
      if(ti_rval != OK)
	rval = ERROR;

    }

  CHECK_PARAM(ti_general_ini, "FIBER_SYNC_DELAY");
  if(param_val > 0)
    {
      tiSetFiberSyncDelay(param_val);
    }

  /////////////////
  // SLAVES
  /////////////////

  for(int32_t inp = 1; inp <= 8; inp++)
    {
      CHECK_PARAM(ti_slaves_ini, "ENABLE_FIBER_" + std::to_string(inp));
      if(param_val > 0)
	{
	  ti_rval = tiAddSlave(inp);
	  if(ti_rval != OK)
	    rval = ERROR;
	}
    }

  /////////////////
  // TS INPUTS
  /////////////////

  uint32_t input_mask = 0;
  for(int32_t inp = 1; inp <= 6; inp++)
    {
      CHECK_PARAM(ti_tsinputs_ini, "ENABLE_TS" + std::to_string(inp));
      if(param_val != -1)
	{
	  if(param_val)
	    input_mask |= (1 << (inp-1));
	}
    }
  if(input_mask > 0)
    {
      ti_rval = tiEnableTSInput(input_mask);
      if(ti_rval != OK)
	rval = ERROR;
    }

  for(int32_t inp = 1; inp <= 6; inp++)
    {
      CHECK_PARAM(ti_tsinputs_ini, "PRESCALE_TS" + std::to_string(inp));
      if(param_val > 0)
	{
	  ti_rval = tiSetInputPrescale(inp, param_val);
	  if(ti_rval != OK)
	    rval = ERROR;
	}
    }

  for(int32_t inp = 1; inp <= 6; inp++)
    {
      CHECK_PARAM(ti_tsinputs_ini, "DELAY_TS" + std::to_string(inp));
      if(param_val > 0)
	{
	  ti_rval = tiSetTSInputDelay(inp, param_val);
	  if(ti_rval != OK)
	    rval = ERROR;
	}
    }

  /////////////////
  // TRIGGER RULES
  /////////////////

  for(int32_t rule = 1; rule <= 4; rule++)
    {
      int32_t rule_val = 0, rule_timestep = 0;
      CHECK_PARAM(ti_rules_ini, "RULE_" + std::to_string(rule));
      if(param_val >= 0)
	{
	  rule_val = param_val;

	  CHECK_PARAM(ti_rules_ini, "RULE_TIMESTEP_" + std::to_string(rule));
	  if(param_val >= 0)
	    {
	      rule_timestep = param_val;

	      ti_rval = tiSetTriggerHoldoff(rule, rule_val, rule_timestep);
	      if(ti_rval != OK)
		rval = ERROR;
	    }

	}

      if(rule == 1)
	continue;

      int32_t rule_min = 0;
      CHECK_PARAM(ti_rules_ini, "RULE_MIN_" + std::to_string(rule));
      if(param_val >= 0)
	{
	  ti_rval = tiSetTriggerHoldoffMin(rule, param_val);
	  if(ti_rval != OK)
	    rval = ERROR;
	}
    }

  return rval;
}

int32_t
tiConfigLoadParameters()
{
  if(ir == NULL)
    return 1;

  parseIni();

  if(param2ti() == ERROR)
    return ERROR;


  return 0;
}

// load in parameters to structure from filename
int32_t
tiConfig(const char *filename)
{
#ifdef DEBUG
  std::cout << __func__ << ": INFO: here" << std::endl;
#endif

  ir = new INIReader(filename);

  if(ir->ParseError() < 0)
    {
      std::cout << "Can't load: " << filename << std::endl;
      return ERROR;
    }

  tiConfigLoadParameters();

  return 0;
}

int32_t
tiConfigEnablePulser()
{
  int32_t param_val = 0, rval = OK;
  ti_param_map::const_iterator pos;

  int32_t fixed_enable = 0, fixed_number = 0, fixed_period = 0, fixed_range = 0;
  int32_t random_enable = 0, random_prescale = 0;

  CHECK_PARAM(ti_pulser_ini, "FIXED_ENABLE");
  fixed_enable = param_val;

  CHECK_PARAM(ti_pulser_ini, "FIXED_NUMBER");
  fixed_number = param_val;

  CHECK_PARAM(ti_pulser_ini, "FIXED_PERIOD");
  fixed_period = param_val;

  CHECK_PARAM(ti_pulser_ini, "FIXED_RANGE");
  fixed_range = param_val;

  CHECK_PARAM(ti_pulser_ini, "RANDOM_ENABLE");
  random_enable = param_val;

  CHECK_PARAM(ti_pulser_ini, "RANDOM_PRESCALE");
  random_prescale = param_val;

  if(fixed_enable)
    {
      tiSoftTrig(1, fixed_number, fixed_number, fixed_range);
    }

  if(random_enable)
    {
      tiSetRandomTrigger(1, random_prescale);
    }

  return OK;
}

int32_t
tiConfigDisablePulser()
{
  int32_t param_val = 0, rval = OK;
  ti_param_map::const_iterator pos;

  int32_t fixed_enable = 0, random_enable = 0;

  CHECK_PARAM(ti_pulser_ini, "FIXED_ENABLE");
  fixed_enable = param_val;

  CHECK_PARAM(ti_pulser_ini, "RANDOM_ENABLE");
  random_enable = param_val;

  if(fixed_enable)
    {
      tiSoftTrig(1, 0, 0, 0);
    }

  if(random_enable)
    {
      tiDisableRandomTrigger();
    }

  return OK;
}

/**
 * @brief Read the module parameters input the maps
 * @return 0
 */

int32_t
ti2param()
{
  int32_t rval = OK, ti_rval = OK;

  /////////////////
  // GENERAL
  /////////////////

  ti_rval = tiGetCrateID(0);
  if(ti_rval == ERROR)
    rval = ERROR;
  else
    ti_general_readback["CRATE_ID"] = ti_rval;

  ti_rval = tiGetCurrentBlockLevel();
  if(ti_rval == ERROR)
    rval = ERROR;
  else
    ti_general_readback["BLOCK_LEVEL"] = ti_rval;

  ti_rval = tiGetBlockBufferLevel();
  if(ti_rval == ERROR)
    rval = ERROR;
  else
    ti_general_readback["BLOCK_BUFFER_LEVEL"] = ti_rval;

  ti_rval = tiGetInstantBlockLevelChange();
  if(ti_rval == ERROR)
    rval = ERROR;
  else
    ti_general_readback["INSTANT_BLOCKLEVEL_ENABLE"] = ti_rval;

  ti_rval = tiGetUseBroadcastBufferLevel();
  if(ti_rval == ERROR)
    rval = ERROR;
  else
    ti_general_readback["BROADCAST_BUFFER_LEVEL_ENABLE"] = ti_rval;

  ti_rval = tiGetBlockLimit();
  if(ti_rval == ERROR)
    rval = ERROR;
  else
    ti_general_readback["BLOCK_LIMIT"] = ti_rval;

  ti_rval = tiGetTriggerSource();
  if(ti_rval == ERROR)
    rval = ERROR;
  else
    ti_general_readback["TRIGGER_SOURCE"] = ti_rval;

  ti_rval = tiGetSyncSource();
  if(ti_rval == ERROR)
    rval = ERROR;
  else
    {
      int32_t sync_val = -1;
      // decode the return bits into value for config file
      if(ti_rval & TI_SYNC_P0)
	sync_val = 0;

      if(ti_rval & TI_SYNC_HFBR1)
	sync_val = 1;

      if(ti_rval & TI_SYNC_HFBR5)
	sync_val = 2;

      if(ti_rval & TI_SYNC_FP)
	sync_val = 3;

      if(ti_rval & TI_SYNC_LOOPBACK)
	sync_val = 4;

      ti_general_readback["SYNC_SOURCE"] = sync_val;

    }

  ti_rval = tiGetSyncResetType();
  if(ti_rval == ERROR)
    rval = ERROR;
  else
    ti_general_readback["SYNC_RESET_TYPE"] = ti_rval;

  // Slave the slave bits for the slave section
  int32_t slave_bits = 0;
  ti_rval = tiGetBusySource();
  if(ti_rval == ERROR)
    rval = ERROR;
  else
    {
      if(ti_rval & TI_BUSY_SWA)
	ti_general_readback["BUSY_SOURCE_SWA"] = 1;

      if(ti_rval & TI_BUSY_SWB)
	ti_general_readback["BUSY_SOURCE_SWB"] = 1;

      if(ti_rval & TI_BUSY_FP_FTDC)
	ti_general_readback["BUSY_SOURCE_FP_TDC"] = 1;

      if(ti_rval & TI_BUSY_FP_FADC)
	ti_general_readback["BUSY_SOURCE_FP_ADC"] = 1;

      if(ti_rval & TI_BUSY_FP)
	ti_general_readback["BUSY_SOURCE_FP"] = 1;

      if(ti_rval & TI_BUSY_LOOPBACK)
	ti_general_readback["BUSY_SOURCE_LOOPBACK"] = 1;

      if(ti_rval & TI_BUSY_HFBR1)
	{
	  slave_bits |= (1 << 0);
	  ti_general_readback["BUSY_SOURCE_FIBER1"] = 1;
	}

      if(ti_rval & TI_BUSY_HFBR2)
	{
	  slave_bits |= (1 << 1);
	  ti_general_readback["BUSY_SOURCE_FIBER2"] = 1;
	}

      if(ti_rval & TI_BUSY_HFBR3)
	{
	  slave_bits |= (1 << 2);
	  ti_general_readback["BUSY_SOURCE_FIBER3"] = 1;
	}

      if(ti_rval & TI_BUSY_HFBR4)
	{
	  slave_bits |= (1 << 3);
	  ti_general_readback["BUSY_SOURCE_FIBER4"] = 1;
	}

      if(ti_rval & TI_BUSY_HFBR5)
	{
	  slave_bits |= (1 << 4);
	  ti_general_readback["BUSY_SOURCE_FIBER5"] = 1;
	}

      if(ti_rval & TI_BUSY_HFBR6)
	{
	  slave_bits |= (1 << 5);
	  ti_general_readback["BUSY_SOURCE_FIBER6"] = 1;
	}

      if(ti_rval & TI_BUSY_HFBR7)
	{
	  slave_bits |= (1 << 6);
	  ti_general_readback["BUSY_SOURCE_FIBER7"] = 1;
	}

      if(ti_rval & TI_BUSY_HFBR8)
	{
	  slave_bits |= (1 << 7);
	  ti_general_readback["BUSY_SOURCE_FIBER8"] = 1;
	}
    }

  ti_rval = tiGetClockSource();
  if(ti_rval == ERROR)
    rval = ERROR;
  else
    ti_general_readback["CLOCK_SOURCE"] = ti_rval;

  ti_rval = tiGetPrescale();
  if(ti_rval == ERROR)
    rval = ERROR;
  else
    ti_general_readback["PRESCALE"] = ti_rval;

  ti_rval = tiGetEventFormat();
  if(ti_rval == ERROR)
    rval = ERROR;
  else
    ti_general_readback["EVENT_FORMAT"] = ti_rval;

  ti_rval = tiGetFPInputReadout();
  if(ti_rval == ERROR)
    rval = ERROR;
  else
    ti_general_readback["FP_INPUT_READOUT_ENABLE"] = ti_rval;

  ti_rval = tiGetGoOutput();
  if(ti_rval == ERROR)
    rval = ERROR;
  else
    ti_general_readback["GO_OUTPUT_ENABLE"] = ti_rval;

  ti_rval = tiGetTriggerWindow();
  if(ti_rval == ERROR)
    rval = ERROR;
  else
    ti_general_readback["TRIGGER_WINDOW"] = ti_rval;

  ti_rval = tiGetTriggerInhibitWindow();
  if(ti_rval == ERROR)
    rval = ERROR;
  else
    ti_general_readback["TRIGGER_INHIBIT_WINDOW"] = ti_rval;

  ti_rval = tiGetTriggerLatchOnLevel();
  if(ti_rval == ERROR)
    rval = ERROR;
  else
    ti_general_readback["TRIGGER_LATCH_ON_LEVEL_ENABLE"] = ti_rval;

  int32_t delay = 0, width = 0, delay_step = 0;
  ti_rval = tiGetTriggerPulse(1, &delay, &width, &delay_step);
  if(ti_rval == ERROR)
    rval = ERROR;
  else
    {
      ti_general_readback["TRIGGER_OUTPUT_DELAY"] = delay;
      ti_general_readback["TRIGGER_OUTPUT_WIDTH"] = width;
      ti_general_readback["TRIGGER_OUTPUT_DELAYSTEP"] = delay_step;
    }

  ti_rval = tiGetPromptTriggerWidth();
  if(ti_rval == ERROR)
    rval = ERROR;
  else
    ti_general_readback["PROMPT_TRIGGER_WIDTH"] = ti_rval;

  int32_t width_step = 0;
  delay = 0;
  width = 0;

  ti_rval = tiGetSyncDelayWidth(&delay, &width, &width_step);
  if(ti_rval == ERROR)
    rval = ERROR;
  else
    {
      ti_general_readback["SYNCRESET_DELAY"] = delay;
      ti_general_readback["SYNCRESET_WIDTH"] = width;
      ti_general_readback["SYNCRESET_WIDTHSTEP"] = width_step;
    }

  ti_rval = tiGetEvTypeScalersFlag();
  if(ti_rval == ERROR)
    rval = ERROR;
  else
    ti_general_readback["EVENTTYPE_SCALERS_ENABLE"] = ti_rval;

  int32_t mode = 0, control = 0;
  ti_rval = tiGetScalerMode(&mode, &control);
  if(ti_rval == ERROR)
    rval = ERROR;
  else
    {
      ti_general_readback["SCALER_MODE"] = mode;
      ti_general_readback["SCALER_MODE_CONTROL"] = control;
    }

  ti_rval = tiGetSyncEventInterval();
  if(ti_rval == ERROR)
    rval = ERROR;
  else
    ti_general_readback["SYNCEVENT_INTERVAL"] = ti_rval;

  ti_rval = tiGetTriggerTableMode();
  if(ti_rval == ERROR)
    rval = ERROR;
  else
    ti_general_readback["TRIGGER_TABLE"] = ti_rval;

  int32_t fixed_type= 0, random_type = 0;
  ti_rval = tiGetPulserEventType(&fixed_type, &random_type);
  if(ti_rval == ERROR)
    rval = ERROR;
  else
    {
      ti_general_readback["FIXED_PULSER_EVENTTYPE"] = fixed_type;
      ti_general_readback["RANDOM_PULSER_EVENTTYPE"] = random_type;
    }

  ti_rval = tiGetFiberDelay();
  if(ti_rval == ERROR)
    rval = ERROR;
  else
    ti_general_readback["FIBER_SYNC_DELAY"] = ti_rval;

  /////////////////
  // SLAVES
  /////////////////

  //  loop through the slave bits gathered from the BUSY source mask above
  for(int32_t ibit = 0; ibit < 8; ibit++)
    {
      if(slave_bits & (1 << ibit))
	{
	  ti_slaves_readback["ENABLE_FIBER_" + std::to_string(ibit + 1)] = 1;
	}
    }


  /////////////////
  // TSINPUTS
  /////////////////
  ti_rval = tiGetTSInput();
  if(ti_rval == ERROR)
    rval = ERROR;
  else
    {
      for(int32_t ibit = 0; ibit < 6; ibit++)
	{
	  if(ti_rval & (1 << ibit))
	    {
	      ti_tsinputs_readback["ENABLE_TS" + std::to_string(ibit + 1)] = 1;
	    }
	}
    }

  for(int32_t inp = 1; inp <= 6; inp++)
    {
      ti_rval = tiGetInputPrescale(inp);
      if(ti_rval == ERROR)
	rval = ERROR;
      else
	{
	  ti_tsinputs_readback["PRESCALE_TS" + std::to_string(inp)] = ti_rval;
	}

      ti_rval = tiGetTSInputDelay(inp);
      if(ti_rval == ERROR)
	rval = ERROR;
      else
	{
	  ti_tsinputs_readback["DELAY_TS" + std::to_string(inp)] = ti_rval;
	}

    }

  /////////////////
  // TRIGGER RULES
  /////////////////
  int32_t slow_clock = 0;
  ti_rval = tiGetTriggerHoldoffClock();
  if(ti_rval == ERROR)
    rval = ERROR;
  else
    slow_clock = ti_rval;

  for(int32_t irule = 1; irule <=4; irule++)
    {
      ti_rval = tiGetTriggerHoldoff(irule);
      if(ti_rval == ERROR)
	rval = ERROR;
      else
	{
	  ti_rules_readback["RULE_" + std::to_string(irule)] = ti_rval & (0x7F);

	  int32_t timestep = (ti_rval & (1 << 7)) ? (1 + slow_clock) : 0;
	  ti_rules_readback["RULE_TIMESTEP_" + std::to_string(irule)] = timestep;
	}

      if(irule == 1)
	continue;

      ti_rval = tiGetTriggerHoldoffMin(irule, 0);
      if(ti_rval == ERROR)
	rval = ERROR;
      else
	{
	  ti_rules_readback["RULE_MIN_" + std::to_string(irule)] = ti_rval;
	}

    }

  return rval;
}

/**
 * @brief Write the Ini values to an output file
 */
int32_t
writeIni(const char* filename)
{
  if(ti2param() == ERROR)
    std::cerr << __func__ << ": ERROR: ti2param() returned ERROR" << std::endl;

  std::ofstream outFile;
  int32_t error;

  outFile.open(filename);
  if(!outFile)
    {
      std::cerr << __func__ << ": ERROR: Unable to open file for writting: " << filename << std::endl;
      return -1;
    }

  outFile << "[general]" << std::endl;

  ti_param_map::const_iterator pos = ti_general_def.begin();

  while(pos != ti_general_def.end())
    {
      if(ti_general_readback[pos->first] != -1)
	{
	  outFile << pos->first << "= " << ti_general_readback[pos->first] << std::endl;
	}

      ++pos;
    }

  outFile << "[slaves]" << std::endl;

  pos = ti_slaves_def.begin();
  while(pos != ti_slaves_def.end())
    {
      if(ti_slaves_readback[pos->first] != -1)
	{
	  outFile << pos->first << "= " << ti_slaves_readback[pos->first] << std::endl;
	}

      ++pos;
    }

  outFile << "[tsinputs]" << std::endl;

  pos = ti_tsinputs_def.begin();
  while(pos != ti_tsinputs_def.end())
    {
      if(ti_tsinputs_readback[pos->first] != -1)
	{
	  outFile << pos->first << "= " << ti_tsinputs_readback[pos->first] << std::endl;
	}

      ++pos;
    }

  outFile << "[trigger_rules]" << std::endl;

  pos = ti_rules_def.begin();
  while(pos != ti_rules_def.end())
    {
      if(ti_rules_readback[pos->first] != -1)
	{
	  outFile << pos->first << "= " << ti_rules_readback[pos->first] << std::endl;
	}

      ++pos;
    }




  outFile.close();
  return 0;

}

// destroy the ini object
int32_t
tiConfigFree()
{
#ifdef DEBUG
  std::cout << __func__ << ": INFO: here" << std::endl;
#endif

  if(ir == NULL)
    return ERROR;

#ifdef DEBUG
  std::cout << "delete ir" << std::endl;
#endif
  delete ir;

  return 0;
}
