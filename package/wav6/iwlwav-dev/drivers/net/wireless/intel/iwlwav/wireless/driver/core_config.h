/******************************************************************************

                               Copyright (c) 2013
                            Lantiq Deutschland GmbH

  For licensing information, see the file 'LICENSE' in the root folder of
  this software module.

******************************************************************************/
/*
 * $Id$
 *
 *
 *
 * Core configuration interface
 *
 */
#ifndef __CORE_CONFIG_H__
#define __CORE_CONFIG_H__

#include "core_cfg_iface.h"

/* Get/set RadioParmDB by vap_handle */
#define WAVE_VAP_RADIO_PDB_GET_INT(vap_handle, id) \
  wave_pdb_get_int(mtlk_vap_get_radio_pdb(vap_handle), id);

#define WAVE_VAP_RADIO_PDB_SET_INT(vap_handle, id, value) \
  wave_pdb_set_int(mtlk_vap_get_radio_pdb(vap_handle), id, value);

int __MTLK_IFUNC core_cfg_get_station (mtlk_handle_t hcore, const void* data, uint32 data_size);
int __MTLK_IFUNC core_cfg_wmm_param_set (mtlk_handle_t hcore, const void* data, uint32 data_size);
int __MTLK_IFUNC core_cfg_wmm_param_set_by_params (mtlk_core_t *master_core, struct mtlk_wmm_settings *wmm_settings);
int __MTLK_IFUNC core_cfg_set_chan_clpb (mtlk_handle_t hcore, const void* data, uint32 data_size);
int __MTLK_IFUNC finish_and_prevent_fw_set_chan_clpb (mtlk_handle_t hcore, const void* data, uint32 data_size);
int __MTLK_IFUNC core_cfg_request_sid (mtlk_handle_t hcore, const void *data, uint32 data_size);
int __MTLK_IFUNC core_cfg_internal_request_sid (mtlk_core_t *core, IEEE_ADDR *addr, uint16 *p_sid);
int __MTLK_IFUNC core_cfg_remove_sid (mtlk_handle_t hcore, const void *data, uint32 data_size);
int core_cfg_ap_disconnect_all (mtlk_handle_t hcore, const void* data, uint32 data_size);
BOOL core_cfg_is_current_channel (mtlk_handle_t hcore, uint32 channel);

uint8 core_cfg_get_is_ht_cur(mtlk_core_t *core);
uint8 core_cfg_get_is_ht_cfg(mtlk_core_t *core);

BOOL core_cfg_mbss_check_activation_params (struct nic *nic);
int core_cfg_set_mac_addr (mtlk_core_t *nic, const char *mac);
int core_cfg_set_cur_bonding(mtlk_core_t *core, uint8 bonding);
int core_cfg_set_user_bonding(mtlk_core_t *core, uint8 bonding);

int core_cfg_set_is_dot11d(mtlk_core_t *core, BOOL is_dot11d);
uint8 core_cfg_get_network_mode_cur(mtlk_core_t *core);
uint8 core_cfg_get_network_mode_cfg(mtlk_core_t *core);
uint8 core_cfg_get_network_mode(mtlk_core_t *core);
BOOL core_cfg_net_state_is_connected(uint16 net_state);

int __MTLK_IFUNC core_cfg_set_chan (mtlk_core_t *core, const struct mtlk_chan_def *cd, struct set_chan_param_data *cpd);
int core_cfg_send_set_chan(mtlk_core_t *core,
                           const struct mtlk_chan_def *cd, struct set_chan_param_data *cpd);

int core_cfg_set_calibration_mask(mtlk_handle_t hcore, const void* data, uint32 data_size);
int core_cfg_get_calibration_mask(mtlk_handle_t hcore, const void* data, uint32 data_size);
int __MTLK_IFUNC wave_core_set_nfrp_cfg  (mtlk_handle_t hcore, const void *data, uint32 data_size);

int core_cfg_send_set_chan_by_msg(mtlk_txmm_msg_t *man_msg);
int poll_client_req(mtlk_vap_handle_t vap_handle, sta_entry *sta, int *status);

