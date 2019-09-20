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
 * Radio's abilities management functionality
 *
 *
 */

#include "mtlkinc.h"
#include "mtlk_coreui.h"
#include "hw_mmb.h"
#include "wave_radio.h"
#include "mtlk_ab_manager.h"


#define LOG_LOCAL_GID   GID_WAVE_RADIO
#define LOG_LOCAL_FID   1

const mtlk_ability_id_t _wave_radio_abilities[] = {
  WAVE_RADIO_REQ_SET_TEST,
  WAVE_RADIO_REQ_GET_TEST
};

/*************************************************************************************************
 * Radio abilities
 *************************************************************************************************/
static const  mtlk_ability_id_t _radio_mbss_master_abilities[] = {
  WAVE_RADIO_REQ_MBSS_ADD_VAP_IDX,
  WAVE_RADIO_REQ_MBSS_ADD_VAP_NAME,
  WAVE_RADIO_REQ_MBSS_DEL_VAP_IDX,
  WAVE_RADIO_REQ_MBSS_DEL_VAP_NAME
};

static const mtlk_ability_id_t _radio_mac_watchdog_abilities[] = {
  WAVE_RADIO_REQ_GET_MAC_WATCHDOG_CFG,
  WAVE_RADIO_REQ_SET_MAC_WATCHDOG_CFG
};

static const mtlk_ability_id_t _radio_dot11d_abilities[] = {
  WAVE_RADIO_REQ_SET_DOT11D_CFG,
  WAVE_RADIO_REQ_GET_DOT11D_CFG
};

static const mtlk_ability_id_t _radio_general_abilities[] = {
  WAVE_RADIO_REQ_SET_COUNTRY_CFG,
  WAVE_RADIO_REQ_GET_COUNTRY_CFG,
  WAVE_RADIO_REQ_SET_MASTER_CFG,
  WAVE_RADIO_REQ_GET_MASTER_CFG,
  WAVE_RADIO_REQ_SET_FW_DEBUG,
  WAVE_RADIO_REQ_SET_TPC_CFG,
  WAVE_RADIO_REQ_GET_TPC_CFG,
  WAVE_RADIO_REQ_SET_SCAN_AND_CALIB_CFG,
  WAVE_RADIO_REQ_GET_SCAN_AND_CALIB_CFG,
  WAVE_RADIO_REQ_SET_DOT11H_AP_CFG,
  WAVE_RADIO_REQ_GET_DOT11H_AP_CFG,
  WAVE_RADIO_REQ_SET_QAMPLUS_MODE,
  WAVE_RADIO_REQ_GET_QAMPLUS_MODE,
  WAVE_RADIO_REQ_SET_RADIO_MODE,
  WAVE_RADIO_REQ_GET_RADIO_MODE,
  WAVE_RADIO_REQ_SET_AMSDU_NUM,
  WAVE_RADIO_REQ_GET_AMSDU_NUM,
  WAVE_RADIO_REQ_SET_CCA_THRESHOLD,
  WAVE_RADIO_REQ_GET_CCA_THRESHOLD,
  WAVE_RADIO_REQ_SET_CCA_ADAPTIVE,
  WAVE_RADIO_REQ_GET_CCA_ADAPTIVE,
  WAVE_RADIO_REQ_SET_RADAR_RSSI_TH,
  WAVE_RADIO_REQ_GET_RADAR_RSSI_TH,
  WAVE_RADIO_REQ_SET_IRE_CTRL_B,
  WAVE_RADIO_REQ_GET_IRE_CTRL_B,
  WAVE_RADIO_REQ_SET_COEX_CFG,
  WAVE_RADIO_REQ_GET_COEX_CFG,
  WAVE_RADIO_REQ_STA_CHANGE_BSS,
  WAVE_RADIO_REQ_SET_CHAN,
  WAVE_RADIO_REQ_DO_SCAN,
  WAVE_RADIO_REQ_START_SCHED_SCAN,
  WAVE_RADIO_REQ_STOP_SCHED_SCAN,
  WAVE_RADIO_REQ_SCAN_TIMEOUT,
  WAVE_RADIO_REQ_ALTER_SCAN,
  WAVE_RADIO_REQ_DUMP_SURVEY,
  WAVE_RADIO_REQ_FIN_PREV_FW_SC,
  WAVE_RADIO_REQ_SET_FW_LOG_SEVERITY,
  WAVE_RADIO_REQ_GET_RADIO_INFO,
  WAVE_RADIO_REQ_SET_FIXED_LTF_AND_GI,
  WAVE_RADIO_REQ_GET_FIXED_LTF_AND_GI,
  WAVE_RADIO_REQ_CAC_TIMEOUT,
  WAVE_RADIO_REQ_GET_RTS_RATE,
  WAVE_RADIO_REQ_SET_RTS_RATE,
  WAVE_RADIO_REQ_SET_NFRP_CFG,
  WAVE_RADIO_REQ_GET_CDB_CFG,
};

