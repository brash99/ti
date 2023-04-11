#include <cstring>
#include <iostream>
#include <iomanip>
#include <string>
#include <sstream>
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

static ti_param_map ti_general_ini;
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
    { "SYNCEVENT_INTERVAL", -1 },

    { "TRIGGER_TABLE", -1 },

    { "FIXED_PULSER_EVENTTYPE", -1 },
    { "RANDOM_PULSER_EVENTTYPE", -1 }
  };

static ti_param_map ti_slaves_ini;
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

static ti_param_map ti_tsinputs_ini;
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

static ti_param_map ti_rules_ini;
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



int32_t
tiConfigInitGlobals()
{
  std::cout << __func__ << ": INFO: here" << std::endl;

  return 0;
}


/**
 * @brief Write the Ini values to the local ti_param_map's
 */
void
parseIni()
{
  std::cout << __func__ << ": INFO: here" << std::endl;

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


}

/**
 * @brief Print the values stored in the local structure
 */
void
tiConfigPrintParameters()
{
  std::cout << __func__ << ": INFO: HERE" << std::endl;

  ti_param_map::const_iterator pos = ti_general_ini.begin();

  while(pos != ti_general_ini.end())
    {
      printf("  %28.24s = 0x%08x (%d)\n", pos->first.c_str(), pos->second, pos->second);
      ++pos;
    }

  pos = ti_slaves_ini.begin();

  while(pos != ti_slaves_ini.end())
    {
      printf("  %28.24s = 0x%08x (%d)\n", pos->first.c_str(), pos->second, pos->second);
      ++pos;
    }

  pos = ti_tsinputs_ini.begin();

  while(pos != ti_tsinputs_ini.end())
    {
      printf("  %28.24s = 0x%08x (%d)\n", pos->first.c_str(), pos->second, pos->second);
      ++pos;
    }

  pos = ti_rules_ini.begin();

  while(pos != ti_rules_ini.end())
    {
      printf("  %28.24s = 0x%08x (%d)\n", pos->first.c_str(), pos->second, pos->second);
      ++pos;
    }


}

/**
 * @brief Write the local parameter structure to the library
 * @return 0
 */