uint32 __MTLK_IFUNC core_cfg_get_regd_code(mtlk_core_t *core);
void   __MTLK_IFUNC core_cfg_country_code_get(mtlk_core_t *core, mtlk_country_code_t *country_code);
int    __MTLK_IFUNC core_cfg_country_code_set(mtlk_core_t *core, const mtlk_country_code_t *country_code);
void   __MTLK_IFUNC core_cfg_country_code_set_default(mtlk_core_t* core);
int    __MTLK_IFUNC core_cfg_country_code_set_by_str(mtlk_core_t *core, char *country, int len);
int    __MTLK_IFUNC core_cfg_set_country_from_ui (mtlk_core_t *core, mtlk_country_code_t *country_code);
int    __MTLK_IFUNC core_cfg_set_hostapd_initial_data (mtlk_core_t *core, struct intel_vendor_initial_data_cfg* cc_data);

void   __MTLK_IFUNC core_cfg_sta_country_code_set_default_on_activate(mtlk_core_t* core);
void   __MTLK_IFUNC core_cfg_sta_country_code_update_on_connect(mtlk_core_t *core, mtlk_country_code_t *country_code);

void core_cfg_set_tx_power_limit(mtlk_core_t *core, unsigned center_freq, enum chanWidth width, unsigned cf_primary, mtlk_country_code_t req_country_code);

int core_recovery_cfg_wmm_param_set(mtlk_core_t *core);

int __MTLK_IFUNC core_cfg_set_four_addr_cfg (mtlk_handle_t hcore, const void *data, uint32 data_size);
int __MTLK_IFUNC core_cfg_get_four_addr_cfg (mtlk_handle_t hcore, const void *data, uint32 data_size);
int __MTLK_IFUNC core_cfg_get_four_addr_sta_list (mtlk_handle_t hcore, const void* data, uint32 data_size);
void core_cfg_flush_four_addr_list (mtlk_core_t *nic);
BOOL core_cfg_four_addr_entry_exists (mtlk_core_t *nic,
  const IEEE_ADDR *addr);

int  __MTLK_IFUNC mtlk_core_cfg_set_cca_threshold (mtlk_handle_t hcore, const void* data, uint32 data_size);
int  __MTLK_IFUNC mtlk_core_cfg_get_cca_threshold (mtlk_handle_t hcore, const void* data, uint32 data_size);
int  __MTLK_IFUNC mtlk_core_cfg_init_cca_threshold (mtlk_core_t *core);
int  __MTLK_IFUNC mtlk_core_cfg_recovery_cca_threshold (mtlk_core_t *core);
int  __MTLK_IFUNC mtlk_core_cfg_send_actual_cca_threshold (mtlk_core_t *core);

int  mtlk_core_cfg_init_cca_adapt(mtlk_core_t *core);
int  mtlk_core_cfg_read_cca_threshold(mtlk_core_t *core, iwpriv_cca_th_t *cca_th_params);
int  __MTLK_IFUNC mtlk_core_cfg_set_cca_intervals (mtlk_handle_t hcore, const void* data, uint32 data_size);
int  __MTLK_IFUNC mtlk_core_cfg_get_cca_intervals (mtlk_handle_t hcore, const void* data, uint32 data_size);
int  mtlk_core_cfg_recovery_cca_adapt(mtlk_core_t *core);
int  mtlk_core_cfg_init_cca_adapt(mtlk_core_t *core);
int  mtlk_core_cfg_send_cca_threshold_req(mtlk_core_t *core, iwpriv_cca_th_t *cca_th_params);

int  __MTLK_IFUNC mtlk_core_cfg_set_radar_rssi_threshold (mtlk_handle_t hcore, const void* data, uint32 data_size);
int  __MTLK_IFUNC mtlk_core_cfg_get_radar_rssi_threshold (mtlk_handle_t hcore, const void* data, uint32 data_size);
int  __MTLK_IFUNC mtlk_core_cfg_recovery_radar_rssi_threshold (mtlk_core_t *core);

int  mtlk_core_cfg_send_active_ant_mask (mtlk_core_t *core, uint32 mask);
int __MTLK_IFUNC mtlk_core_cfg_set_ire_ctrl_b (mtlk_handle_t hcore, const void* data, uint32 data_size);
int __MTLK_IFUNC mtlk_core_cfg_get_ire_ctrl_b (mtlk_handle_t hcore, const void* data, uint32 data_size);
void mtlk_core_cfg_init_ire_ctrl(mtlk_core_t *core);
int  mtlk_core_cfg_recovery_ire_ctrl(mtlk_core_t *core);

int __MTLK_IFUNC mtlk_core_cfg_set_static_plan (mtlk_handle_t hcore, const void *data, uint32 data_size);