static const mtlk_ability_id_t _radio_ap_general_abilities[] = {
  WAVE_RADIO_REQ_GET_RX_DUTY_CYCLE,
  WAVE_RADIO_REQ_SET_RX_DUTY_CYCLE,
  WAVE_RADIO_REQ_GET_11B_CFG,
  WAVE_RADIO_REQ_SET_DBG_CLI,
  WAVE_HW_REQ_GET_AP_CAPABILITIES,
};

static const mtlk_ability_id_t _radio_hw_data_abilities[] = {
  WAVE_RADIO_REQ_GET_HW_LIMITS,
#ifdef MTLK_LEGACY_STATISTICS
  WAVE_RADIO_REQ_GET_MHI_STATS,
#endif
  WAVE_RADIO_REQ_GET_PHY_RX_STATUS,
  WAVE_RADIO_REQ_GET_AGG_RATE_LIMIT,
  WAVE_RADIO_REQ_SET_AGG_RATE_LIMIT,
  WAVE_RADIO_REQ_GET_TX_POWER_LIMIT_OFFSET,
  WAVE_RADIO_REQ_SET_TX_POWER_LIMIT_OFFSET,
  WAVE_RADIO_REQ_GET_TX_RATE_POWER,
#ifdef CPTCFG_IWLWAV_SET_PM_QOS
  WAVE_RADIO_REQ_GET_CPU_DMA_LATENCY,
  WAVE_RADIO_REQ_SET_CPU_DMA_LATENCY,
#endif
  WAVE_RADIO_REQ_GET_MAX_MPDU_LENGTH,
  WAVE_RADIO_REQ_SET_MAX_MPDU_LENGTH,
  WAVE_RADIO_REQ_SET_FIXED_RATE,
  WAVE_RADIO_SET_STATIC_PLAN,
  WAVE_RADIO_REQ_GET_IRE_STAND_ALONE,
  WAVE_RADIO_REQ_SET_IRE_STAND_ALONE,
  WAVE_RADIO_REQ_SET_SSB_MODE,
  WAVE_RADIO_REQ_GET_SSB_MODE,
  WAVE_RADIO_REQ_SET_AX_CONFIG,
  WAVE_RADIO_REQ_GET_AX_CONFIG,
  WAVE_RADIO_REQ_SET_FREQ_JUMP_MODE,
  WAVE_RADIO_REQ_SET_RESTRICTED_AC_MODE,
  WAVE_RADIO_REQ_GET_RESTRICTED_AC_MODE,
  WAVE_HW_REQ_GET_BF_EXPLICIT_CAP,
  WAVE_HW_REQ_SET_TEMPERATURE_SENSOR,
  WAVE_HW_REQ_GET_TEMPERATURE_SENSOR,
  WAVE_HW_REQ_GET_TASKLET_LIMITS,
  WAVE_HW_REQ_SET_TASKLET_LIMITS,
  MTLK_HW_REQ_GET_COUNTERS_SRC,
  MTLK_HW_REQ_SET_COUNTERS_SRC,
  WAVE_HW_REQ_SET_DYNAMIC_MU_CFG,
  WAVE_HW_REQ_GET_DYNAMIC_MU_CFG,
  WAVE_RADIO_REQ_GET_PHY_INBAND_POWER,
};

static const mtlk_ability_id_t _hw_card_gen6_abilities[] = {
  WAVE_HW_REQ_GET_PVT_SENSOR,
  WAVE_HW_REQ_SET_TEST_BUS,
};

