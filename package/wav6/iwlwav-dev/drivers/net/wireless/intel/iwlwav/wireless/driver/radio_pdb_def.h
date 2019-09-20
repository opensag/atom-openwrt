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
 * Radio's parameters DB definitions
 *
 * Written by: Grygorii Strashko
 *
 */

#ifndef __RADIO_PDB_DEF_H_
#define __RADIO_PDB_DEF_H_

#include "mtlk_param_db.h"
#include "channels.h"
#include "mtlkaux.h"
#include "core_priv.h"
#include "mtlk_coreui.h"
#include "mtlk_psdb.h"
#include "scan_support.h"

#define MTLK_IDEFS_ON
#include "mtlkidefs.h"

/* same for all cards */
#define WAVE_RADIO_CALIBRATION_MASK_DEFAULT (uint32)(-1)

static const uint32 wave_radio_initial_zero_int = 0;

/* Interference Detection */
static const uint32 wave_radio_initial_interfdet_mode = FALSE;
static const uint32 wave_radio_initial_spectrum_mode = CW_40;
static const uint32 wave_radio_initial_bonding_mode = ALTERNATE_LOWER;
static const uint32 wave_radio_initial_channel = 0;
static const mtlk_channel_prev_cfg_t wave_radio_initial_channel_prev = { MTLK_CHANNEL_PREV_CFG_PRI_NUM_DEFAULT, 0 };
static const uint32 wave_radio_initial_power_selection = 0;
static const uint32 wave_radio_initial_mac_soft_reset_enable = FALSE;
static const uint32 wave_radio_initial_dot11d_enabled = TRUE;
static const uint32 wave_radio_initial_mac_watchdog_timeout_ms = MAC_WATCHDOG_DEFAULT_TIMEOUT_MS;
static const uint32 wave_radio_initial_mac_watchdog_period_ms = MAC_WATCHDOG_DEFAULT_PERIOD_MS;
static const uint32 wave_radio_initial_frequency_band_cfg = MTLK_HW_BAND_2_4_GHZ;
static const uint32 wave_radio_initial_frequency_band_cur = MTLK_HW_BAND_2_4_GHZ; /* FIXME should be NONE */
static const uint32 wave_radio_initial_tx_power_psd = 0;
static const uint32 wave_radio_initial_tx_power_cfg = 0;
static const uint32 wave_radio_initial_short_cyclic_prefix = 0;
static const uint32 wave_radio_initial_power_increase = FALSE;
static const uint32 wave_radio_initial_sm_enable = TRUE;
static const uint32 wave_radio_initial_STBC = 0;
static const uint32 wave_radio_initial_RTS_thresh = 2347;
static const uint32 wave_radio_initial_adv_coding = 1;
static const uint32 wave_radio_initial_OFDM_prot = 1;
static const uint32 wave_radio_initial_overlap_prot = 1;
static const uint32 wave_radio_initial_long_retry = 7;
static const uint32 wave_radio_initial_short_retry = 7;
static const uint32 wave_radio_initial_MSDU_lifetime = 512;
static const uint32 wave_radio_initial_power_limit = 4;
static const uint32 wave_radio_initial_short_preamble = 0;
static const uint32 wave_radio_initial_scan_params[6] = { 400, 400, 5, 7, 0, 0 };
static const uint32 wave_radio_initial_scan_params_bg[6] = { 100, 20, 1, 1, 1, 1000 };
static const uint32 wave_radio_scan_modif_flags = SMF_AUTO_ENABLE_BG | SMF_AUTO_PASSIVE_ON_NO_IR;
static const uint32 wave_radio_calib_cw_masks[2] = { (1 << CW_20), (1 << CW_20) };
static const mtlk_country_code_t wave_radio_initial_country_code = { { COUNTRY_CODE_DFLT } };
static const iwpriv_cca_th_t wave_radio_initial_cca_threshold = { { -62, -62, -72, -72, -69 }, 1 };
static const iwpriv_cca_adapt_t wave_radio_initial_cca_intervals = { 0, 0, -1, 10, 5, 30, 60 };
static const uint32  wave_radio_initial_radio_mode = WAVE_RADIO_ON; /* Radio is ON */
static const uint32  wave_radio_initial_he_mu_operation = 1; /*Enabled by default in FW*/
static const uint32  wave_radio_initial_bf_mode = 0;
static const uint32  wave_radio_initial_active_ant_mask = 0; /* 0 translates to the full ant mask supported by HW */
static const FixedPower_t wave_radio_initial_fixed_pwr = {MTLK_PARAM_DB_INVALID_UINT8, 0, 0, 0};
static const uint32 wave_radio_initial_freq_jump_mode_cfg = MTLK_FREQ_JUMP_MODE_DEFAULT;
static const uint32 wave_radio_initial_beacon_period = 100;
static const uint32 wave_radio_initial_ire_ctrl = SET_AP_LOW_CHANNEL;
static const uint32 wave_radio_initial_unconnected_sta_scan_time = 20;
static const uint32 wave_radio_initial_coex_mode = CO_EX_QOS_NUM_OF_TYPES; /* Invalid mode */
static const uint32 wave_radio_initial_coex_enable = 0;
static const uint8 wave_radio_initial_ssb_mode_cfg[MTLK_SSB_MODE_CFG_SIZE] = {MTLK_PARAM_DB_INVALID_UINT8, MTLK_PARAM_DB_INVALID_UINT8}; /* Invalid */
static const UMI_TRIGGER_FRAME_CONFIG wave_radio_trigger_frame_cfg = { 0 };
static const uint32 wave_radio_initial_block_tx = MTLK_BLOCK_TX_DEFAULT;
static const psdb_pw_limits_t wave_radio_initial_tpc_pw_limits = {{0,0,0,0,0,0,0,0,0}}; /* MUST be aligned to (PSDB_PHY_CW_LAST - 1) */
static const uint32 wave_radio_initial_tpc_loop_type = TPC_CLOSED_LOOP;
static const uint32 wave_radio_initial_offline_acm = WAVE_RADIO_CALIBRATION_MASK_DEFAULT;
static const uint32 wave_radio_initial_online_acm = WAVE_RADIO_CALIBRATION_MASK_DEFAULT;
static const uint32 wave_radio_initial_short_slot = 1;
static const uint32 wave_radio_initial_radar_detection = 1;
static const UMI_SET_PD_THRESH wave_radio_initial_pd_thresh_cfg = {0, QOS_PD_THRESHOLD_NUM_MODES, 0}; /* Invalid */
static const UMI_FIXED_LTF_AND_GI_REQ_t wave_radio_initial_fixed_ltf_and_gi_cfg = {1, 0, 0};
static const UMI_DBG_HE_MU_GROUP_STATS wave_radio_umi_dbg_mu_group_stats[HE_MU_MAX_NUM_OF_GROUPS] = {{0}};
static const uint32  mtlk_radio_initial_invalid_uint8  = MTLK_PARAM_DB_INVALID_UINT8;
static const uint32  mtlk_radio_initial_invalid_uint32 = MTLK_PARAM_DB_INVALID_UINT32;
static const uint32  mtlk_radio_initial_interfdet_thr  = MTLK_INTERFDET_THRESHOLD_INVALID;
static const UMI_SET_RESTRICTED_AC mtlk_radio_initial_rest_ac_mode_cfg = {0, 0, MTLK_PARAM_DB_INVALID_UINT8, 0}; /* Invalid */
static const mtlk_erp_cfg_t mtlk_radio_initial_erp_cfg = {0};
static const UMI_DYNAMIC_MU_TYPE wave_radio_initial_dynamic_mu_type_cfg = {WAVE_DYNAMIC_MU_TYPE_DL_MU_TYPE_DEFAULT, WAVE_DYNAMIC_MU_TYPE_UL_MU_TYPE_DEFAULT,
                    WAVE_DYNAMIC_MU_TYPE_MIN_STA_IN_GROUP_NUM_DEFAULT, WAVE_DYNAMIC_MU_TYPE_MAX_STA_IN_GROUP_NUM_DEFAULT, WAVE_DYNAMIC_MU_TYPE_CDB_CFG_DEFAULT};