int __MTLK_IFUNC mtlk_core_send_ssb_mode (mtlk_core_t *core, const mtlk_ssb_mode_cfg_t *ssb_mode_cfg);
void __MTLK_IFUNC mtlk_core_read_ssb_mode (mtlk_core_t *core, mtlk_ssb_mode_cfg_t *ssb_mode_cfg, uint32 *data_size);
int __MTLK_IFUNC mtlk_core_set_ssb_mode (mtlk_handle_t hcore, const void *data, uint32 data_size);
int __MTLK_IFUNC mtlk_core_get_ssb_mode (mtlk_handle_t hcore, const void *data, uint32 data_size);
int __MTLK_IFUNC mtlk_core_cfg_set_atf_quotas (mtlk_handle_t hcore, const void *data, uint32 data_size);
int __MTLK_IFUNC mtlk_core_cfg_send_slow_probing_mask (mtlk_core_t *core, uint32 mask);
int __MTLK_IFUNC mtlk_core_receive_slow_probing_mask (mtlk_core_t *core, uint32 *mask);

void __MTLK_IFUNC core_cfg_change_chan_width_if_coex_en (mtlk_core_t *core, struct mtlk_chan_def *ccd);
int __MTLK_IFUNC mtlk_core_set_coex_cfg (mtlk_handle_t hcore, const void *data, uint32 data_size);
int __MTLK_IFUNC mtlk_core_get_coex_cfg (mtlk_handle_t hcore, const void *data, uint32 data_size);
int __MTLK_IFUNC mtlk_core_send_coex_config_req (mtlk_core_t *core, uint8 coex_mode, uint8 coex_enable);
int __MTLK_IFUNC mtlk_core_cfg_get_auth_cfg (mtlk_handle_t hcore, const void* data, uint32 data_size);
int __MTLK_IFUNC mtlk_core_cfg_set_auth_cfg (mtlk_handle_t hcore, const void* data, uint32 data_size);

int __MTLK_IFUNC mtlk_core_cfg_set_mcast_range (mtlk_handle_t hcore, const void* data, uint32 data_size);
int __MTLK_IFUNC mtlk_core_cfg_get_mcast_range_list_ipv4 (mtlk_handle_t hcore, const void* data, uint32 data_size);
int __MTLK_IFUNC mtlk_core_cfg_get_mcast_range_list_ipv6 (mtlk_handle_t hcore, const void* data, uint32 data_size);

int __MTLK_IFUNC mtlk_core_get_unconnected_station (mtlk_handle_t hcore, const void* data, uint32 data_size);
int __MTLK_IFUNC mtlk_core_cfg_set_max_mpdu_length (mtlk_handle_t hcore, const void *data, uint32 data_size);
int __MTLK_IFUNC mtlk_core_cfg_get_max_mpdu_length (mtlk_handle_t hcore, const void *data, uint32 data_size);
int __MTLK_IFUNC mtlk_core_cfg_send_max_mpdu_length (mtlk_core_t *core, const uint32 max_mpdu_length);
uint32 __MTLK_IFUNC mtlk_core_cfg_read_max_mpdu_length (mtlk_core_t *core);

int __MTLK_IFUNC mtlk_core_cfg_send_freq_jump_mode (mtlk_core_t *core, uint32 freq_jump_mode_en);
int __MTLK_IFUNC mtlk_core_cfg_set_freq_jump_mode (mtlk_handle_t hcore, const void *data, uint32 data_size);

int __MTLK_IFUNC mtlk_core_cfg_set_wds_wep_enc_cfg (mtlk_handle_t hcore, const void* data, uint32 data_size);
int __MTLK_IFUNC mtlk_core_cfg_get_wds_wep_enc_cfg (mtlk_handle_t hcore, const void* data, uint32 data_size);

int __MTLK_IFUNC mtlk_core_cfg_set_operating_mode (mtlk_handle_t hcore, const void* data, uint32 data_size);
BOOL __MTLK_IFUNC core_cfg_equal_range (struct mtlk_chan_def *chan1, struct mtlk_chan_def *chan2);
BOOL __MTLK_IFUNC core_cfg_channels_overlap (struct mtlk_chan_def *chandef1, struct mtlk_chan_def *chandef2);

