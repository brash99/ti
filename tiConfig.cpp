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

static ti_param_t param;
static ti_param_t defparam =
  {
    .crate_id = -1,
    .block_level = 1,
    .block_buffer_level = 1,
    .instant_block_level_enable = -1,
    .broadcast_buffer_level_enable = -1,
    .block_limit = -1,

    .trigger_source = -1,
    .sync_source = -1,
    .busy_source = {0,0,0,0,0,0,0,{0,0,0,0,0,0,0,0}},
    .clock_source = -1,

    .prescale = -1,
    .event_format = -1,
    .fp_input_readout_enable = 1,
    .go_output_enable = -1,

    .trigger_window = -1,
    .trigger_inhibit_window = -1,
    .trigger_latch_on_level_enable = -1,

    .trigger_output_delay = -1,
    .trigger_output_delaystep = -1,
    .trigger_output_width = -1,

    .prompt_trigger_width = -1,

    .syncreset_delay = -1,
    .syncreset_width = -1,
    .syncreset_widthstep = -1,

    .eventtype_scalers_enable = -1,
    .scaler_mode = -1,
    .syncevent_interval = -1,

    .trigger_table = -1,

    .fixed_pulser_eventtype = 0xFD,
    .random_pulser_eventtype = 0xFE,

    .fiber_enable = {-1,-1,-1,-1,-1,-1,-1,-1},
    .ts_inputs = {{-1, -1, -1},{-1, -1, -1},{-1, -1, -1},{-1, -1, -1},{-1, -1, -1},{-1, -1, -1}},
    .trigger_rules = {{-1, 0, -1},{-1, 0, -1},{-1, 0, -1},{-1, 0, -1}},

  };


int32_t
tiConfigInitGlobals()
{
  std::cout << __func__ << ": INFO: here" << std::endl;

  memset(&param, 0, sizeof(param));

  return 0;
}


/**
 * @brief Write the Ini values to the local module structure
 */