static const UMI_HE_MU_FIXED_PARAMTERS wave_radio_initial_he_mu_fixed_params_cfg = {WAVE_HE_MU_FIXED_PARAMTERS_MU_SEQUENCE_DEFAULT, WAVE_HE_MU_FIXED_PARAMTERS_LTF_GI_DEFAULT,
                    WAVE_HE_MU_FIXED_PARAMTERS_CODING_TYPE_DEFAULT, WAVE_HE_MU_FIXED_PARAMTERS_HE_RATE_DEFAULT};
static const UMI_HE_MU_DURATION wave_radio_initial_he_mu_duration_cfg = {WAVE_HE_MU_DURATION_PPDU_DURATION_DEFAULT, WAVE_HE_MU_DURATION_TXOP_DURATION_DEFAULT,
                    WAVE_HE_MU_DURATION_TF_LENGTH_DEFAULT, WAVE_HE_MU_DURATION_NUM_REPETITIONS_DEFAULT};

static const mtlk_pdb_initial_value wave_radio_parameters[] =
{
  /* ID,                                                      TYPE,                 FLAGS,                        SIZE,                            POINTER TO CONST */
  {PARAM_DB_RADIO_TEST_PARAM0,                                PARAM_DB_TYPE_INT,    PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(wave_radio_initial_zero_int),        &wave_radio_initial_zero_int},
  {PARAM_DB_RADIO_TEST_PARAM1,                                PARAM_DB_TYPE_INT,    PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(wave_radio_initial_zero_int),        &wave_radio_initial_zero_int},
  /* Interference Detection */
  {PARAM_DB_RADIO_INTERFDET_MODE,                             PARAM_DB_TYPE_INT,     PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(wave_radio_initial_interfdet_mode), &wave_radio_initial_interfdet_mode},
  {PARAM_DB_RADIO_INTERFDET_20MHZ_DETECTION_THRESHOLD,        PARAM_DB_TYPE_INT,     PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(wave_radio_initial_zero_int),       &wave_radio_initial_zero_int},
  {PARAM_DB_RADIO_INTERFDET_20MHZ_NOTIFICATION_THRESHOLD,     PARAM_DB_TYPE_INT,     PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(mtlk_radio_initial_interfdet_thr),  &mtlk_radio_initial_interfdet_thr},
  {PARAM_DB_RADIO_INTERFDET_40MHZ_DETECTION_THRESHOLD,        PARAM_DB_TYPE_INT,     PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(wave_radio_initial_zero_int),       &wave_radio_initial_zero_int},
  {PARAM_DB_RADIO_INTERFDET_40MHZ_NOTIFICATION_THRESHOLD,     PARAM_DB_TYPE_INT,     PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(mtlk_radio_initial_interfdet_thr),  &mtlk_radio_initial_interfdet_thr},
  {PARAM_DB_RADIO_INTERFDET_SCAN_NOISE_THRESHOLD,             PARAM_DB_TYPE_INT,     PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(wave_radio_initial_zero_int),       &wave_radio_initial_zero_int},
  {PARAM_DB_RADIO_INTERFDET_SCAN_MINIMUM_NOISE,               PARAM_DB_TYPE_INT,     PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(wave_radio_initial_zero_int),       &wave_radio_initial_zero_int},
  {PARAM_DB_RADIO_INTERFDET_ACTIVE_POLLING_TIMEOUT,           PARAM_DB_TYPE_INT,     PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(wave_radio_initial_zero_int),       &wave_radio_initial_zero_int},
  {PARAM_DB_RADIO_INTERFDET_SHORT_SCAN_POLLING_TIMEOUT,       PARAM_DB_TYPE_INT,     PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(wave_radio_initial_zero_int),       &wave_radio_initial_zero_int},
  {PARAM_DB_RADIO_INTERFDET_LONG_SCAN_POLLING_TIMEOUT,        PARAM_DB_TYPE_INT,     PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(wave_radio_initial_zero_int),       &wave_radio_initial_zero_int},
  {PARAM_DB_RADIO_INTERFDET_ACTIVE_NOTIFICATION_TIMEOUT,      PARAM_DB_TYPE_INT,     PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(wave_radio_initial_zero_int),       &wave_radio_initial_zero_int},
  {PARAM_DB_RADIO_INTERFDET_SHORT_SCAN_NOTIFICATION_TIMEOUT,  PARAM_DB_TYPE_INT,     PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(wave_radio_initial_zero_int),       &wave_radio_initial_zero_int},
  {PARAM_DB_RADIO_INTERFDET_LONG_SCAN_NOTIFICATION_TIMEOUT,   PARAM_DB_TYPE_INT,     PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(wave_radio_initial_zero_int),       &wave_radio_initial_zero_int},
  {PARAM_DB_RADIO_PROG_MODEL_SPECTRUM_MODE,                   PARAM_DB_TYPE_INT,     PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(wave_radio_initial_spectrum_mode),  &wave_radio_initial_spectrum_mode},
  {PARAM_DB_RADIO_SPECTRUM_MODE,                              PARAM_DB_TYPE_INT,     PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(wave_radio_initial_spectrum_mode),  &wave_radio_initial_spectrum_mode},
  {PARAM_DB_RADIO_BONDING_MODE,                               PARAM_DB_TYPE_INT,     PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(wave_radio_initial_bonding_mode),   &wave_radio_initial_bonding_mode},
  {PARAM_DB_RADIO_CHANNEL_CFG,                                PARAM_DB_TYPE_INT,     PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(wave_radio_initial_channel),        &wave_radio_initial_channel},
  {PARAM_DB_RADIO_CHANNEL_CUR,                                PARAM_DB_TYPE_INT,     PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(wave_radio_initial_channel),        &wave_radio_initial_channel},
  {PARAM_DB_RADIO_CHANNEL_PREV,                               PARAM_DB_TYPE_BINARY,  PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(wave_radio_initial_channel_prev), &wave_radio_initial_channel_prev},
  {PARAM_DB_RADIO_POWER_SELECTION,                            PARAM_DB_TYPE_INT,     PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(wave_radio_initial_power_selection), &wave_radio_initial_power_selection},
  {PARAM_DB_RADIO_MAC_SOFT_RESET_ENABLE,                      PARAM_DB_TYPE_INT,     PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(wave_radio_initial_mac_soft_reset_enable), &wave_radio_initial_mac_soft_reset_enable},
  {PARAM_DB_RADIO_DOT11D_ENABLED,                             PARAM_DB_TYPE_INT,     PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(wave_radio_initial_dot11d_enabled), &wave_radio_initial_dot11d_enabled},
  {PARAM_DB_RADIO_MAC_WATCHDOG_TIMER_TIMEOUT_MS,              PARAM_DB_TYPE_INT,     PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(wave_radio_initial_mac_watchdog_timeout_ms), &wave_radio_initial_mac_watchdog_timeout_ms},
  {PARAM_DB_RADIO_MAC_WATCHDOG_TIMER_PERIOD_MS,               PARAM_DB_TYPE_INT,     PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(wave_radio_initial_mac_watchdog_period_ms), &wave_radio_initial_mac_watchdog_period_ms},
  {PARAM_DB_RADIO_FREQ_BAND_CFG,                              PARAM_DB_TYPE_INT,     PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(wave_radio_initial_frequency_band_cfg), &wave_radio_initial_frequency_band_cfg},
  {PARAM_DB_RADIO_FREQ_BAND_CUR,                              PARAM_DB_TYPE_INT,     PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(wave_radio_initial_frequency_band_cur), &wave_radio_initial_frequency_band_cur},
  {PARAM_DB_RADIO_TX_POWER_REG_LIM,                           PARAM_DB_TYPE_INT,     PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(wave_radio_initial_zero_int), &wave_radio_initial_zero_int},
  {PARAM_DB_RADIO_TX_POWER_PSD,                               PARAM_DB_TYPE_INT,     PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(wave_radio_initial_tx_power_psd),  &wave_radio_initial_tx_power_psd},
  {PARAM_DB_RADIO_TX_POWER_CFG,                               PARAM_DB_TYPE_INT,     PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(wave_radio_initial_tx_power_cfg),  &wave_radio_initial_tx_power_cfg},
  {PARAM_DB_RADIO_TPC_PW_LIMITS_PSD,                          PARAM_DB_TYPE_BINARY,  PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(wave_radio_initial_tpc_pw_limits), &wave_radio_initial_tpc_pw_limits},
  {PARAM_DB_RADIO_TPC_PW_LIMITS_CFG,                          PARAM_DB_TYPE_BINARY,  PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(wave_radio_initial_tpc_pw_limits), &wave_radio_initial_tpc_pw_limits},
  {PARAM_DB_RADIO_SHORT_CYCLIC_PREFIX,                        PARAM_DB_TYPE_INT,     PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(wave_radio_initial_short_cyclic_prefix), &wave_radio_initial_short_cyclic_prefix},
  {PARAM_DB_RADIO_POWER_INCREASE,                             PARAM_DB_TYPE_INT,     PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(wave_radio_initial_power_increase), &wave_radio_initial_power_increase},
  {PARAM_DB_RADIO_SM_ENABLE,                                  PARAM_DB_TYPE_INT,     PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(wave_radio_initial_sm_enable),      &wave_radio_initial_sm_enable},
  {PARAM_DB_RADIO_STBC,                                       PARAM_DB_TYPE_INT,     PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(wave_radio_initial_STBC),           &wave_radio_initial_STBC},
  {PARAM_DB_RADIO_RTS_THRESH,                                 PARAM_DB_TYPE_INT,     PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(wave_radio_initial_RTS_thresh),     &wave_radio_initial_RTS_thresh},
  {PARAM_DB_RADIO_ADVANCED_CODING,                            PARAM_DB_TYPE_INT,     PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(wave_radio_initial_adv_coding),     &wave_radio_initial_adv_coding},
  {PARAM_DB_RADIO_OFDM_PROTECTION,                            PARAM_DB_TYPE_INT,     PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(wave_radio_initial_OFDM_prot),      &wave_radio_initial_OFDM_prot},
  {PARAM_DB_RADIO_OVERLAPPING_PROT,                           PARAM_DB_TYPE_INT,     PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(wave_radio_initial_overlap_prot),   &wave_radio_initial_overlap_prot},
  {PARAM_DB_RADIO_LONG_RETRY_LIMIT,                           PARAM_DB_TYPE_INT,     PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(wave_radio_initial_long_retry),     &wave_radio_initial_long_retry},
  {PARAM_DB_RADIO_SHORT_RETRY_LIMIT,                          PARAM_DB_TYPE_INT,     PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(wave_radio_initial_short_retry),    &wave_radio_initial_short_retry},
  {PARAM_DB_RADIO_MSDU_LIFETIME,                              PARAM_DB_TYPE_INT,     PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(wave_radio_initial_MSDU_lifetime),  &wave_radio_initial_MSDU_lifetime},
  {PARAM_DB_RADIO_POWER_LIMIT,                                PARAM_DB_TYPE_INT,     PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(wave_radio_initial_power_limit),    &wave_radio_initial_power_limit},
  {PARAM_DB_RADIO_SHORT_PREAMBLE,                             PARAM_DB_TYPE_INT,     PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(wave_radio_initial_short_preamble), &wave_radio_initial_short_preamble},
  {PARAM_DB_RADIO_SCAN_PARAMS,                                PARAM_DB_TYPE_BINARY,  PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(wave_radio_initial_scan_params),    &wave_radio_initial_scan_params},
  {PARAM_DB_RADIO_SCAN_PARAMS_BG,                             PARAM_DB_TYPE_BINARY,  PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(wave_radio_initial_scan_params_bg), &wave_radio_initial_scan_params_bg},
  {PARAM_DB_RADIO_SCAN_MODIFS,                                PARAM_DB_TYPE_INT,     PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(wave_radio_scan_modif_flags),       &wave_radio_scan_modif_flags},
  {PARAM_DB_RADIO_CALIB_CW_MASKS,                             PARAM_DB_TYPE_BINARY,  PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(wave_radio_calib_cw_masks),         &wave_radio_calib_cw_masks},
  {PARAM_DB_RADIO_11B_ANTSEL_RATE,                            PARAM_DB_TYPE_INT,     PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(mtlk_radio_initial_invalid_uint8),  &mtlk_radio_initial_invalid_uint8},
  {PARAM_DB_RADIO_11B_ANTSEL_RXANT,                           PARAM_DB_TYPE_INT,     PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(mtlk_radio_initial_invalid_uint8),  &mtlk_radio_initial_invalid_uint8},
  {PARAM_DB_RADIO_11B_ANTSEL_TXANT,                           PARAM_DB_TYPE_INT,     PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(mtlk_radio_initial_invalid_uint8),  &mtlk_radio_initial_invalid_uint8},
  {PARAM_DB_RADIO_COUNTRY_CODE_ALPHA,                         PARAM_DB_TYPE_BINARY,  PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(wave_radio_initial_country_code),   &wave_radio_initial_country_code},
  {PARAM_DB_RADIO_CCA_THRESHOLD,                              PARAM_DB_TYPE_BINARY,  PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(wave_radio_initial_cca_threshold),  &wave_radio_initial_cca_threshold},
  {PARAM_DB_RADIO_CCA_ADAPT,                                  PARAM_DB_TYPE_BINARY,  PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(wave_radio_initial_cca_intervals),  &wave_radio_initial_cca_intervals},
  {PARAM_DB_RADIO_TX_POWER_LIMIT_OFFSET,                      PARAM_DB_TYPE_INT,     PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(wave_radio_initial_zero_int),       &wave_radio_initial_zero_int},
  {PARAM_DB_RADIO_QAMPLUS_MODE,                               PARAM_DB_TYPE_INT,     PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(mtlk_radio_initial_invalid_uint8),  &mtlk_radio_initial_invalid_uint8},
  {PARAM_DB_RADIO_MODE_CURRENT,                               PARAM_DB_TYPE_INT,     PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(wave_radio_initial_radio_mode),     &wave_radio_initial_radio_mode},
  {PARAM_DB_RADIO_MODE_REQUESTED,                             PARAM_DB_TYPE_INT,     PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(wave_radio_initial_radio_mode),     &wave_radio_initial_radio_mode},
  {PARAM_DB_RADIO_ACS_UPDATE_TO,                              PARAM_DB_TYPE_INT,     PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(wave_radio_initial_zero_int),       &wave_radio_initial_zero_int},
  {PARAM_DB_RADIO_MU_OPERATION,                               PARAM_DB_TYPE_INT,     PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(mtlk_radio_initial_invalid_uint8),  &mtlk_radio_initial_invalid_uint8},
  {PARAM_DB_RADIO_HE_MU_OPERATION,                            PARAM_DB_TYPE_INT,     PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(wave_radio_initial_he_mu_operation), &wave_radio_initial_he_mu_operation},
  {PARAM_DB_RADIO_DISABLE_MASTER_VAP,                         PARAM_DB_TYPE_INT,     PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(wave_radio_initial_zero_int), &wave_radio_initial_zero_int},
  {PARAM_DB_RADIO_RADAR_RSSI_TH,                              PARAM_DB_TYPE_INT,     PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(mtlk_radio_initial_invalid_uint8),  &mtlk_radio_initial_invalid_uint8},
  {PARAM_DB_RADIO_RTS_DYNAMIC_BW,                             PARAM_DB_TYPE_INT,     PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(mtlk_radio_initial_invalid_uint8),  &mtlk_radio_initial_invalid_uint8},
  {PARAM_DB_RADIO_RTS_STATIC_BW,                              PARAM_DB_TYPE_INT,     PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(mtlk_radio_initial_invalid_uint8),  &mtlk_radio_initial_invalid_uint8},
  {PARAM_DB_RADIO_BF_MODE,                                    PARAM_DB_TYPE_INT,     PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(wave_radio_initial_bf_mode),        &wave_radio_initial_bf_mode},
  {PARAM_DB_RADIO_ACTIVE_ANT_MASK,                            PARAM_DB_TYPE_INT,     PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(wave_radio_initial_active_ant_mask),&wave_radio_initial_active_ant_mask},
  {PARAM_DB_RADIO_SLOW_PROBING_MASK,                          PARAM_DB_TYPE_INT,     PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(mtlk_radio_initial_invalid_uint32), &mtlk_radio_initial_invalid_uint32},
  {PARAM_DB_RADIO_TXOP_MODE,                                  PARAM_DB_TYPE_INT,     PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(mtlk_radio_initial_invalid_uint8),  &mtlk_radio_initial_invalid_uint8},
  {PARAM_DB_RADIO_FIXED_PWR,                                  PARAM_DB_TYPE_BINARY,  PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(wave_radio_initial_fixed_pwr),      &wave_radio_initial_fixed_pwr},
  {PARAM_DB_RADIO_FREQ_JUMP_MODE,                             PARAM_DB_TYPE_INT,     PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(wave_radio_initial_freq_jump_mode_cfg), &wave_radio_initial_freq_jump_mode_cfg},
  {PARAM_DB_RADIO_BEACON_PERIOD,                              PARAM_DB_TYPE_INT,     PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(wave_radio_initial_beacon_period),  &wave_radio_initial_beacon_period},
  {PARAM_DB_RADIO_AGG_RATE_LIMIT_MODE,                        PARAM_DB_TYPE_INT,     PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(mtlk_radio_initial_invalid_uint8),  &mtlk_radio_initial_invalid_uint8},
  {PARAM_DB_RADIO_AGG_RATE_LIMIT_MAXRATE,                     PARAM_DB_TYPE_INT,     PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(wave_radio_initial_zero_int),       &wave_radio_initial_zero_int},
  {PARAM_DB_RADIO_IRE_CTRL_B,                                 PARAM_DB_TYPE_INT,     PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(wave_radio_initial_ire_ctrl),        &wave_radio_initial_ire_ctrl},
  {PARAM_DB_RADIO_COEX_MODE,                                  PARAM_DB_TYPE_INT,     PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(wave_radio_initial_coex_mode),       &wave_radio_initial_coex_mode},
  {PARAM_DB_RADIO_COEX_ENABLED,                               PARAM_DB_TYPE_INT,     PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(wave_radio_initial_coex_enable),     &wave_radio_initial_coex_enable},
  {PARAM_DB_RADIO_UNCONNECTED_STA_SCAN_TIME,                  PARAM_DB_TYPE_INT,     PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(wave_radio_initial_unconnected_sta_scan_time),  &wave_radio_initial_unconnected_sta_scan_time},
  {PARAM_DB_RADIO_MAX_MPDU_LENGTH,                            PARAM_DB_TYPE_INT,     PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(mtlk_radio_initial_invalid_uint32),  &mtlk_radio_initial_invalid_uint32},
  {PARAM_DB_RADIO_SSB_MODE,                                   PARAM_DB_TYPE_BINARY,  PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(wave_radio_initial_ssb_mode_cfg),    &wave_radio_initial_ssb_mode_cfg},
  {PARAM_DB_RADIO_BLOCK_TX,                                   PARAM_DB_TYPE_INT,     PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(wave_radio_initial_block_tx),        &wave_radio_initial_block_tx},
  {PARAM_DB_RADIO_TF_PARAMS,                                  PARAM_DB_TYPE_BINARY,  PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(wave_radio_trigger_frame_cfg),       &wave_radio_trigger_frame_cfg},
  {PARAM_DB_RADIO_RX_DUTY_CYCLE_ON_TIME,                      PARAM_DB_TYPE_INT,     PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(mtlk_radio_initial_invalid_uint32),  &mtlk_radio_initial_invalid_uint32},
  {PARAM_DB_RADIO_RX_DUTY_CYCLE_OFF_TIME,                     PARAM_DB_TYPE_INT,     PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(mtlk_radio_initial_invalid_uint32),  &mtlk_radio_initial_invalid_uint32},
  {PARAM_DB_RADIO_CALIBRATION_ALGO_MASK,                      PARAM_DB_TYPE_INT,     PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(wave_radio_initial_offline_acm),     &wave_radio_initial_offline_acm},
  {PARAM_DB_RADIO_ONLINE_ACM,                                 PARAM_DB_TYPE_INT,     PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(wave_radio_initial_online_acm),      &wave_radio_initial_online_acm},
  {PARAM_DB_RADIO_TPC_LOOP_TYPE,                              PARAM_DB_TYPE_INT,     PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(wave_radio_initial_tpc_loop_type),   &wave_radio_initial_tpc_loop_type},
  {PARAM_DB_RADIO_SHORT_SLOT,                                 PARAM_DB_TYPE_INT,     PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(wave_radio_initial_short_slot),      &wave_radio_initial_short_slot},
  {PARAM_DB_RADIO_DFS_RADAR_DETECTION,                        PARAM_DB_TYPE_INT,     PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(wave_radio_initial_radar_detection), &wave_radio_initial_radar_detection},
  {PARAM_DB_RADIO_AMSDU_NUM,                                  PARAM_DB_TYPE_INT,     PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(mtlk_radio_initial_invalid_uint32),  &mtlk_radio_initial_invalid_uint32},
  {PARAM_DB_RADIO_AMSDU_VNUM,                                 PARAM_DB_TYPE_INT,     PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(mtlk_radio_initial_invalid_uint32),  &mtlk_radio_initial_invalid_uint32},
  {PARAM_DB_RADIO_AMSDU_HENUM,                                PARAM_DB_TYPE_INT,     PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(mtlk_radio_initial_invalid_uint32),  &mtlk_radio_initial_invalid_uint32},
  {PARAM_DB_RADIO_PD_THRESHOLD,                               PARAM_DB_TYPE_BINARY,  PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(wave_radio_initial_pd_thresh_cfg),   &wave_radio_initial_pd_thresh_cfg},
  {PARAM_DB_RADIO_FIXED_LTF_AND_GI,                           PARAM_DB_TYPE_BINARY,  PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(wave_radio_initial_fixed_ltf_and_gi_cfg), &wave_radio_initial_fixed_ltf_and_gi_cfg},
  {PARAM_DB_RADIO_PLAN_MU_GROUP_STATS,                        PARAM_DB_TYPE_BINARY,  PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(wave_radio_umi_dbg_mu_group_stats), &wave_radio_umi_dbg_mu_group_stats},
  {PARAM_DB_RADIO_RESTRICTED_AC_MODE,                         PARAM_DB_TYPE_BINARY,  PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(mtlk_radio_initial_rest_ac_mode_cfg), &mtlk_radio_initial_rest_ac_mode_cfg},
  {PARAM_DB_RADIO_FAST_DROP,                                  PARAM_DB_TYPE_INT,     PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(mtlk_radio_initial_invalid_uint8),   &mtlk_radio_initial_invalid_uint8},
  {PARAM_DB_RADIO_ERP_CFG,                                    PARAM_DB_TYPE_BINARY,  PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(mtlk_radio_initial_erp_cfg),         &mtlk_radio_initial_erp_cfg},
  {PARAM_DB_RADIO_DYNAMIC_MC_RATE,                            PARAM_DB_TYPE_INT,     PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(mtlk_radio_initial_invalid_uint32),  &mtlk_radio_initial_invalid_uint32},
  {PARAM_DB_RADIO_TEST_BUS_MODE,                              PARAM_DB_TYPE_INT,     PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(mtlk_radio_initial_invalid_uint32),  &mtlk_radio_initial_invalid_uint32},
  {PARAM_DB_RADIO_WLAN_COUNTERS_SRC,                          PARAM_DB_TYPE_INT,     PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(mtlk_core_initial_zero_int),         &mtlk_core_initial_zero_int},
  {PARAM_DB_RADIO_RTS_CUTOFF_POINT,                           PARAM_DB_TYPE_INT,     PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(mtlk_radio_initial_invalid_uint32),  &mtlk_radio_initial_invalid_uint32},
  {PARAM_DB_RADIO_11N_ACAX_COMPAT,                            PARAM_DB_TYPE_INT,     PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(wave_radio_initial_zero_int),        &wave_radio_initial_zero_int},
  {PARAM_DB_RADIO_DYNAMIC_MU_TYPE,                            PARAM_DB_TYPE_BINARY,  PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(wave_radio_initial_dynamic_mu_type_cfg), &wave_radio_initial_dynamic_mu_type_cfg},
  {PARAM_DB_RADIO_HE_MU_FIXED_PARAMTERS,                      PARAM_DB_TYPE_BINARY,  PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(wave_radio_initial_he_mu_fixed_params_cfg), &wave_radio_initial_he_mu_fixed_params_cfg},
  {PARAM_DB_RADIO_HE_MU_DURATION,                             PARAM_DB_TYPE_BINARY,  PARAM_DB_VALUE_FLAG_NO_FLAG,  sizeof(wave_radio_initial_he_mu_duration_cfg), &wave_radio_initial_he_mu_duration_cfg},
  {PARAM_DB_RADIO_LAST_VALUE_ID,                               0,                    0,                            0,                                          NULL},
};