static const mtlk_ability_id_t _radio_mibs_abilities[] = {
  WAVE_RADIO_REQ_SET_MIBS_CFG,
  WAVE_RADIO_REQ_GET_MIBS_CFG
};

static const mtlk_ability_id_t _radio_bcl_abilities[] = {
  WAVE_RADIO_REQ_SET_MAC_ASSERT,
  WAVE_RADIO_REQ_GET_BCL_MAC_DATA,
  WAVE_RADIO_REQ_SET_BCL_MAC_DATA
};

static const mtlk_ability_id_t _radio_interfdet_abilities[] = {
  WAVE_RADIO_REQ_SET_INTERFDET_PARAMS_CFG,
  WAVE_RADIO_REQ_GET_INTERFDET_MODE_CFG
};

static const mtlk_ability_id_t _radio_ta_abilities[] = {
  WAVE_RADIO_REQ_GET_TA_CFG,
  WAVE_RADIO_REQ_SET_TA_CFG
};

static const mtlk_ability_id_t _radio_11b_abilities[] = {
  WAVE_RADIO_REQ_SET_11B_CFG,
};

static const mtlk_ability_id_t _radio_recovery_abilities[] = {
  WAVE_RADIO_REQ_SET_RECOVERY_CFG,
  WAVE_RADIO_REQ_GET_RECOVERY_CFG,
  WAVE_RADIO_REQ_GET_RECOVERY_STATS,
  WAVE_RCVRY_RESET
};

static const mtlk_ability_id_t _radio_calibration_abilities[] = {
  WAVE_RADIO_REQ_GET_CALIBRATION_MASK,
  WAVE_RADIO_REQ_SET_CALIBRATION_MASK
};

/*************************************************************************************************
 *************************************************************************************************/
static const mtlk_ab_tbl_item_t wave_radio_abilities_master_ap_init_tbl[]= {
    { _radio_dot11d_abilities,             ARRAY_SIZE(_radio_dot11d_abilities)},
    { _radio_mibs_abilities,               ARRAY_SIZE(_radio_mibs_abilities)},
    { _radio_mac_watchdog_abilities,       ARRAY_SIZE(_radio_mac_watchdog_abilities)},
    { _radio_bcl_abilities,                ARRAY_SIZE(_radio_bcl_abilities)},
    { _radio_hw_data_abilities,            ARRAY_SIZE(_radio_hw_data_abilities)},
    { _radio_general_abilities,            ARRAY_SIZE(_radio_general_abilities)},
    { _radio_ap_general_abilities,         ARRAY_SIZE(_radio_ap_general_abilities)},
    { _radio_mbss_master_abilities,        ARRAY_SIZE(_radio_mbss_master_abilities)},
    { _radio_interfdet_abilities,          ARRAY_SIZE(_radio_interfdet_abilities)},
    { _radio_ta_abilities,                 ARRAY_SIZE(_radio_ta_abilities)},
    { _radio_11b_abilities,                ARRAY_SIZE(_radio_11b_abilities)},
    { _radio_recovery_abilities,           ARRAY_SIZE(_radio_recovery_abilities)},
    { _radio_calibration_abilities,        ARRAY_SIZE(_radio_calibration_abilities)},
    { NULL, 0},
};


/*************************************************************************************************
 * Radio's abilities management implementation
 *************************************************************************************************/