void
parseIni()
{
  ti_param_t *ini = &param;

  if(ir == NULL)
    return;

  //
  // Module parameters
  //
  int32_t var;
#define __GETINT(__ini_sec, __ini_var, __svar)		\
  var = ir->GetInteger(__ini_sec, __ini_var, -1);	\
  if(var == -1)						\
    ini->__svar = defparam.__svar;			\
  else							\
    ini->__svar = var;

  __GETINT("general", "CRATE_ID", crate_id);
  __GETINT("general", "BLOCK_LEVEL", block_level);
  __GETINT("general", "BLOCK_BUFFER_LEVEL", block_buffer_level);

  __GETINT("general", "INSTANT_BLOCKLEVEL_ENABLE", instant_block_level_enable);
  __GETINT("general", "BROADCAST_BUFFER_LEVEL_ENABLE", broadcast_buffer_level_enable);

  __GETINT("general", "BLOCK_LIMIT", block_limit);

  __GETINT("general", "TRIGGER_SOURCE", trigger_source);

  __GETINT("general", "SYNC_SOURCE", sync_source);

  __GETINT("general", "BUSY_SOURCE_SWA", busy_source.swa);
  __GETINT("general", "BUSY_SOURCE_SWB", busy_source.swb);
  __GETINT("general", "BUSY_SOURCE_P2", busy_source.p2);
  __GETINT("general", "BUSY_SOURCE_FP_TDC", busy_source.fp_tdc);
  __GETINT("general", "BUSY_SOURCE_FP_FADC", busy_source.fp_fadc);
  __GETINT("general", "BUSY_SOURCE_FP", busy_source.fp);
  __GETINT("general", "BUSY_SOURCE_LOOPBACK", busy_source.loopback);

  __GETINT("general", "BUSY_SOURCE_FIBER1", busy_source.fiber[0]);
  __GETINT("general", "BUSY_SOURCE_FIBER2", busy_source.fiber[1]);
  __GETINT("general", "BUSY_SOURCE_FIBER3", busy_source.fiber[2]);
  __GETINT("general", "BUSY_SOURCE_FIBER4", busy_source.fiber[3]);
  __GETINT("general", "BUSY_SOURCE_FIBER5", busy_source.fiber[4]);
  __GETINT("general", "BUSY_SOURCE_FIBER6", busy_source.fiber[5]);
  __GETINT("general", "BUSY_SOURCE_FIBER7", busy_source.fiber[6]);
  __GETINT("general", "BUSY_SOURCE_FIBER8", busy_source.fiber[7]);

  __GETINT("general", "CLOCK_SOURCE", clock_source);

  __GETINT("general", "PRESCALE", prescale);
  __GETINT("general", "EVENT_FORMAT", event_format);
  __GETINT("general", "FP_INPUT_READOUT_ENABLE", fp_input_readout_enable);

  __GETINT("general", "GO_OUTPUT_ENABLE", go_output_enable);

  __GETINT("general", "TRIGGER_WINDOW", trigger_window);
  __GETINT("general", "TRIGGER_INHIBIT_WINDOW", trigger_inhibit_window);

  __GETINT("general", "TRIGGER_LATCH_ON_LEVEL_ENABLE", trigger_latch_on_level_enable);

  __GETINT("general", "TRIGGER_OUTPUT_DELAY", trigger_output_delay);
  __GETINT("general", "TRIGGER_OUTPUT_DELAYSTEP", trigger_output_delaystep);
  __GETINT("general", "TRIGGER_OUTPUT_WIDTH", trigger_output_width);

  __GETINT("general", "PROMPT_TRIGGER_WIDTH", prompt_trigger_width);

  __GETINT("general", "SYNCRESET_DELAY", syncreset_delay);
  __GETINT("general", "SYNCRESET_WIDTH", syncreset_width);

  __GETINT("general", "EVENTTYPE_SCALERS_ENABLE", eventtype_scalers_enable);
  __GETINT("general", "SCALER_MODE", scaler_mode);
  __GETINT("general", "SYNCEVENT_INTERVAL", syncevent_interval);

  __GETINT("general", "TRIGGER_TABLE", trigger_table);

  __GETINT("general", "FIXED_PULSER_EVENTTYPE", fixed_pulser_eventtype);
  __GETINT("general", "RANDOM_PULSER_EVENTTYPE", random_pulser_eventtype);

  __GETINT("slaves", "ENABLE_FIBER_1", fiber_enable[0]);
  __GETINT("slaves", "ENABLE_FIBER_2", fiber_enable[1]);
  __GETINT("slaves", "ENABLE_FIBER_3", fiber_enable[2]);
  __GETINT("slaves", "ENABLE_FIBER_4", fiber_enable[3]);
  __GETINT("slaves", "ENABLE_FIBER_5", fiber_enable[4]);
  __GETINT("slaves", "ENABLE_FIBER_6", fiber_enable[5]);
  __GETINT("slaves", "ENABLE_FIBER_7", fiber_enable[6]);
  __GETINT("slaves", "ENABLE_FIBER_8", fiber_enable[7]);

  __GETINT("tsinputs", "ENABLE_TS1", ts_inputs[0].enable);
  __GETINT("tsinputs", "ENABLE_TS2", ts_inputs[1].enable);
  __GETINT("tsinputs", "ENABLE_TS3", ts_inputs[2].enable);
  __GETINT("tsinputs", "ENABLE_TS4", ts_inputs[3].enable);
  __GETINT("tsinputs", "ENABLE_TS5", ts_inputs[4].enable);
  __GETINT("tsinputs", "ENABLE_TS6", ts_inputs[5].enable);

  __GETINT("tsinputs", "PRESCALE_TS1", ts_inputs[0].prescale);
  __GETINT("tsinputs", "PRESCALE_TS2", ts_inputs[1].prescale);
  __GETINT("tsinputs", "PRESCALE_TS3", ts_inputs[2].prescale);
  __GETINT("tsinputs", "PRESCALE_TS4", ts_inputs[3].prescale);
  __GETINT("tsinputs", "PRESCALE_TS5", ts_inputs[4].prescale);
  __GETINT("tsinputs", "PRESCALE_TS6", ts_inputs[5].prescale);

  __GETINT("tsinputs", "DELAY_TS1", ts_inputs[0].delay);
  __GETINT("tsinputs", "DELAY_TS2", ts_inputs[1].delay);
  __GETINT("tsinputs", "DELAY_TS3", ts_inputs[2].delay);
  __GETINT("tsinputs", "DELAY_TS4", ts_inputs[3].delay);
  __GETINT("tsinputs", "DELAY_TS5", ts_inputs[4].delay);
  __GETINT("tsinputs", "DELAY_TS6", ts_inputs[5].delay);

  __GETINT("trigger_rules", "RULE_1", trigger_rules[0].window);
  __GETINT("trigger_rules", "RULE_TIMESTEP_1", trigger_rules[0].timestep);
  __GETINT("trigger_rules", "RULE_MIN_1", trigger_rules[0].minimum);

  __GETINT("trigger_rules", "RULE_2", trigger_rules[1].window);
  __GETINT("trigger_rules", "RULE_TIMESTEP_2", trigger_rules[1].timestep);
  __GETINT("trigger_rules", "RULE_MIN_2", trigger_rules[1].minimum);

  __GETINT("trigger_rules", "RULE_3", trigger_rules[2].window);
  __GETINT("trigger_rules", "RULE_TIMESTEP_3", trigger_rules[2].timestep);
  __GETINT("trigger_rules", "RULE_MIN_3", trigger_rules[2].minimum);

  __GETINT("trigger_rules", "RULE_4", trigger_rules[3].window);
  __GETINT("trigger_rules", "RULE_TIMESTEP_4", trigger_rules[3].timestep);
  __GETINT("trigger_rules", "RULE_MIN_4", trigger_rules[3].minimum);

}