#if defined (CPTCFG_IWLWAV_LINDRV_HW_PCIEG5)
static const uint32  wave_radio_initial_g5_tpc_loop_type = TPC_CLOSED_LOOP;
#endif

#if defined (CPTCFG_IWLWAV_LINDRV_HW_PCIEG6)
static const uint32  wave_radio_initial_g6_tpc_loop_type = TPC_CLOSED_LOOP;
#endif

#ifdef CPTCFG_IWLWAV_LINDRV_HW_PCIEG5
static const uint32  wave_radio_initial_pcieg5_wrx300_short_slot = 0;
static const uint32  wave_radio_initial_pcieg5_wrx300_radar_detection = 0;
static const uint32  wave_radio_initial_pcieg5_wrx500_short_slot = 1;
static const uint32  wave_radio_initial_pcieg5_wrx500_radar_detection = 1;
static const uint32  wave_radio_initial_g5_txop_sid = GEN5_ALL_STA_SID;

static const mtlk_pdb_initial_value wave_radio_parameters_pcieg5_wrx300[] =
{
  {PARAM_DB_RADIO_SHORT_SLOT,            PARAM_DB_TYPE_INT, PARAM_DB_VALUE_FLAG_NO_FLAG, sizeof(wave_radio_initial_pcieg5_wrx300_short_slot),      &wave_radio_initial_pcieg5_wrx300_short_slot},
  {PARAM_DB_RADIO_DFS_RADAR_DETECTION,   PARAM_DB_TYPE_INT, PARAM_DB_VALUE_FLAG_NO_FLAG, sizeof(wave_radio_initial_pcieg5_wrx300_radar_detection), &wave_radio_initial_pcieg5_wrx300_radar_detection},
  {PARAM_DB_RADIO_TPC_LOOP_TYPE,         PARAM_DB_TYPE_INT, PARAM_DB_VALUE_FLAG_NO_FLAG, sizeof(wave_radio_initial_g5_tpc_loop_type),              &wave_radio_initial_g5_tpc_loop_type},
  {PARAM_DB_RADIO_TXOP_SID,              PARAM_DB_TYPE_INT, PARAM_DB_VALUE_FLAG_NO_FLAG, sizeof(wave_radio_initial_g5_txop_sid),                   &wave_radio_initial_g5_txop_sid },
  {PARAM_DB_RADIO_LAST_VALUE_ID,         0,                 0,                           0,                                                        NULL},
};