int32_t
param2ti()
{
  int32_t param_val = 0, rval = OK;
  ti_param_map::const_iterator pos;

#define CHECK_PARAM(__ini, __key)					\
  pos = __ini.find(__key);						\
  if(pos == __ini.end()) {						\
    std::cerr << __func__ << ": ERROR finding " << __key << std::endl;	\
    return ERROR;							\
  }

  /* Write the parameters to the device */
  CHECK_PARAM(ti_general_ini, "CRATE_ID");
  if(param_val > 0)
    {
      rval = tiSetCrateID(param_val);
      if(rval != OK)
	return ERROR;
    }

  CHECK_PARAM(ti_general_ini, "BLOCK_LEVEL");
  if(param_val > 0)
    {
      rval = tiSetBlockLevel(param_val);
      if(rval != OK)
	return ERROR;
    }

  CHECK_PARAM(ti_general_ini, "BLOCK_BUFFER_LEVEL");
  if(param_val > 0)
    {
      rval = tiSetBlockBufferLevel(param_val);
      if(rval != OK)
	return ERROR;
    }

  CHECK_PARAM(ti_general_ini, "INSTANT_BLOCKLEVEL_ENABLE");
  if(param_val > 0)
    {
      rval = tiSetInstantBlockLevelChange(param_val);
      if(rval != OK)
	return ERROR;
    }

  CHECK_PARAM(ti_general_ini, "BROADCAST_BUFFER_LEVEL_ENABLE");
  if(param_val > 0)
    {
      rval = tiUseBroadcastBufferLevel(param_val);
      if(rval != OK)
	return ERROR;
    }

  CHECK_PARAM(ti_general_ini, "BLOCK_LIMIT");
  if(param_val > 0)
    {
      rval = tiSetBlockLimit(param_val);
      if(rval != OK)
	return ERROR;
    }

  CHECK_PARAM(ti_general_ini, "TRIGGER_SOURCE");
  if(param_val > 0)
    {
      rval = tiSetTriggerSource(param_val);
      if(rval != OK)
	return ERROR;
    }

  CHECK_PARAM(ti_general_ini, "SYNC_SOURCE");
  if(param_val > 0)
    {
      rval = tiSetSyncSource(param_val);
      if(rval != OK)
	return ERROR;
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
      rval = tiSetBusySource(busy_source_mask, 1);
      if(rval != OK)
	return ERROR;
    }


  CHECK_PARAM(ti_general_ini, "CLOCK_SOURCE");
  if(param_val > 0)
    {
      rval = tiSetClockSource(param_val);
      if(rval != OK)
	return ERROR;
    }

  CHECK_PARAM(ti_general_ini, "PRESCALE");
  if(param_val > 0)
    {
      rval = tiSetPrescale(param_val);
      if(rval != OK)
	return ERROR;
    }

  CHECK_PARAM(ti_general_ini, "EVENT_FORMAT");
  if(param_val > 0)
    {
      rval = tiSetEventFormat(param_val);
      if(rval != OK)
	return ERROR;
    }

  CHECK_PARAM(ti_general_ini, "FP_INPUT_READOUT_ENABLE");
  if(param_val > 0)
    {
      rval = tiSetFPInputReadout(param_val);
      if(rval != OK)
	return ERROR;
    }

  CHECK_PARAM(ti_general_ini, "GO_OUTPUT_ENABLE");
  if(param_val > 0)
    {
      rval = tiSetGoOutput(param_val);
      if(rval != OK)
	return ERROR;
    }

  CHECK_PARAM(ti_general_ini, "TRIGGER_WINDOW");
  if(param_val > 0)
    {
      rval = tiSetTriggerWindow(param_val);
      if(rval != OK)
	return ERROR;
    }

  CHECK_PARAM(ti_general_ini, "TRIGGER_INHIBIT_WINDOW");
  if(param_val > 0)
    {
      rval = tiSetTriggerInhibitWindow(param_val);
      if(rval != OK)
	return ERROR;
    }

  CHECK_PARAM(ti_general_ini, "TRIGGER_LATCH_ON_LEVEL_ENABLE");
  if(param_val > 0)
    {
      rval = tiSetTriggerLatchOnLevel(param_val);
      if(rval != OK)
	return ERROR;
    }

  CHECK_PARAM(ti_general_ini, "TRIGGER_OUTPUT_DELAY");
  if(param_val > 0)
    {
      int32_t delay = param_val, width = 0, delaystep = 0;

      CHECK_PARAM(ti_general_ini, "TRIGGER_OUTPUT_WIDTH");
      width = param_val;

      CHECK_PARAM(ti_general_ini, "TRIGGER_OUTPUT_DELAYSTEP");
      delaystep = param_val;

      rval = tiSetTriggerPulse(1, delay, width, delaystep);
      if(rval != OK)
	return ERROR;
    }

  // FIXME: PROMPT_TRIGGER_WIDTH

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
      rval = tiSetEvTypeScalers(param_val);
      if(rval != OK)
	return ERROR;
    }

  CHECK_PARAM(ti_general_ini, "SCALER_MODE");
  if(param_val > 0)
    {
      rval = tiSetScalerMode(param_val, 0);
      if(rval != OK)
	return ERROR;

    }

  CHECK_PARAM(ti_general_ini, "SYNCEVENT_INTERVAL");
  if(param_val > 0)
    {
      rval = tiSetSyncEventInterval(param_val);
      if(rval != OK)
	return ERROR;

    }

  CHECK_PARAM(ti_general_ini, "TRIGGER_TABLE");
  if(param_val > 0)
    {
      rval = tiTriggerTablePredefinedConfig(param_val);
      if(rval != OK)
	return ERROR;

    }

  CHECK_PARAM(ti_general_ini, "FIXED_PULSER_EVENTTYPE");
  if(param_val > 0)
    {
      int32_t fixed = param_val, random = 0;

      CHECK_PARAM(ti_general_ini, "RANDOM_PULSER_EVENTTYPE");
      random = param_val;

      rval = tiDefinePulserEventType(fixed, random);
      if(rval != OK)
	return ERROR;

    }

  CHECK_PARAM(ti_slaves_ini, "ENABLE_FIBER_1");
  if(param_val > 0)
    {
      rval = tiAddSlave(1);
      if(rval != OK)
	return ERROR;
    }

  CHECK_PARAM(ti_slaves_ini, "ENABLE_FIBER_1");
  if(param_val > 0)
    {
      rval = tiAddSlave(1);
      if(rval != OK)
	return ERROR;
    }

  CHECK_PARAM(ti_slaves_ini, "ENABLE_FIBER_2");
  if(param_val > 0)
    {
      rval = tiAddSlave(2);
      if(rval != OK)
	return ERROR;
    }

  CHECK_PARAM(ti_slaves_ini, "ENABLE_FIBER_3");
  if(param_val > 0)
    {
      rval = tiAddSlave(3);
      if(rval != OK)
	return ERROR;
    }

  CHECK_PARAM(ti_slaves_ini, "ENABLE_FIBER_4");
  if(param_val > 0)
    {
      rval = tiAddSlave(4);
      if(rval != OK)
	return ERROR;
    }

  CHECK_PARAM(ti_slaves_ini, "ENABLE_FIBER_5");
  if(param_val > 0)
    {
      rval = tiAddSlave(5);
      if(rval != OK)
	return ERROR;
    }

  CHECK_PARAM(ti_slaves_ini, "ENABLE_FIBER_6");
  if(param_val > 0)
    {
      rval = tiAddSlave(6);
      if(rval != OK)
	return ERROR;
    }

  CHECK_PARAM(ti_slaves_ini, "ENABLE_FIBER_7");
  if(param_val > 0)
    {
      rval = tiAddSlave(7);
      if(rval != OK)
	return ERROR;
    }

  CHECK_PARAM(ti_slaves_ini, "ENABLE_FIBER_8");
  if(param_val > 0)
    {
      rval = tiAddSlave(8);
      if(rval != OK)
	return ERROR;
    }