int __MTLK_IFUNC mtlk_core_cfg_set_wds_wpa_entry (mtlk_handle_t hcore, const void* data, uint32 data_size);
void __MTLK_IFUNC mtlk_core_cfg_flush_wds_wpa_list (mtlk_core_t *nic);
BOOL __MTLK_IFUNC mtlk_core_cfg_wds_wpa_entry_exists (mtlk_core_t *nic, const IEEE_ADDR *addr);
int __MTLK_IFUNC mtlk_core_cfg_get_wds_wpa_entry (mtlk_handle_t hcore, const void* data, uint32 data_size);
int __MTLK_IFUNC mtlk_core_cfg_get_multi_ap_blacklist_entries (mtlk_handle_t hcore, const void* data, uint32 data_size);
int __MTLK_IFUNC mtlk_core_cfg_propagate_dfs_state (mtlk_handle_t hcore, const void* data, uint32 data_size);

void __MTLK_IFUNC mtlk_core_cfg_get_pd_threshold (mtlk_core_t *core, UMI_SET_PD_THRESH *pd_thresh);
int __MTLK_IFUNC mtlk_core_cfg_send_pd_threshold (mtlk_core_t *core, const UMI_SET_PD_THRESH *pd_thresh);
void __MTLK_IFUNC mtlk_core_cfg_get_restricted_ac_mode (mtlk_core_t *core, UMI_SET_RESTRICTED_AC *restricted_ac_mode);
int __MTLK_IFUNC mtlk_core_cfg_send_restricted_ac_mode (mtlk_core_t *core, const UMI_SET_RESTRICTED_AC *restricted_ac_mode);
int __MTLK_IFUNC mtlk_core_cfg_set_restricted_ac_mode_cfg (mtlk_handle_t hcore, const void *data, uint32 data_size);
int __MTLK_IFUNC mtlk_core_cfg_get_restricted_ac_mode_cfg (mtlk_handle_t hcore, const void *data, uint32 data_size);

int __MTLK_IFUNC mtlk_core_cfg_set_dgaf_disabled(mtlk_handle_t hcore, const void* data, uint32 data_size);
void __MTLK_IFUNC mtlk_core_cfg_set_block_tx (mtlk_core_t *core, int value);
int  __MTLK_IFUNC mtlk_core_cfg_get_block_tx (mtlk_core_t *core);

int __MTLK_IFUNC mtlk_core_set_fixed_ltf_and_gi (mtlk_handle_t hcore, const void *data, uint32 data_size);
int __MTLK_IFUNC mtlk_core_get_fixed_ltf_and_gi (mtlk_handle_t hcore, const void *data, uint32 data_size);
void __MTLK_IFUNC mtlk_core_read_fixed_ltf_and_gi (mtlk_core_t *core, mtlk_fixed_ltf_and_gi_t *fixed_ltf_and_gi, uint32 *data_size);

int __MTLK_IFUNC mtlk_core_receive_coex_config (mtlk_core_t *core, uint8 *enabled, uint8 *mode);
int __MTLK_IFUNC mtlk_core_set_mu_operation (mtlk_core_t *core, uint8 mu_operation);
int __MTLK_IFUNC mtlk_core_receive_mu_operation (mtlk_core_t *core, BOOL *mu_operation);
int __MTLK_IFUNC mtlk_core_set_rts_mode (mtlk_core_t *core, uint8 dynamic_bw, uint8 static_bw);
int __MTLK_IFUNC mtlk_core_receive_rts_mode (mtlk_core_t *core, mtlk_core_rts_mode_t *rts_params);
int __MTLK_IFUNC mtlk_core_set_reliable_multicast (mtlk_core_t *core, uint8 flag);
int __MTLK_IFUNC mtlk_core_receive_reliable_multicast (mtlk_core_t *core, uint8 *flag);
int __MTLK_IFUNC mtlk_core_set_rx_duty_cycle (mtlk_core_t *core, const mtlk_rx_duty_cycle_cfg_t *rx_duty_cycle_cfg);
int __MTLK_IFUNC mtlk_core_cfg_set_rx_duty_cycle (mtlk_handle_t hcore, const void* data, uint32 data_size);
int __MTLK_IFUNC mtlk_core_cfg_get_rx_duty_cycle (mtlk_handle_t hcore, const void* data, uint32 data_size);
int __MTLK_IFUNC mtlk_core_set_rx_threshold (mtlk_core_t *core, const mtlk_rx_th_cfg_t *rx_th_cfg);
int __MTLK_IFUNC mtlk_core_cfg_set_rx_threshold (mtlk_handle_t hcore, const void* data, uint32 data_size);
int __MTLK_IFUNC mtlk_core_cfg_get_rx_threshold (mtlk_handle_t hcore, const void* data, uint32 data_size);
int __MTLK_IFUNC mtlk_core_send_txop_mode (mtlk_core_t *core, uint32 sid, uint32 txop_mode);
int __MTLK_IFUNC mtlk_core_receive_txop_mode (mtlk_core_t *core, mtlk_core_txop_mode_t *txop_mode);
int __MTLK_IFUNC mtlk_core_set_admission_capacity (mtlk_core_t *core, uint32 value);
int __MTLK_IFUNC mtlk_core_receive_admission_capacity (mtlk_core_t *core, uint32 *value);
int __MTLK_IFUNC mtlk_core_cfg_send_fast_drop (mtlk_core_t *core, uint8 fast_drop);
int __MTLK_IFUNC mtlk_core_cfg_set_fast_drop (mtlk_core_t *core, uint8 fast_drop);
int __MTLK_IFUNC mtlk_core_receive_fast_drop (mtlk_core_t *core, uint8 *fast_drop);
int __MTLK_IFUNC mtlk_core_send_erp_cfg (mtlk_core_t *core, mtlk_erp_cfg_t *erp_cfg);