static const mtlk_pdb_initial_value wave_radio_parameters_pcieg5_wrx500[] =
{
  {PARAM_DB_RADIO_SHORT_SLOT,            PARAM_DB_TYPE_INT, PARAM_DB_VALUE_FLAG_NO_FLAG, sizeof(wave_radio_initial_pcieg5_wrx500_short_slot),      &wave_radio_initial_pcieg5_wrx500_short_slot},
  {PARAM_DB_RADIO_DFS_RADAR_DETECTION,   PARAM_DB_TYPE_INT, PARAM_DB_VALUE_FLAG_NO_FLAG, sizeof(wave_radio_initial_pcieg5_wrx500_radar_detection), &wave_radio_initial_pcieg5_wrx500_radar_detection},
  {PARAM_DB_RADIO_TPC_LOOP_TYPE,         PARAM_DB_TYPE_INT, PARAM_DB_VALUE_FLAG_NO_FLAG, sizeof(wave_radio_initial_g5_tpc_loop_type),              &wave_radio_initial_g5_tpc_loop_type},
  {PARAM_DB_RADIO_TXOP_SID,              PARAM_DB_TYPE_INT, PARAM_DB_VALUE_FLAG_NO_FLAG, sizeof(wave_radio_initial_g5_txop_sid),                   &wave_radio_initial_g5_txop_sid },
  {PARAM_DB_RADIO_LAST_VALUE_ID,         0,                 0,                           0,                                                        NULL},
};
#endif /* CPTCFG_IWLWAV_LINDRV_HW_PCIEG5 */