#ifdef OLDWAY
  /* TS Inputs */
  uint32_t input_mask = 0;
  for(int32_t input = 0; input < 6; input++)
    {
      if(param.ts_inputs[input].enable > 0)
	{
	  input_mask |= (1 << input);

	  rval = tiSetTSInputDelay(input + 1, param.ts_inputs[input].delay);
	  if(rval != OK)
	    return ERROR;

	  rval = tiSetInputPrescale(input + 1, param.ts_inputs[input].prescale);
	  if(rval != OK)
	    return ERROR;
	}
    }

  if(input_mask != 0)
    {
      rval = tiEnableTSInput(input_mask);
      if(rval != OK)
	return ERROR;

    }

  /* Trigger Rules */
  for(int32_t irule = 0; irule < 4; irule++)
    {
      if(param.trigger_rules[irule].window > 0)
	{
	  rval = tiSetTriggerHoldoff(irule, param.trigger_rules[irule].window,
				     param.trigger_rules[irule].timestep);
	  if(rval != OK)
	    return ERROR;
	}

      if(param.trigger_rules[irule].minimum > 0)
	{
	  rval = tiSetTriggerHoldoffMin(irule, param.trigger_rules[irule].minimum);
	  if(rval != OK)
	    return ERROR;
	}
    }
#endif // OLDWAY

  return 0;
}

int32_t
tiConfigLoadParameters()
{
  if(ir == NULL)
    return 1;

  parseIni();

  param2ti();


  return 0;
}

// load in parameters to structure from filename
int32_t
tiConfig(const char *filename)
{
  std::cout << __func__ << ": INFO: here" << std::endl;

  ir = new INIReader(filename);
  if(ir->ParseError() < 0)
    {
      std::cout << "Can't load: " << filename << std::endl;
      return ERROR;
    }

  tiConfigLoadParameters();
  return 0;
}

// destroy the ini object
int32_t
tiConfigFree()
{
  std::cout << __func__ << ": INFO: here" << std::endl;

  if(ir == NULL)
    return ERROR;

  std::cout << "delete ir" << std::endl;
  delete ir;

  return 0;
}