int wave_core_cfg_send_rcvry_msg(mtlk_handle_t hcore, const void *data, uint32 data_size);

int __MTLK_IFUNC    wave_core_get_pvt_sensor(mtlk_handle_t hcore, const void *data, uint32 data_size);
int __MTLK_IFUNC    wave_core_set_test_bus_mode(mtlk_handle_t hcore, const void *data, uint32 data_size);
int __MTLK_IFUNC    wave_core_send_test_bus_mode(mtlk_core_t *core, const uint32 mode);

int __MTLK_IFUNC mtlk_core_cfg_set_ssid (mtlk_core_t *core, const u8 *ssid, u8 ssid_len);
int __MTLK_IFUNC mtlk_core_cfg_get_counters_src (mtlk_handle_t hcore, const void* data, uint32 data_size);
int __MTLK_IFUNC mtlk_core_cfg_set_counters_src (mtlk_handle_t hcore, const void* data, uint32 data_size);

int __MTLK_IFUNC mtlk_core_get_tpc_cfg (mtlk_handle_t hcore, const void* data, uint32 data_size);
int __MTLK_IFUNC mtlk_core_set_tpc_cfg (mtlk_handle_t hcore, const void* data, uint32 data_size);
int __MTLK_IFUNC mtlk_reload_tpc_wrapper (struct nic *nic, uint32 channel, psdb_pw_limits_t *pwl);
int __MTLK_IFUNC wave_core_cfg_get_rts_rate (mtlk_handle_t hcore, const void* data, uint32 data_size);
int __MTLK_IFUNC wave_core_cfg_set_rts_rate (mtlk_handle_t hcore, const void* data, uint32 data_size);
int __MTLK_IFUNC wave_core_cfg_recover_cutoff_point (mtlk_core_t *core);
unsigned int __MTLK_IFUNC wave_core_cfg_get_nof_channels (mtlk_hw_band_e  hw_band);

/* These 6 functions are required for further Recovery integration of Dynamic MU API */
int __MTLK_IFUNC wave_core_cfg_send_dynamic_mu_type(mtlk_core_t *core, const UMI_DYNAMIC_MU_TYPE *dynamic_mu_type);
int __MTLK_IFUNC wave_core_cfg_send_he_mu_fixed_parameters(mtlk_core_t *core, const UMI_HE_MU_FIXED_PARAMTERS *he_mu_fixed_parameters);
int __MTLK_IFUNC wave_core_cfg_send_he_mu_duration(mtlk_core_t *core, const UMI_HE_MU_DURATION *he_mu_duration);
void __MTLK_IFUNC wave_core_cfg_get_dynamic_mu_type(mtlk_core_t *core, UMI_DYNAMIC_MU_TYPE *dynamic_mu_type);
void __MTLK_IFUNC wave_core_cfg_get_he_mu_fixed_parameters(mtlk_core_t *core, UMI_HE_MU_FIXED_PARAMTERS *he_mu_fixed_parameters);
void __MTLK_IFUNC wave_core_cfg_get_he_mu_duration(mtlk_core_t *core, UMI_HE_MU_DURATION *he_mu_duration);
int __MTLK_IFUNC wave_core_cfg_set_dynamic_mu_cfg(mtlk_handle_t hcore, const void *data, uint32 data_size);
int __MTLK_IFUNC wave_core_cfg_get_dynamic_mu_cfg(mtlk_handle_t hcore, const void *data, uint32 data_size);

#endif /*__CORE_CONFIG_H__*/
