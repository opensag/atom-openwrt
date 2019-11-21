/******************************************************************************

                               Copyright (c) 2012
                            Lantiq Deutschland GmbH

  For licensing information, see the file 'LICENSE' in the root folder of
  this software module.

******************************************************************************/
/*
 * $Id$
 *
 * 
 *
 * Module responsible for configuration.
 *
 * Authors: originaly written by Joel Isaacson;
 *  further development and support by: Andriy Tkachuk, Artem Migaev,
 *  Oleksandr Andrushchenko.
 *
 */

#include "mtlkinc.h"

#include "mhi_mac_event.h"
#include "mib_osdep.h"
#include "mtlkparams.h"
#include "scan.h"
#include "eeprom.h"
#include "drvver.h"
//#include "aocs.h" /*for dfs.h*/
#include "mtlkmib.h"
#include "mtlkhal.h"
#include "mtlkaux.h"
#include "mtlk_coreui.h"
#include "core_config.h"
#include "hw_mmb.h"

#define LOG_LOCAL_GID   GID_MIBS
#define LOG_LOCAL_FID   1


const mtlk_core_cfg_t def_card_cfg = 
{
  /* is hidden SSID */
  FALSE
};

/*****************************************************************************
**
** NAME         mtlk_mib_set_nic_cfg
**
** PARAMETERS   nic            Card context
**
** RETURNS      none
**
** DESCRIPTION  Fills the card configuration structure with user defined
**              values (or default ones)
**
******************************************************************************/
void mtlk_mib_set_nic_cfg (struct nic *nic)
{
  nic->slow_ctx->cfg = def_card_cfg;
}

int
mtlk_mib_set_pre_activate (struct nic *nic)
{
  MIB_VALUE uValue;

  int   res;
  uint8 freq_band_cfg;
  uint8 net_mode;
  wave_radio_t *radio = wave_vap_radio_get(nic->vap_handle);

  if (!mtlk_vap_is_master_ap(nic->vap_handle))
    return MTLK_ERR_OK;

  memset(&uValue, 0, sizeof(MIB_VALUE));

  freq_band_cfg = core_cfg_get_freq_band_cfg(nic);
  net_mode = get_net_mode(freq_band_cfg, TRUE);

  if (net_mode >= NUM_OF_NETWORK_MODES) {
    return MTLK_ERR_NO_ENTRY;
  }

  uValue.sPreActivateType.u8NetworkMode                    = net_mode;
  uValue.sPreActivateType.u8UpperLowerChannel              = WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_BONDING_MODE);
  uValue.sPreActivateType.u8SpectrumMode                   = WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_PROG_MODEL_SPECTRUM_MODE);
  uValue.sPreActivateType.u32OperationalRateSet            = HOST_TO_MAC32(get_operate_rate_set(net_mode));
  uValue.sPreActivateType.u32BSSbasicRateSet               = HOST_TO_MAC32(get_basic_rate_set(net_mode, CFG_BASIC_RATE_SET_DEFAULT));

  ILOG2_DDDDD("SENDING PREACTIVATION MIB:\n"
       "u8NetworkMode                       = %d\n"
       "u8UpperLowerChannel                 = %d\n"
       "u8SpectrumMode                      = %d\n"
       "u32OperationalRateSet               = %X\n"
       "u32BSSbasicRateSet                  = %X",
      uValue.sPreActivateType.u8NetworkMode,
      uValue.sPreActivateType.u8UpperLowerChannel,
      uValue.sPreActivateType.u8SpectrumMode,
      uValue.sPreActivateType.u32OperationalRateSet,
      uValue.sPreActivateType.u32BSSbasicRateSet);


  res = mtlk_set_mib_value_raw(mtlk_vap_get_txmm(nic->vap_handle),
                               MIB_PRE_ACTIVATE, &uValue);
  if (MTLK_ERR_OK != res) {
    ELOG_V("Failed to set MIB_PRE_ACTIVATE");
  }

  return res;
}

static int
mtlk_set_mib_values_ex (struct nic *nic, mtlk_txmm_msg_t* man_msg)
{
  mtlk_txmm_data_t* man_entry;
  mtlk_txmm_t *txmm = mtlk_vap_get_txmm(nic->vap_handle);
  wave_radio_t *radio = wave_vap_radio_get(nic->vap_handle);

  man_entry = mtlk_txmm_msg_get_empty_data(man_msg, txmm);
  if (!man_entry) {
    ELOG_V("Can't get MM data");
    return -ENOMEM;
  }

  ILOG2_V("Must do MIB's");

  /* MIB_IEEE_ADDRESS */
  if (mtlk_vap_is_master(nic->vap_handle)
      && !mtlk_vap_is_dut(nic->vap_handle)) {
    MIB_VALUE uValue;

    memset(&uValue, 0, sizeof(MIB_VALUE));
    MTLK_CORE_PDB_GET_MAC(nic, PARAM_DB_CORE_MAC_ADDR, uValue.au8ListOfu8.au8Elements);

    if (MTLK_ERR_OK != mtlk_set_mib_value_raw(mtlk_vap_get_txmm(nic->vap_handle), MIB_IEEE_ADDRESS, &uValue)) {
      ELOG_V("Failed to set MIB_IEEE_ADDRESS");
      return -ENODEV;
    }
  }

  if (mtlk_vap_is_master_ap(nic->vap_handle)) {
    if (MTLK_ERR_OK !=
        mtlk_set_mib_value_uint16(txmm, MIB_LONG_RETRY_LIMIT,
                                  WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_LONG_RETRY_LIMIT))) {

      ELOG_V("Failed to set MIB item");
      return -ENODEV;
    }

    /* Check if TPC close loop is ON and we have calibrations in EEPROM for selected frequency band */
    {
      if (TPC_CLOSED_LOOP == WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_TPC_LOOP_TYPE))
      {
          uint8 freq_band_cur = WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_FREQ_BAND_CUR);
          if (!mtlk_eeprom_is_band_supported(mtlk_core_get_eeprom(nic),freq_band_cur))
          {
              ELOG_S("TPC close loop is ON and no calibrations for current band (%s GHz) in EEPROM",
                     mtlk_eeprom_band_to_string(freq_band_cur));

              if (mtlk_hw_type_is_gen6(mtlk_vap_get_hw(nic->vap_handle))) {
                WLOG_V("Force TPC open loop");
                WAVE_RADIO_PDB_SET_INT(radio, PARAM_DB_RADIO_TPC_LOOP_TYPE, TPC_OPEN_LOOP);
              } else {
                mtlk_set_hw_state(nic, MTLK_HW_STATE_ERROR);
              }
          }
      }
    }

    // set driver features
    mtlk_set_mib_value_uint8(txmm, MIB_SPECTRUM_MODE, 
                             WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_PROG_MODEL_SPECTRUM_MODE));
  }

  ILOG2_V("End Mibs");
  return 0;
}

int
mtlk_set_vap_mibs (struct nic *nic)
{
  int             res       = -ENOMEM;
  mtlk_txmm_msg_t man_msg;

  /* The function sets all known FW MIBs excluding ones
     related with security and depended on channel & spectrum.
     Channel depended MIBs set after AOCS finished.
     Called just after VAP add, before core activation and on recovery.
  */
  if (mtlk_txmm_msg_init(&man_msg) == MTLK_ERR_OK) {
    res = mtlk_set_mib_values_ex(nic, &man_msg);
    mtlk_txmm_msg_cleanup(&man_msg);
  }
  else {
    ELOG_V("Can't init TXMM msg");
  }

  return res;
}