int
wave_radio_abilities_register(mtlk_core_t *core)
{
  int res = MTLK_ERR_WRONG_CONTEXT;
  const mtlk_ab_tbl_item_t* ab_tabl;
  mtlk_abmgr_t* abmgr = mtlk_vap_get_abmgr(core->vap_handle);
  const mtlk_ab_tbl_item_t* curr_item;
  wave_radio_t  *radio = NULL;

  if (!mtlk_vap_is_master_ap(core->vap_handle)) {
    WLOG_V("Attempt to register radio ability in a wrong context!");
    return res;
  }

  radio = wave_vap_radio_get(core->vap_handle);
  if (!radio) {
    return MTLK_ERR_UNKNOWN;
  }

  res = MTLK_ERR_OK;

  ab_tabl = wave_radio_abilities_master_ap_init_tbl;

  for(curr_item = ab_tabl; NULL != curr_item->abilities; ++curr_item) {
    res = mtlk_abmgr_register_ability_set(abmgr, curr_item->abilities, curr_item->num_elems);
    if (MTLK_ERR_OK != res) {
      const mtlk_ab_tbl_item_t* rollback_item;
      for(rollback_item = ab_tabl; rollback_item != curr_item; ++rollback_item) {
        mtlk_abmgr_disable_ability_set(abmgr,  rollback_item->abilities, rollback_item->num_elems);
        mtlk_abmgr_unregister_ability_set(abmgr,  rollback_item->abilities, rollback_item->num_elems);
      }
      break;
    }
    mtlk_abmgr_enable_ability_set(abmgr, curr_item->abilities, curr_item->num_elems);
  }

  if (MTLK_ERR_OK != res) {
    return res;
  }

  /* Gen6 specific */
  if (wave_radio_is_gen6(radio)) {
    /* HW card abilities are for the first radio */
    if (wave_radio_is_first(radio)) {
      res = mtlk_abmgr_register_ability_set(abmgr, _hw_card_gen6_abilities, ARRAY_SIZE(_hw_card_gen6_abilities));
      if (MTLK_ERR_OK == res) {
        mtlk_abmgr_enable_ability_set(abmgr, _hw_card_gen6_abilities, ARRAY_SIZE(_hw_card_gen6_abilities));
      }
    }
  }

  return res;
}

void
wave_radio_abilities_unregister(mtlk_core_t *core)
{
  const mtlk_ab_tbl_item_t* ab_tabl;
  mtlk_abmgr_t* abmgr = mtlk_vap_get_abmgr(core->vap_handle);
  wave_radio_t  *radio = NULL;

  if (!mtlk_vap_is_master_ap(core->vap_handle)) {
      return;
  }

  radio = wave_vap_radio_get(core->vap_handle);
  if (!radio) {
    return;
  }

  ab_tabl = wave_radio_abilities_master_ap_init_tbl;

  for (; NULL != ab_tabl->abilities; ++ab_tabl) {
    mtlk_abmgr_disable_ability_set(abmgr, ab_tabl->abilities, ab_tabl->num_elems);
    mtlk_abmgr_unregister_ability_set(abmgr, ab_tabl->abilities, ab_tabl->num_elems);
  }

  /* Gen6 specific */
  if (wave_radio_is_gen6(radio)) {
    /* HW card abilities are for the first radio */
    if (wave_radio_is_first(radio)) {
      mtlk_abmgr_disable_ability_set(abmgr, _hw_card_gen6_abilities, ARRAY_SIZE(_hw_card_gen6_abilities));
      mtlk_abmgr_unregister_ability_set(abmgr, _hw_card_gen6_abilities, ARRAY_SIZE(_hw_card_gen6_abilities));
    }
  }
}

void
wave_radio_abilities_disable_vap_ops (mtlk_vap_handle_t vap_handle)
{
  mtlk_abmgr_disable_ability_set(mtlk_vap_get_abmgr(vap_handle), _radio_mbss_master_abilities,
                                                            ARRAY_SIZE(_radio_mbss_master_abilities));
}

void
wave_radio_abilities_enable_vap_ops (mtlk_vap_handle_t vap_handle)
{
  mtlk_abmgr_enable_ability_set(mtlk_vap_get_abmgr(vap_handle), _radio_mbss_master_abilities,
                                                            ARRAY_SIZE(_radio_mbss_master_abilities));
}

void
wave_radio_abilities_disable_11b_abilities (mtlk_vap_handle_t vap_handle)
{
  mtlk_abmgr_disable_ability_set(mtlk_vap_get_abmgr(vap_handle), _radio_11b_abilities,
                                 ARRAY_SIZE(_radio_11b_abilities));
}

void
wave_radio_abilities_enable_11b_abilities (mtlk_vap_handle_t vap_handle)
{
  mtlk_abmgr_enable_ability_set(mtlk_vap_get_abmgr(vap_handle), _radio_11b_abilities,
                                ARRAY_SIZE(_radio_11b_abilities));
}