#ifdef CPTCFG_IWLWAV_LINDRV_HW_PCIEG6
static const uint32  wave_radio_initial_pcieg6_wrx300_short_slot = 0;
static const uint32  wave_radio_initial_pcieg6_wrx300_radar_detection = 0;
static const uint32  wave_radio_initial_pcieg6_wrx500_short_slot = 1;
/* disable radar detection by default */
static const uint32  wave_radio_initial_pcieg6_wrx500_radar_detection = 0;
static const uint32  wave_radio_initial_g6_txop_sid = GEN6_ALL_STA_SID;

static const mtlk_pdb_initial_value wave_radio_parameters_pcieg6_wrx300[] =
{
  {PARAM_DB_RADIO_SHORT_SLOT,            PARAM_DB_TYPE_INT, PARAM_DB_VALUE_FLAG_NO_FLAG, sizeof(wave_radio_initial_pcieg6_wrx300_short_slot),      &wave_radio_initial_pcieg6_wrx300_short_slot},
  {PARAM_DB_RADIO_DFS_RADAR_DETECTION,   PARAM_DB_TYPE_INT, PARAM_DB_VALUE_FLAG_NO_FLAG, sizeof(wave_radio_initial_pcieg6_wrx300_radar_detection), &wave_radio_initial_pcieg6_wrx300_radar_detection},
  {PARAM_DB_RADIO_TPC_LOOP_TYPE,         PARAM_DB_TYPE_INT, PARAM_DB_VALUE_FLAG_NO_FLAG, sizeof(wave_radio_initial_g6_tpc_loop_type),              &wave_radio_initial_g6_tpc_loop_type},
  {PARAM_DB_RADIO_TXOP_SID,              PARAM_DB_TYPE_INT, PARAM_DB_VALUE_FLAG_NO_FLAG, sizeof(wave_radio_initial_g6_txop_sid),                   &wave_radio_initial_g6_txop_sid },
  {PARAM_DB_RADIO_LAST_VALUE_ID,         0,                 0,                           0,                                                        NULL },
};