/**
 * @brief Print the values stored in the local structure
 */
void
tiConfigPrintParameters()
{
  ti_param_t *sp;

  sp = &param;

#ifndef PRINTPARAM
#define PRINTPARAM(_reg)						\
  printf("  %28.24s = 0x%08x (%d)\n",					\
	 #_reg, sp->_reg, sp->_reg);
#endif

  PRINTPARAM(crate_id);
  PRINTPARAM(block_level);
  PRINTPARAM(block_buffer_level);
  PRINTPARAM(instant_block_level_enable);
  PRINTPARAM(broadcast_buffer_level_enable);
  PRINTPARAM(block_limit);
  PRINTPARAM(trigger_source);
  PRINTPARAM(sync_source);
  PRINTPARAM(busy_source.swa);
  PRINTPARAM(busy_source.swb);
  PRINTPARAM(busy_source.p2);
  PRINTPARAM(busy_source.fp_tdc);
  PRINTPARAM(busy_source.fp_fadc);
  PRINTPARAM(busy_source.fp);
  PRINTPARAM(busy_source.loopback);
  PRINTPARAM(busy_source.fiber[0]);
  PRINTPARAM(busy_source.fiber[1]);
  PRINTPARAM(busy_source.fiber[2]);
  PRINTPARAM(busy_source.fiber[3]);
  PRINTPARAM(busy_source.fiber[4]);
  PRINTPARAM(busy_source.fiber[5]);
  PRINTPARAM(busy_source.fiber[6]);
  PRINTPARAM(busy_source.fiber[7]);

  PRINTPARAM(clock_source);

  PRINTPARAM(prescale);
  PRINTPARAM(event_format);
  PRINTPARAM(fp_input_readout_enable);
  PRINTPARAM(go_output_enable);

  PRINTPARAM(trigger_window);
  PRINTPARAM(trigger_inhibit_window);
  PRINTPARAM(trigger_latch_on_level_enable);

  PRINTPARAM(trigger_output_delay);
  PRINTPARAM(trigger_output_delaystep);
  PRINTPARAM(trigger_output_width);

  PRINTPARAM(prompt_trigger_width);

  PRINTPARAM(syncreset_delay);
  PRINTPARAM(syncreset_width);

  PRINTPARAM(eventtype_scalers_enable);
  PRINTPARAM(scaler_mode);
  PRINTPARAM(syncevent_interval);

  PRINTPARAM(trigger_table);

  PRINTPARAM(fixed_pulser_eventtype);
  PRINTPARAM(random_pulser_eventtype);

  for(int ifiber = 0; ifiber < 8; ifiber++)
    PRINTPARAM(fiber_enable[ifiber]);

  for(int input = 0; input < 6; input++)
    {
      PRINTPARAM(ts_inputs[input].enable);
      PRINTPARAM(ts_inputs[input].prescale);
      PRINTPARAM(ts_inputs[input].delay);
    }

  for(int irule = 0; irule < 4; irule++)
    {
      PRINTPARAM(trigger_rules[irule].window);
      PRINTPARAM(trigger_rules[irule].timestep);
      PRINTPARAM(trigger_rules[irule].minimum);
    }

}

/**
 * @brief Write the local parameter structure to the library
 * @return 0
 */
int32_t
param2ti()
{
  int32_t rval = OK;

  /* Write the parameters to the device */
  if(param.crate_id > 0)
    {
      rval = tiSetCrateID(param.crate_id);
      if(rval != OK)
	return ERROR;
    }

  if(param.block_level > 0)
    {
      rval = tiSetBlockLevel(param.block_level);
      if(rval != OK)
	return ERROR;
    }

  if(param.block_buffer_level > 0)
    {
      rval = tiSetBlockBufferLevel(param.block_buffer_level);
      if(rval != OK)
	return ERROR;
    }

  if(param.trigger_source > 0)
    {
      rval = tiSetTriggerSource(param.trigger_source);
      if(rval != OK)
	return ERROR;
    }

  if(param.instant_block_level_enable > 0)
    {
      rval = tiSetInstantBlockLevelChange(param.instant_block_level_enable);
      if(rval != OK)
	return ERROR;
    }

  if(param.broadcast_buffer_level_enable > 0)
    {
      rval = tiUseBroadcastBufferLevel(param.broadcast_buffer_level_enable);
      if(rval != OK)
	return ERROR;
    }

  if(param.block_limit > 0)
    {
      rval = tiSetBlockLimit(param.block_limit);
      if(rval != OK)
	return ERROR;
    }

  if(param.trigger_source > 0)
    {
      rval = tiSetTriggerSource(param.trigger_source);
      if(rval != OK)
	return ERROR;
    }

  if(param.sync_source > 0)
    {
      rval = tiSetSyncSource(param.sync_source);
      if(rval != OK)
	return ERROR;
    }

  /* Busy Source, build a busy source mask */
  uint32_t busy_source_mask = 0;

  if(param.busy_source.swa > 0)
    busy_source_mask |= TI_BUSY_SWA;

  if(param.busy_source.swb > 0)
    busy_source_mask |= TI_BUSY_SWB;

  if(param.busy_source.fp_tdc > 0)
    busy_source_mask |= TI_BUSY_FP_FTDC;

  if(param.busy_source.fp_fadc > 0)
    busy_source_mask |= TI_BUSY_FP_FADC;

  if(param.busy_source.fp > 0)
    busy_source_mask |= TI_BUSY_FP;

  if(param.busy_source.loopback > 0)
    busy_source_mask |= TI_BUSY_LOOPBACK;

  for(int32_t ifiber = 0; ifiber < 8 ; ifiber++)
    {
      if(param.busy_source.fiber[ifiber] > 0)
	busy_source_mask |= (TI_BUSY_HFBR1 << ifiber);
    }

  if(busy_source_mask != 0)
    {
      rval = tiSetBusySource(busy_source_mask, 1);
      if(rval != OK)
	return ERROR;
    }


  if(param.clock_source > 0)
    {
      rval = tiSetClockSource(param.clock_source);
      if(rval != OK)
	return ERROR;
    }

  if(param.prescale > 0)
    {
      rval = tiSetPrescale(param.prescale);
      if(rval != OK)
	return ERROR;
    }

  if(param.event_format > 0)
    {
      rval = tiSetEventFormat(param.event_format);
      if(rval != OK)
	return ERROR;
    }

  if(param.fp_input_readout_enable > 0)
    {
      rval = tiSetFPInputReadout(param.fp_input_readout_enable);
      if(rval != OK)
	return ERROR;
    }

  if(param.go_output_enable > 0)
    {
      rval = tiSetGoOutput(param.go_output_enable);
      if(rval != OK)
	return ERROR;
    }

  if(param.trigger_window > 0)
    {
      rval = tiSetTriggerWindow(param.trigger_window);
      if(rval != OK)
	return ERROR;
    }

  if(param.trigger_inhibit_window > 0)
    {
      rval = tiSetTriggerInhibitWindow(param.trigger_inhibit_window);
      if(rval != OK)
	return ERROR;
    }

  if(param.trigger_latch_on_level_enable > 0)
    {
      rval = tiSetTriggerLatchOnLevel(param.trigger_latch_on_level_enable);
      if(rval != OK)
	return ERROR;
    }

  if(param.trigger_output_delay > 0)
    {
      rval = tiSetTriggerPulse(1, param.trigger_output_delay,
			       param.trigger_output_width, param.trigger_output_delaystep);
      if(rval != OK)
	return ERROR;
    }

  if(param.syncreset_delay > 0)
    {
      tiSetSyncDelayWidth(param.syncreset_delay, param.syncreset_width,
				 param.syncreset_widthstep);
    }

  if(param.eventtype_scalers_enable > 0)
    {
      rval = tiSetEvTypeScalers(param.eventtype_scalers_enable);
      if(rval != OK)
	return ERROR;
    }

  if(param.scaler_mode > 0)
    {
      rval = tiSetScalerMode(param.scaler_mode, 0);
      if(rval != OK)
	return ERROR;

    }

  if(param.syncevent_interval > 0)
    {
      rval = tiSetSyncEventInterval(param.syncevent_interval);
      if(rval != OK)
	return ERROR;

    }

  if(param.trigger_table > 0)
    {
      rval = tiTriggerTablePredefinedConfig(param.trigger_table);
      if(rval != OK)
	return ERROR;

    }

  if(param.fixed_pulser_eventtype > 0)
    {
      rval = tiDefinePulserEventType(param.fixed_pulser_eventtype, param.random_pulser_eventtype);
      if(rval != OK)
	return ERROR;

    }

  /* Slave Configuration */
  for(int32_t ifiber = 0; ifiber < 8 ; ifiber++)
    {
      if(param.fiber_enable[ifiber] > 0)
	{
	  rval = tiAddSlave(ifiber + 1);
	  if(rval != OK)
	    return ERROR;
	}
    }

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
      return 1;
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
    return 1;

  std::cout << "delete ir" << std::endl;
  delete ir;

  return 0;
}