static const mtlk_pdb_initial_value wave_radio_parameters_pcieg6_wrx500[] =
{
  {PARAM_DB_RADIO_SHORT_SLOT,            PARAM_DB_TYPE_INT, PARAM_DB_VALUE_FLAG_NO_FLAG, sizeof(wave_radio_initial_pcieg6_wrx500_short_slot),      &wave_radio_initial_pcieg6_wrx500_short_slot},
  {PARAM_DB_RADIO_DFS_RADAR_DETECTION,   PARAM_DB_TYPE_INT, PARAM_DB_VALUE_FLAG_NO_FLAG, sizeof(wave_radio_initial_pcieg6_wrx500_radar_detection), &wave_radio_initial_pcieg6_wrx500_radar_detection},
  {PARAM_DB_RADIO_TPC_LOOP_TYPE,         PARAM_DB_TYPE_INT, PARAM_DB_VALUE_FLAG_NO_FLAG, sizeof(wave_radio_initial_g6_tpc_loop_type),              &wave_radio_initial_g6_tpc_loop_type},
  {PARAM_DB_RADIO_TXOP_SID,              PARAM_DB_TYPE_INT, PARAM_DB_VALUE_FLAG_NO_FLAG, sizeof(wave_radio_initial_g6_txop_sid),                   &wave_radio_initial_g6_txop_sid },
  {PARAM_DB_RADIO_LAST_VALUE_ID,         0,                 0,                           0,                                                        NULL },
};
#endif /* CPTCFG_IWLWAV_LINDRV_HW_PCIEG6 */
#define MTLK_IDEFS_OFF
#include "mtlkidefs.h"

#endif /* __RADIO_PDB_DEF_H_ */
