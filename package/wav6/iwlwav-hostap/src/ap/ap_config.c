/*
 * hostapd / Configuration helper functions
 * Copyright (c) 2003-2014, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "utils/includes.h"

#include "utils/common.h"
#include "crypto/sha1.h"
#include "crypto/tls.h"
#include "radius/radius_client.h"
#include "common/ieee802_11_defs.h"
#include "common/eapol_common.h"
#include "common/dhcp.h"
#include "eap_common/eap_wsc_common.h"
#include "eap_server/eap.h"
#include "wpa_auth.h"
#include "sta_info.h"
#include "ap_config.h"
#include "drivers/driver.h"


static void hostapd_config_free_vlan(struct hostapd_bss_config *bss)
{
	struct hostapd_vlan *vlan, *prev;

	vlan = bss->vlan;
	prev = NULL;
	while (vlan) {
		prev = vlan;
		vlan = vlan->next;
		os_free(prev);
	}

	bss->vlan = NULL;
}


#ifndef DEFAULT_WPA_DISABLE_EAPOL_KEY_RETRIES
#define DEFAULT_WPA_DISABLE_EAPOL_KEY_RETRIES 0
#endif /* DEFAULT_WPA_DISABLE_EAPOL_KEY_RETRIES */

enum multibss_aid_offset {
	NON_MULTIBSS_AID_OFFSET = 0,
	MULTIBSS_AID_OFFSET = 64
};

void hostapd_config_defaults_bss(struct hostapd_bss_config *bss)
{
	dl_list_init(&bss->anqp_elem);

	bss->logger_syslog_level = HOSTAPD_LEVEL_INFO;
	bss->logger_stdout_level = HOSTAPD_LEVEL_INFO;
	bss->logger_syslog = (unsigned int) -1;
	bss->logger_stdout = (unsigned int) -1;

	bss->auth_algs = WPA_AUTH_ALG_OPEN | WPA_AUTH_ALG_SHARED;

	bss->wep_rekeying_period = 300;
	/* use key0 in individual key and key1 in broadcast key */
	bss->broadcast_key_idx_min = 1;
	bss->broadcast_key_idx_max = 2;
	bss->eap_reauth_period = 3600;

	bss->wpa_group_rekey = 600;
	bss->wpa_gmk_rekey = 86400;
	bss->wpa_group_update_count = 4;
	bss->wpa_pairwise_update_count = 4;
	bss->wpa_disable_eapol_key_retries =
		DEFAULT_WPA_DISABLE_EAPOL_KEY_RETRIES;
	bss->wpa_key_mgmt = WPA_KEY_MGMT_PSK;
	bss->wpa_pairwise = WPA_CIPHER_TKIP;
	bss->wpa_group = WPA_CIPHER_TKIP;
	bss->rsn_pairwise = 0;

	bss->max_num_sta = MAX_STA_COUNT;
	bss->num_res_sta = 0;

	bss->dtim_period = 2;
	bss->management_frames_rate = MGMT_FRAMES_RATE_DEFAULT;

	bss->radius_server_auth_port = 1812;
	bss->eap_sim_db_timeout = 1;
	bss->ap_max_inactivity = AP_MAX_INACTIVITY;
	bss->eapol_version = EAPOL_VERSION;

	bss->max_listen_interval = 65535;

	bss->pwd_group = 19; /* ECC: GF(p=256) */

#ifdef CONFIG_IEEE80211W
	bss->assoc_sa_query_max_timeout = 1000;
	bss->assoc_sa_query_retry_timeout = 201;
	bss->group_mgmt_cipher = WPA_CIPHER_AES_128_CMAC;
#endif /* CONFIG_IEEE80211W */
#ifdef EAP_SERVER_FAST
	 /* both anonymous and authenticated provisioning */
	bss->eap_fast_prov = 3;
	bss->pac_key_lifetime = 7 * 24 * 60 * 60;
	bss->pac_key_refresh_time = 1 * 24 * 60 * 60;
#endif /* EAP_SERVER_FAST */

	/* Set to -1 as defaults depends on HT in setup */
	bss->wmm_enabled = -1;

#ifdef CONFIG_IEEE80211R_AP
	bss->ft_over_ds = 1;
	bss->rkh_pos_timeout = 86400;
	bss->rkh_neg_timeout = 60;
	bss->rkh_pull_timeout = 1000;
	bss->rkh_pull_retries = 4;
	bss->r0_key_lifetime = 1209600;
#endif /* CONFIG_IEEE80211R_AP */

	bss->radius_das_time_window = 300;

	bss->sae_anti_clogging_threshold = 5;
	bss->sae_sync = 5;

	bss->gas_frag_limit = 1400;

#ifdef CONFIG_FILS
	dl_list_init(&bss->fils_realms);
	bss->fils_hlp_wait_time = 30;
	bss->dhcp_server_port = DHCP_SERVER_PORT;
	bss->dhcp_relay_port = DHCP_SERVER_PORT;
#endif /* CONFIG_FILS */

	bss->broadcast_deauth = 1;

#ifdef CONFIG_MBO
	bss->mbo_cell_data_conn_pref = -1;
	bss->mbo_cell_aware = 1;
	bss->mbo_pmf_bypass = 1;
#endif /* CONFIG_MBO */

	/* Disable TLS v1.3 by default for now to avoid interoperability issue.
	 * This can be enabled by default once the implementation has been fully
	 * completed and tested with other implementations. */
	bss->tls_flags = TLS_CONN_DISABLE_TLSv1_3;
	bss->send_probe_response = 1;

#ifdef CONFIG_HS20
	bss->hs20_release = (HS20_VERSION >> 4) + 1;
#endif /* CONFIG_HS20 */

	/* Default to strict CRL checking. */
	bss->check_crl_strict = 1;

	/* default mesh mode */
	bss->mesh_mode = MESH_MODE_DEFAULT;

	bss->sAggrConfigSize = 0; /* set the size to 0 to mark sAggrConfig as unintialized */

	bss->s11nProtection = 1; /* enabled */

	bss->sUdmaEnabled = 0;
	bss->sUdmaVlanId = 0;

	/* default rrm*/
	bss->radio_measurements[0] = WLAN_RRM_CAPS_LINK_MEASUREMENT |
		WLAN_RRM_CAPS_BEACON_REPORT_PASSIVE | WLAN_RRM_CAPS_BEACON_REPORT_TABLE;
	bss->radio_measurements[1] = WLAN_RRM_CAPS_STATISTICS_MEASUREMENT |
		WLAN_RRM_CAPS_CHANNEL_LOAD | WLAN_RRM_CAPS_NOISE_HISTOGRAM;

	bss->scan_timeout = SCAN_TIMEOUT_DEFAULT;

	bss->sDisableSoftblock = 0; /* Softblock Enabled */
}

#ifdef CONFIG_ACS
static const int acs_numbss_coeflist_defaults[ACS_NUMBSS_NUM_COEFS + 1] =
{ 4, 0, 2, 0, 0, 0, 2, 0, 1, 0, 0, 0, 9, 3, 1, -1 };
#endif /* CONFIG_ACS */

struct hostapd_config * hostapd_config_defaults(void)
{
#define ecw2cw(ecw) ((1 << (ecw)) - 1)

	struct hostapd_config *conf;
	struct hostapd_bss_config *bss;
	const int aCWmin = 4, aCWmax = 10;
	const struct hostapd_wmm_ac_params ac_bk =
		{ aCWmin, aCWmax, 7, 0, 0 }; /* background traffic */
	const struct hostapd_wmm_ac_params ac_be =
		{ aCWmin, aCWmax, 3, 0, 0 }; /* best effort traffic */
	const struct hostapd_wmm_ac_params ac_vi = /* video traffic */
		{ aCWmin - 1, aCWmin, 2, 3008 / 32, 0 };
	const struct hostapd_wmm_ac_params ac_vo = /* voice traffic */
		{ aCWmin - 2, aCWmin - 1, 2, 1504 / 32, 0 };
	const struct hostapd_tx_queue_params txq_bk =
		{ 7, ecw2cw(aCWmin), ecw2cw(aCWmax), 0 };
	const struct hostapd_tx_queue_params txq_be =
		{ 3, ecw2cw(aCWmin), 4 * (ecw2cw(aCWmin) + 1) - 1, 0};
	const struct hostapd_tx_queue_params txq_vi =
		{ 1, (ecw2cw(aCWmin) + 1) / 2 - 1, ecw2cw(aCWmin), 30};
	const struct hostapd_tx_queue_params txq_vo =
		{ 1, (ecw2cw(aCWmin) + 1) / 4 - 1,
		  (ecw2cw(aCWmin) + 1) / 2 - 1, 15};

#undef ecw2cw

	conf = os_zalloc(sizeof(*conf));
	bss = os_zalloc(sizeof(*bss));
	if (conf == NULL || bss == NULL) {
		wpa_printf(MSG_ERROR, "Failed to allocate memory for "
			   "configuration data.");
		os_free(conf);
		os_free(bss);
		return NULL;
	}
	conf->bss = os_calloc(1, sizeof(struct hostapd_bss_config *));
	if (conf->bss == NULL) {
		os_free(conf);
		os_free(bss);
		return NULL;
	}
	conf->bss[0] = bss;

	bss->radius = os_zalloc(sizeof(*bss->radius));
	if (bss->radius == NULL) {
		os_free(conf->bss);
		os_free(conf);
		os_free(bss);
		return NULL;
	}

	hostapd_config_defaults_bss(bss);

	conf->num_bss = 1;

	conf->beacon_int = 100;
	conf->sDisableMasterVap = 0;
	conf->rts_threshold = -2; /* use driver default: 2347 */
	conf->fragm_threshold = -2; /* user driver default: 2346 */

	/* Set to invalid value means do not add Power Constraint IE */
	conf->local_pwr_constraint = -1;
	conf->ap_max_num_sta = MAX_STA_COUNT;

	conf->wmm_ac_params[0] = ac_be;
	conf->wmm_ac_params[1] = ac_bk;
	conf->wmm_ac_params[2] = ac_vi;
	conf->wmm_ac_params[3] = ac_vo;

	conf->tx_queue[0] = txq_vo;
	conf->tx_queue[1] = txq_vi;
	conf->tx_queue[2] = txq_be;
	conf->tx_queue[3] = txq_bk;

	conf->ht_rifs = 1;
	conf->ht_capab = HT_CAP_INFO_SMPS_DISABLED;
	conf->ht_tx_bf_capab = 0;

	conf->scan_passive_dwell = 20;
	conf->scan_active_dwell = 10;
	conf->scan_passive_total_per_channel = 200;
	conf->scan_active_total_per_channel = 20;
	conf->channel_transition_delay_factor = 5;
	conf->scan_activity_threshold = 25;
	conf->obss_beacon_rssi_threshold = -60;

	conf->ap_table_max_size = 255;
	conf->ap_table_expiration_time = 60;
	conf->track_sta_max_age = 180;
	conf->assoc_rsp_rx_mcs_mask = 1;
	conf->ieee80211n_acax_compat = 1;

#ifdef CONFIG_TESTING_OPTIONS
	conf->ignore_probe_probability = 0.0;
	conf->ignore_auth_probability = 0.0;
	conf->ignore_assoc_probability = 0.0;
	conf->ignore_reassoc_probability = 0.0;
	conf->corrupt_gtk_rekey_mic_probability = 0.0;
	conf->ecsa_ie_only = 0;
#endif /* CONFIG_TESTING_OPTIONS */

	conf->acs = 0;
	conf->acs_ch_list.num = 0;
#ifdef CONFIG_ACS
  {
    int i;
    int grp_priorities_throughput[ACS_NUM_GRP_PRIORITIES] = { 3, 2, 1, 0 };
    int grp_priorities_reach[ACS_NUM_GRP_PRIORITIES] = { 0, 1, 2, 3 };
    int acs_bw_threshold[ACS_BW_THRESH_NUM] = { 20, 20, 20 };
	int acs_penalty_factors[ACS_NUM_PENALTY_FACTORS] = {1, 0, 0, 0, 1, 0, 1, 0, 1, 1, 0};
	int acs_to_degradation[ACS_NUM_DEGRADATION_FACTORS] = {1, 1, 1, 1, 1, 1, 100};
	int sCoCPower[] = {0, 4, 4};
	int sRTSmode[] = {0, 0};
	int sInterferDetThresh[] = {-68, -68, -68, -68, 5, -68};
	int sCcaAdapt[] = {10, 5, -30, 10, 5, 30, 60};

	conf->acs_num_scans = 5;
	conf->acs_algo = ACS_ALGO_SMART;
	conf->acs_numbss_info_file = strdup("/tmp/acs_numbss_info.txt");
	if (NULL == conf->acs_numbss_info_file) goto fail;
	conf->acs_numbss_coeflist = os_malloc(sizeof(int) * (ACS_NUMBSS_NUM_COEFS + 1));
	if (NULL == conf->acs_numbss_coeflist) goto fail;
	memcpy(conf->acs_numbss_coeflist, acs_numbss_coeflist_defaults, sizeof(acs_numbss_coeflist_defaults));

    /* SmartACS */

    conf->acs_smart_info_file = strdup("/tmp/acs_smart_info.txt");
    conf->acs_history_file = strdup("/tmp/acs_history.txt");
    conf->acs_init_done = 0;

    for (i = 0; i < ACS_MAX_CHANNELS; i++)
      conf->acs_chan_cust_penalty[i] = 50;

    for (i = 0; i < ACS_MAX_CHANNELS; i++)
      conf->acs_chan_noise_penalty[i] = 50;

    conf->acs_fallback_chan.primary = 1;
    conf->acs_fallback_chan.secondary = 0;
    conf->acs_fallback_chan.width = 20;

    conf->acs_penalty_factors = os_malloc(sizeof(int) * (ACS_NUM_PENALTY_FACTORS + 1));
    if (NULL == conf->acs_penalty_factors)
		goto fail;
	conf->acs_penalty_factors[ACS_NUM_PENALTY_FACTORS] = 0;
	memcpy(conf->acs_penalty_factors, acs_penalty_factors, sizeof(acs_penalty_factors));

    conf->acs_to_degradation = os_malloc(sizeof(int) * (ACS_NUM_DEGRADATION_FACTORS + 1));
    if (NULL == conf->acs_to_degradation)
		goto fail;
    conf->acs_to_degradation[ACS_NUM_DEGRADATION_FACTORS] = 0;
	memcpy(conf->acs_to_degradation, acs_to_degradation, sizeof(acs_to_degradation));

    conf->acs_grp_priorities_throughput = os_malloc(sizeof(int) * ACS_NUM_GRP_PRIORITIES);
    if (NULL == conf->acs_grp_priorities_throughput)
		goto fail;
    memcpy(conf->acs_grp_priorities_throughput, grp_priorities_throughput, sizeof(grp_priorities_throughput));

    for (i = 0; i < ACS_NUM_GRP_PRIORITIES; i++) {
      conf->acs_grp_prio_tp_map[conf->acs_grp_priorities_throughput[i]] = i;
    }

    conf->acs_grp_priorities_reach = os_malloc(sizeof(int) * ACS_NUM_GRP_PRIORITIES);
    if (NULL == conf->acs_grp_priorities_reach)
		goto fail;
    memcpy(conf->acs_grp_priorities_reach, grp_priorities_reach, sizeof(grp_priorities_reach));

    for (i = 0; i < ACS_NUM_GRP_PRIORITIES; i++) {
      conf->acs_grp_prio_reach_map[conf->acs_grp_priorities_reach[i]] = i;
    }

    conf->acs_vht_dynamic_bw = 0;
    conf->acs_policy = 0;

    conf->acs_switch_thresh = 20;
    conf->acs_bw_comparison  =  0;

    conf->acs_bw_threshold = os_malloc(sizeof(int) * ACS_BW_THRESH_NUM);
    if (NULL == conf->acs_bw_threshold)
		goto fail;
    memcpy(conf->acs_bw_threshold, acs_bw_threshold, sizeof(acs_bw_threshold));

	conf->sPowerSelection = 0; /* 100% */

	conf->sCoCPower = os_malloc(sizeof(int) * COC_POWER_SIZE);
	if (NULL == conf->sCoCPower)
		goto fail;
	memcpy(conf->sCoCPower, sCoCPower, sizeof(sCoCPower));

	conf->sRTSmode = os_malloc(sizeof(int) * RT_MODE_SIZE);
	if (NULL == conf->sRTSmode)
		goto fail;
	memcpy(conf->sRTSmode, sRTSmode, sizeof(sRTSmode));

	conf->sInterferDetThresh = os_malloc(sizeof(int) * INTERFER_DEF_THRESH_SIZE);
	if (NULL == conf->sInterferDetThresh)
		goto fail;
	memcpy(conf->sInterferDetThresh, sInterferDetThresh, sizeof(sInterferDetThresh));

	conf->sCcaAdapt = os_malloc(sizeof(int) * CCA_ADAPT_SIZE);
	if (NULL == conf->sCcaAdapt)
		goto fail;
	memcpy(conf->sCcaAdapt, sCcaAdapt, sizeof(sCcaAdapt));

	goto exit;

  fail:
    wpa_printf(MSG_ERROR, "Failed to allocate memory for configuration data.");

    if (conf->acs_bw_threshold) os_free(conf->acs_bw_threshold);
    if (conf->acs_numbss_info_file) os_free(conf->acs_numbss_info_file);
    if (conf->acs_numbss_coeflist) os_free(conf->acs_numbss_coeflist);
    if (conf->acs_grp_priorities_reach) os_free(conf->acs_grp_priorities_reach);
    if (conf->acs_grp_priorities_throughput) os_free(conf->acs_grp_priorities_throughput);
    if (conf->acs_penalty_factors) os_free(conf->acs_penalty_factors);
    if (conf->acs_to_degradation) os_free(conf->acs_to_degradation);
    if (conf->acs_smart_info_file) os_free(conf->acs_smart_info_file);
    if (conf->acs_history_file) os_free(conf->acs_history_file);

    os_free(conf->bss);
    os_free(conf);
    os_free(bss->radius);
    os_free(bss);
    return NULL;
  }
exit:
#endif /* CONFIG_ACS */

	/* The third octet of the country string uses an ASCII space character
	 * by default to indicate that the regulations encompass all
	 * environments for the current frequency band in the country. */
	conf->country[2] = ' ';
	conf->rssi_reject_assoc_rssi = 0;
	conf->rssi_reject_assoc_timeout = 30;

	conf->he_capab.he_mac_capab_info[HE_MACCAP_CAP0_IDX] |= 
										HE_MAC_CAP0_HTC_HE_SUPPORT;
	conf->he_capab.he_mac_capab_info[HE_MACCAP_CAP0_IDX] |=
										HE_MAC_CAP0_TWT_REQUESTER_SUPPORT;
	conf->he_capab.he_mac_capab_info[HE_MACCAP_CAP0_IDX] |=
							set_he_cap(7, HE_MAC_CAP0_MAX_NUM_OF_FRAG_MSDU);
	conf->he_capab.he_mac_capab_info[HE_MACCAP_CAP1_IDX] |=
						set_he_cap(7, HE_MAC_CAP1_MULTI_TID_AGGR_RX_SUPPORT);
	conf->he_capab.he_mac_capab_info[HE_MACCAP_CAP3_IDX] |=
							set_he_cap(2, HE_MAC_CAP3_MAX_AMPDU_LEN_EXP_EXT);
	conf->he_capab.he_mac_capab_info[HE_MACCAP_CAP4_IDX] |=
						set_he_cap(1, HE_MAC_CAP4_MULTI_TID_AGGR_TX_SUPPORT);
	conf->he_capab.he_mac_capab_info[HE_MACCAP_CAP5_IDX] |=
						set_he_cap(3, HE_MAC_CAP5_MULTI_TID_AGGR_TX_SUPPORT);
	conf->he_capab.he_mac_capab_info[HE_MACCAP_CAP3_IDX] |=
										HE_MAC_CAP3_OM_CONTROL_SUPPORT;
	conf->he_capab.he_mac_capab_info[HE_MACCAP_CAP2_IDX] |=
							HE_MAC_CAP2_ACK_ENABLED_AGGREGATION_SUPPORT;
	conf->he_capab.he_mac_capab_info[HE_MACCAP_CAP4_IDX] |=
									HE_MAC_CAP4_AMSDU_IN_AMPDU_SUPPORT;
	conf->he_capab.he_mac_capab_info[HE_MACCAP_CAP5_IDX] |=
									HE_MAC_CAP5_OM_CONTROL_UL_MU_DATA_DIS_RX_SUP;
	conf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP1_IDX] |=
												HE_PHY_CAP1_DEVICE_CLASS;
	conf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP0_IDX] |=
											HE_PHY_CAP0_CHANNEL_WIDTH_SET_B0;
	conf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP0_IDX] |=
											HE_PHY_CAP0_CHANNEL_WIDTH_SET_B1;
	conf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP0_IDX] |=
											HE_PHY_CAP0_CHANNEL_WIDTH_SET_B2;
	conf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP2_IDX] |=
								HE_PHY_CAP2_STBC_TX_LESS_OR_EQUAL_80MHz;
	conf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP1_IDX] |=
									HE_PHY_CAP1_LDPC_CODING_IN_PAYLOAD;
	conf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP3_IDX] |=
												HE_PHY_CAP3_SU_BEAMFORMER;
	conf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP4_IDX] |=
												HE_PHY_CAP4_SU_BEAMFORMEE;
	conf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP4_IDX] |=
												HE_PHY_CAP4_MU_BEAMFORMER;
	conf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP4_IDX] |=
						set_he_cap(3, HE_PHY_CAP4_BF_STS_LESS_OR_EQ_80MHz);
	conf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP4_IDX] |=
						set_he_cap(3, HE_PHY_CAP4_BF_STS_GREATER_THAN_80MHz);
	conf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP5_IDX] |=
						set_he_cap(3, HE_PHY_CAP5_NUM_SOUND_DIM_LESS_80MHz);
	conf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP5_IDX] |=
						set_he_cap(3, HE_PHY_CAP5_NUM_SOUND_DIM_GREAT_80MHz);
	conf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP7_IDX] |=
									set_he_cap(3, HE_PHY_CAP7_MAX_NC);
	conf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP6_IDX] |=
							HE_PHY_CAP6_TRIGGERED_SU_BEAMFORMING_FEEDBACK;
	conf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP6_IDX] |=
									HE_PHY_CAP6_TRIGGERED_CQI_FEEDBACK;
	conf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP6_IDX] |=
										HE_PHY_CAP6_PPE_THRESHOLD_PRESENT;
	conf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP7_IDX] |=
					HE_PHY_CAP7_SU_PPDU_AND_HE_MU_WITH_4X_HE_LTF_0_8US_GI;
	conf->he_capab.he_ppe_thresholds[HE_PPE_CAP0_IDX] |=
										set_he_cap(3, HE_PPE_CAP0_NSS_M1);
	conf->he_capab.he_ppe_thresholds[HE_PPE_CAP0_IDX] |=
							set_he_cap(15, HE_PPE_CAP0_RU_INDEX_BITMASK);
	conf->he_capab.he_ppe_thresholds[HE_PPE_CAP1_IDX] |=
						set_he_cap(7, HE_PPE_CAP1_PPET8_FOR_NSS1_FOR_RU0);
	conf->he_capab.he_ppe_thresholds[HE_PPE_CAP2_IDX] |=
						set_he_cap(7, HE_PPE_CAP2_PPET8_FOR_NSS1_FOR_RU1);
	conf->he_capab.he_ppe_thresholds[HE_PPE_CAP2_IDX] |=
						set_he_cap(3,HE_PPE_CAP2_PPET8_FOR_NSS1_FOR_RU2 );
	conf->he_capab.he_ppe_thresholds[HE_PPE_CAP3_IDX] |=
						set_he_cap(1, HE_PPE_CAP3_PPET8_FOR_NSS1_FOR_RU2);
	conf->he_capab.he_ppe_thresholds[HE_PPE_CAP3_IDX] |=
						set_he_cap(7, HE_PPE_CAP3_PPET8_FOR_NSS1_FOR_RU3);
	conf->he_capab.he_ppe_thresholds[HE_PPE_CAP4_IDX] |=
						set_he_cap(7, HE_PPE_CAP4_PPET8_FOR_NSS2_FOR_RU0);
	conf->he_capab.he_ppe_thresholds[HE_PPE_CAP5_IDX] |=
						set_he_cap(7, HE_PPE_CAP5_PPET8_FOR_NSS2_FOR_RU1);
	conf->he_capab.he_ppe_thresholds[HE_PPE_CAP5_IDX] |=
						set_he_cap(3, HE_PPE_CAP5_PPET8_FOR_NSS2_FOR_RU2);
	conf->he_capab.he_ppe_thresholds[HE_PPE_CAP6_IDX] |=
						set_he_cap(1, HE_PPE_CAP6_PPET8_FOR_NSS2_FOR_RU2);
	conf->he_capab.he_ppe_thresholds[HE_PPE_CAP6_IDX] |=
						set_he_cap(7, HE_PPE_CAP6_PPET8_FOR_NSS2_FOR_RU3);
	conf->he_capab.he_ppe_thresholds[HE_PPE_CAP7_IDX] |=
						set_he_cap(7, HE_PPE_CAP7_PPET8_FOR_NSS3_FOR_RU0);
	conf->he_capab.he_ppe_thresholds[HE_PPE_CAP8_IDX] |=
						set_he_cap(7, HE_PPE_CAP8_PPET8_FOR_NSS3_FOR_RU1);
	conf->he_capab.he_ppe_thresholds[HE_PPE_CAP8_IDX] |=
						set_he_cap(3, HE_PPE_CAP8_PPET8_FOR_NSS3_FOR_RU2);
	conf->he_capab.he_ppe_thresholds[HE_PPE_CAP9_IDX] |=
						set_he_cap(1, HE_PPE_CAP9_PPET8_FOR_NSS3_FOR_RU2);
	conf->he_capab.he_ppe_thresholds[HE_PPE_CAP9_IDX] |=
						set_he_cap(7, HE_PPE_CAP9_PPET8_FOR_NSS3_FOR_RU3);
	conf->he_capab.he_ppe_thresholds[HE_PPE_CAP10_IDX] |=
						set_he_cap(7, HE_PPE_CAP10_PPET8_FOR_NSS4_FOR_RU0);
	conf->he_capab.he_ppe_thresholds[HE_PPE_CAP11_IDX] |=
						set_he_cap(7, HE_PPE_CAP11_PPET8_FOR_NSS4_FOR_RU1);
	conf->he_capab.he_ppe_thresholds[HE_PPE_CAP11_IDX] |=
						set_he_cap(3, HE_PPE_CAP11_PPET8_FOR_NSS4_FOR_RU2);
	conf->he_capab.he_ppe_thresholds[HE_PPE_CAP12_IDX] |=
						set_he_cap(1, HE_PPE_CAP12_PPET8_FOR_NSS4_FOR_RU2);
	conf->he_capab.he_ppe_thresholds[HE_PPE_CAP12_IDX] |=
						set_he_cap(7, HE_PPE_CAP12_PPET8_FOR_NSS4_FOR_RU3);
	conf->he_capab.he_txrx_mcs_support[0] = 0xaa;
	conf->he_capab.he_txrx_mcs_support[1] = 0xff;
	conf->he_capab.he_txrx_mcs_support[2] = 0xaa;
	conf->he_capab.he_txrx_mcs_support[3] = 0xff;
	conf->he_capab.he_txrx_mcs_support[4] = 0xaa;
	conf->he_capab.he_txrx_mcs_support[5] = 0xff;
	conf->he_capab.he_txrx_mcs_support[6] = 0xaa;
	conf->he_capab.he_txrx_mcs_support[7] = 0xff;
	conf->he_oper.he_oper_params[HE_OPERATION_CAP0_IDX] |=
					set_he_cap(4, HE_OPERATION_CAP0_DEFAULT_PE_DURATION);
	conf->he_oper.he_oper_params[HE_OPERATION_CAP0_IDX] |=
						set_he_cap(15, HE_OPERATION_CAP0_TXOP_DUR_RTS_TH);
	conf->he_oper.he_oper_params[HE_OPERATION_CAP1_IDX] |=
						set_he_cap(1, HE_OPERATION_CAP1_TXOP_DUR_RTS_TH);
	conf->he_oper.he_mcs_nss_set[0] |=
			set_he_cap(IEEE80211_HE_MCS_NOT_SUPPORTED, HE_MCS_NSS_FOR_2SS);
	conf->he_oper.he_mcs_nss_set[0] |=
			set_he_cap(IEEE80211_HE_MCS_NOT_SUPPORTED, HE_MCS_NSS_FOR_3SS);
	conf->he_oper.he_mcs_nss_set[0] |=
			set_he_cap(IEEE80211_HE_MCS_NOT_SUPPORTED, HE_MCS_NSS_FOR_4SS);
	conf->he_oper.he_mcs_nss_set[1] |=
			set_he_cap(IEEE80211_HE_MCS_NOT_SUPPORTED, HE_MCS_NSS_FOR_5SS);
	conf->he_oper.he_mcs_nss_set[1] |=
			set_he_cap(IEEE80211_HE_MCS_NOT_SUPPORTED, HE_MCS_NSS_FOR_6SS);
	conf->he_oper.he_mcs_nss_set[1] |=
			set_he_cap(IEEE80211_HE_MCS_NOT_SUPPORTED, HE_MCS_NSS_FOR_7SS);
	conf->he_oper.he_mcs_nss_set[1] |=
			set_he_cap(IEEE80211_HE_MCS_NOT_SUPPORTED, HE_MCS_NSS_FOR_8SS);
	conf->he_oper.he_oper_params[HE_OPERATION_CAP2_IDX] |=
										 HE_OPERATION_CAP2_ER_SU_DISABLE;
	conf->he_oper.he_oper_params[HE_OPERATION_CAP1_IDX] |=
										HE_OPERATION_CAP1_CO_LOCATED_BSS;
	conf->he_oper.max_co_located_bssid_ind = 5;
	conf->he_mu_edca.he_qos_info |= HE_QOS_INFO_QUEUE_REQUEST;
	conf->he_mu_edca.he_mu_ac_be_param[HE_MU_AC_PARAM_ACI_AIFSN_IDX] |=
									set_he_cap(3, HE_MU_AC_PARAM_AIFSN);
	conf->he_mu_edca.he_mu_ac_be_param[HE_MU_AC_PARAM_ECWMIN_ECWMAX_IDX] |=
									set_he_cap(15, HE_MU_AC_PARAM_ECWMIN);
	conf->he_mu_edca.he_mu_ac_be_param[HE_MU_AC_PARAM_ECWMIN_ECWMAX_IDX] |=
									set_he_cap(15, HE_MU_AC_PARAM_ECWMAX);
	conf->he_mu_edca.he_mu_ac_be_param[HE_MU_AC_PARAM_MU_EDCA_TIMER_IDX] = 13;
	conf->he_mu_edca.he_mu_ac_bk_param[HE_MU_AC_PARAM_ACI_AIFSN_IDX] |=
										set_he_cap(7, HE_MU_AC_PARAM_AIFSN);
	conf->he_mu_edca.he_mu_ac_bk_param[HE_MU_AC_PARAM_ACI_AIFSN_IDX] |=
										set_he_cap(1, HE_MU_AC_PARAM_ACI);
	conf->he_mu_edca.he_mu_ac_bk_param[HE_MU_AC_PARAM_ECWMIN_ECWMAX_IDX] |=
									set_he_cap(15, HE_MU_AC_PARAM_ECWMIN);
	conf->he_mu_edca.he_mu_ac_bk_param[HE_MU_AC_PARAM_ECWMIN_ECWMAX_IDX] |=
									set_he_cap(15, HE_MU_AC_PARAM_ECWMAX);
	conf->he_mu_edca.he_mu_ac_bk_param[HE_MU_AC_PARAM_MU_EDCA_TIMER_IDX] = 13;
	conf->he_mu_edca.he_mu_ac_vi_param[HE_MU_AC_PARAM_ACI_AIFSN_IDX] |=
									set_he_cap(2, HE_MU_AC_PARAM_AIFSN);
	conf->he_mu_edca.he_mu_ac_vi_param[HE_MU_AC_PARAM_ECWMIN_ECWMAX_IDX] |=
								set_he_cap(15, HE_MU_AC_PARAM_ECWMIN);
	conf->he_mu_edca.he_mu_ac_vi_param[HE_MU_AC_PARAM_ECWMIN_ECWMAX_IDX] |=
								set_he_cap(15, HE_MU_AC_PARAM_ECWMAX);
	conf->he_mu_edca.he_mu_ac_vi_param[HE_MU_AC_PARAM_ACI_AIFSN_IDX] |=
									set_he_cap(2, HE_MU_AC_PARAM_ACI);
	conf->he_mu_edca.he_mu_ac_vi_param[HE_MU_AC_PARAM_MU_EDCA_TIMER_IDX] = 13;
	conf->he_mu_edca.he_mu_ac_vo_param[HE_MU_AC_PARAM_ACI_AIFSN_IDX] |=
									set_he_cap(2, HE_MU_AC_PARAM_AIFSN);
	conf->he_mu_edca.he_mu_ac_vo_param[HE_MU_AC_PARAM_ACI_AIFSN_IDX] |=
									set_he_cap(3, HE_MU_AC_PARAM_ACI);
	conf->he_mu_edca.he_mu_ac_vo_param[HE_MU_AC_PARAM_ECWMIN_ECWMAX_IDX] |=
								set_he_cap(15, HE_MU_AC_PARAM_ECWMIN);
	conf->he_mu_edca.he_mu_ac_vo_param[HE_MU_AC_PARAM_ECWMIN_ECWMAX_IDX] |=
								set_he_cap(15, HE_MU_AC_PARAM_ECWMAX);
	conf->he_mu_edca.he_mu_ac_vo_param[HE_MU_AC_PARAM_MU_EDCA_TIMER_IDX] = 13;

	/* Non advertised capabilities */
	conf->he_non_advertised_caps.he_mac_capab_info[HE_MACCAP_CAP0_IDX] |=
											HE_MAC_CAP0_HTC_HE_SUPPORT;
	conf->he_non_advertised_caps.he_mac_capab_info[HE_MACCAP_CAP1_IDX] |=
						set_he_cap(3, HE_MAC_CAP1_MINIMUM_FRAGMENT_SIZE);
	conf->he_non_advertised_caps.he_mac_capab_info[HE_MACCAP_CAP1_IDX] |=
					set_he_cap(2, HE_MAC_CAP1_TRIGGER_FRAME_MAC_PAD_DUR);
	conf->he_non_advertised_caps.he_mac_capab_info[HE_MACCAP_CAP3_IDX] |=
						set_he_cap(3, HE_MAC_CAP3_MAX_AMPDU_LEN_EXP_EXT);
	conf->he_non_advertised_caps.he_mac_capab_info[HE_MACCAP_CAP5_IDX] |=
						set_he_cap(1, HE_MAC_CAP5_UL_2X996TONE_RU_SUPPORT);
	conf->he_non_advertised_caps.he_phy_capab_info[HE_PHYCAP_CAP1_IDX] |=
												HE_PHY_CAP1_DEVICE_CLASS;
	conf->he_non_advertised_caps.he_phy_capab_info[HE_PHYCAP_CAP2_IDX] |=
								 HE_PHY_CAP2_NDP_4X_HE_LTF_AND_3_2MS_GI;
	conf->he_non_advertised_caps.he_phy_capab_info[HE_PHYCAP_CAP5_IDX] |=
									HE_PHY_CAP5_NG_16_FOR_SU_FB_SUPPORT;
	conf->he_non_advertised_caps.he_phy_capab_info[HE_PHYCAP_CAP5_IDX] |=
									HE_PHY_CAP5_NG_16_FOR_MU_FB_SUPPORT;
	conf->he_non_advertised_caps.he_phy_capab_info[HE_PHYCAP_CAP6_IDX] |=
							HE_PHY_CAP6_CODEBOOK_SIZE42_FOR_SU_SUPPORT;
	conf->he_non_advertised_caps.he_phy_capab_info[HE_PHYCAP_CAP6_IDX] |=
							HE_PHY_CAP6_CODEBOOK_SIZE75_FOR_MU_SUPPORT;
	conf->he_non_advertised_caps.he_phy_capab_info[HE_PHYCAP_CAP7_IDX] |=
					HE_PHY_CAP7_SU_PPDU_AND_HE_MU_WITH_4X_HE_LTF_0_8US_GI;
	conf->he_non_advertised_caps.he_phy_capab_info[HE_PHYCAP_CAP7_IDX] |=
									set_he_cap(3, HE_PHY_CAP7_MAX_NC);
	conf->he_non_advertised_caps.he_phy_capab_info[HE_PHYCAP_CAP8_IDX] |=
									HE_PHY_CAP8_80MHZ_IN_160MHZ_HE_PPDU;

	conf->he_spatial_reuse_ie_present_in_beacon = 1;
	conf->he_spatial_reuse_ie_present_in_probe_response = 1;
	conf->he_spatial_reuse_ie_present_in_assoc_response = 1;

	conf->he_spatial_reuse.he_sr_control |=
						set_he_cap(0, HE_SRP_NON_SRG_OFFSET_PRESENT);
	conf->he_mu_edca_ie_present = 1;

	conf->he_spatial_reuse.he_sr_control |=
								set_he_cap(0, HE_SRP_SRG_INFO_PRESENT);
	conf->he_spatial_reuse.he_non_srg_obss_pd_max_offset = 0xaa;

	bss->twt_responder_support = 1;
	conf->he_nfr_buffer_threshold = 0xff;

	conf->mbssid_aid_offset = NON_MULTIBSS_AID_OFFSET;

	conf->he_oper.bss_color_info = 4;

	return conf;
}


int hostapd_mac_comp(const void *a, const void *b)
{
	return os_memcmp(a, b, sizeof(macaddr));
}


static int hostapd_config_read_wpa_psk(const char *fname,
				       struct hostapd_ssid *ssid)
{
	FILE *f;
	char buf[128], *pos;
	const char *keyid;
	char *context;
	char *context2;
	char *token;
	char *name;
	char *value;
	int line = 0, ret = 0, len, ok;
	u8 addr[ETH_ALEN];
	struct hostapd_wpa_psk *psk;

	if (!fname)
		return 0;

	f = fopen(fname, "r");
	if (!f) {
		wpa_printf(MSG_ERROR, "WPA PSK file '%s' not found.", fname);
		return -1;
	}

	while (fgets(buf, sizeof(buf), f)) {
		int vlan_id = 0;

		line++;

		if (buf[0] == '#')
			continue;
		pos = buf;
		while (*pos != '\0') {
			if (*pos == '\n') {
				*pos = '\0';
				break;
			}
			pos++;
		}
		if (buf[0] == '\0')
			continue;

		context = NULL;
		keyid = NULL;
		while ((token = str_token(buf, " ", &context))) {
			if (!os_strchr(token, '='))
				break;
			context2 = NULL;
			name = str_token(token, "=", &context2);
			if (!name)
				break;
			value = str_token(token, "", &context2);
			if (!value)
				value = "";
			if (!os_strcmp(name, "keyid")) {
				keyid = value;
			} else if (!os_strcmp(name, "vlanid")) {
				vlan_id = atoi(value);
			} else {
				wpa_printf(MSG_ERROR,
					   "Unrecognized '%s=%s' on line %d in '%s'",
					   name, value, line, fname);
				ret = -1;
				break;
			}
		}

		if (ret == -1)
			break;

		if (!token)
			token = "";
		if (hwaddr_aton(token, addr)) {
			wpa_printf(MSG_ERROR, "Invalid MAC address '%s' on "
				   "line %d in '%s'", token, line, fname);
			ret = -1;
			break;
		}

		psk = os_zalloc(sizeof(*psk));
		if (psk == NULL) {
			wpa_printf(MSG_ERROR, "WPA PSK allocation failed");
			ret = -1;
			break;
		}
		psk->vlan_id = vlan_id;
		if (is_zero_ether_addr(addr))
			psk->group = 1;
		else
			os_memcpy(psk->addr, addr, ETH_ALEN);

		pos = str_token(buf, "", &context);
		if (!pos) {
			wpa_printf(MSG_ERROR, "No PSK on line %d in '%s'",
				   line, fname);
			os_free(psk);
			ret = -1;
			break;
		}

		ok = 0;
		len = os_strlen(pos);
		if (len == 64 && hexstr2bin(pos, psk->psk, PMK_LEN) == 0)
			ok = 1;
		else if (len >= 8 && len < 64) {
			pbkdf2_sha1(pos, ssid->ssid, ssid->ssid_len,
				    4096, psk->psk, PMK_LEN);
			ok = 1;
		}
		if (!ok) {
			wpa_printf(MSG_ERROR, "Invalid PSK '%s' on line %d in "
				   "'%s'", pos, line, fname);
			os_free(psk);
			ret = -1;
			break;
		}

		if (keyid) {
			len = os_strlcpy(psk->keyid, keyid, sizeof(psk->keyid));
			if ((size_t) len >= sizeof(psk->keyid)) {
				wpa_printf(MSG_ERROR,
					   "PSK keyid too long on line %d in '%s'",
					   line, fname);
				os_free(psk);
				ret = -1;
				break;
			}
		}

		psk->next = ssid->wpa_psk;
		ssid->wpa_psk = psk;
	}

	fclose(f);

	return ret;
}


static int hostapd_derive_psk(struct hostapd_ssid *ssid)
{
	ssid->wpa_psk = os_zalloc(sizeof(struct hostapd_wpa_psk));
	if (ssid->wpa_psk == NULL) {
		wpa_printf(MSG_ERROR, "Unable to alloc space for PSK");
		return -1;
	}
	wpa_hexdump_ascii(MSG_DEBUG, "SSID",
			  (u8 *) ssid->ssid, ssid->ssid_len);
	wpa_hexdump_ascii_key(MSG_DEBUG, "PSK (ASCII passphrase)",
			      (u8 *) ssid->wpa_passphrase,
			      os_strlen(ssid->wpa_passphrase));
	pbkdf2_sha1(ssid->wpa_passphrase,
		    ssid->ssid, ssid->ssid_len,
		    4096, ssid->wpa_psk->psk, PMK_LEN);
	wpa_hexdump_key(MSG_DEBUG, "PSK (from passphrase)",
			ssid->wpa_psk->psk, PMK_LEN);
	return 0;
}


int hostapd_setup_wpa_psk(struct hostapd_bss_config *conf)
{
	struct hostapd_ssid *ssid = &conf->ssid;

	if (ssid->wpa_passphrase != NULL) {
		if (ssid->wpa_psk != NULL) {
			wpa_printf(MSG_DEBUG, "Using pre-configured WPA PSK "
				   "instead of passphrase");
		} else {
			wpa_printf(MSG_DEBUG, "Deriving WPA PSK based on "
				   "passphrase");
			if (hostapd_derive_psk(ssid) < 0)
				return -1;
		}
		ssid->wpa_psk->group = 1;
	}

	return hostapd_config_read_wpa_psk(ssid->wpa_psk_file, &conf->ssid);
}


static void hostapd_config_free_radius(struct hostapd_radius_server *servers,
				       int num_servers)
{
	int i;

	for (i = 0; i < num_servers; i++) {
		os_free(servers[i].shared_secret);
	}
	os_free(servers);
}


struct hostapd_radius_attr *
hostapd_config_get_radius_attr(struct hostapd_radius_attr *attr, u8 type)
{
	for (; attr; attr = attr->next) {
		if (attr->type == type)
			return attr;
	}
	return NULL;
}


static void hostapd_config_free_radius_attr(struct hostapd_radius_attr *attr)
{
	struct hostapd_radius_attr *prev;

	while (attr) {
		prev = attr;
		attr = attr->next;
		wpabuf_free(prev->val);
		os_free(prev);
	}
}


void hostapd_config_free_eap_user(struct hostapd_eap_user *user)
{
	hostapd_config_free_radius_attr(user->accept_attr);
	os_free(user->identity);
	bin_clear_free(user->password, user->password_len);
	bin_clear_free(user->salt, user->salt_len);
	os_free(user);
}


void hostapd_config_free_eap_users(struct hostapd_eap_user *user)
{
	struct hostapd_eap_user *prev_user;

	while (user) {
		prev_user = user;
		user = user->next;
		hostapd_config_free_eap_user(prev_user);
	}
}


static void hostapd_config_free_wep(struct hostapd_wep_keys *keys)
{
	int i;
	for (i = 0; i < NUM_WEP_KEYS; i++) {
		bin_clear_free(keys->key[i], keys->len[i]);
		keys->key[i] = NULL;
	}
}


void hostapd_config_clear_wpa_psk(struct hostapd_wpa_psk **l)
{
	struct hostapd_wpa_psk *psk, *tmp;

	for (psk = *l; psk;) {
		tmp = psk;
		psk = psk->next;
		bin_clear_free(tmp, sizeof(*tmp));
	}
	*l = NULL;
}


static void hostapd_config_free_anqp_elem(struct hostapd_bss_config *conf)
{
	struct anqp_element *elem;

	while ((elem = dl_list_first(&conf->anqp_elem, struct anqp_element,
				     list))) {
		dl_list_del(&elem->list);
		wpabuf_free(elem->payload);
		os_free(elem);
	}
}


static void hostapd_config_free_fils_realms(struct hostapd_bss_config *conf)
{
#ifdef CONFIG_FILS
	struct fils_realm *realm;

	while ((realm = dl_list_first(&conf->fils_realms, struct fils_realm,
				      list))) {
		dl_list_del(&realm->list);
		os_free(realm);
	}
#endif /* CONFIG_FILS */
}


static void hostapd_config_free_sae_passwords(struct hostapd_bss_config *conf)
{
	struct sae_password_entry *pw, *tmp;

	pw = conf->sae_passwords;
	conf->sae_passwords = NULL;
	while (pw) {
		tmp = pw;
		pw = pw->next;
		str_clear_free(tmp->password);
		os_free(tmp->identifier);
		os_free(tmp);
	}
}


void hostapd_config_free_bss(struct hostapd_bss_config *conf)
{
	if (conf == NULL)
		return;

	hostapd_config_clear_wpa_psk(&conf->ssid.wpa_psk);

	str_clear_free(conf->ssid.wpa_passphrase);
	os_free(conf->ssid.wpa_psk_file);
	hostapd_config_free_wep(&conf->ssid.wep);
#ifdef CONFIG_FULL_DYNAMIC_VLAN
	os_free(conf->ssid.vlan_tagged_interface);
#endif /* CONFIG_FULL_DYNAMIC_VLAN */

	hostapd_config_free_eap_users(conf->eap_user);
	os_free(conf->eap_user_sqlite);

	os_free(conf->eap_req_id_text);
	os_free(conf->erp_domain);
	os_free(conf->accept_mac);
	os_free(conf->deny_mac);
	os_free(conf->nas_identifier);
	if (conf->radius) {
		hostapd_config_free_radius(conf->radius->auth_servers,
					   conf->radius->num_auth_servers);
		hostapd_config_free_radius(conf->radius->acct_servers,
					   conf->radius->num_acct_servers);
	}
	hostapd_config_free_radius_attr(conf->radius_auth_req_attr);
	hostapd_config_free_radius_attr(conf->radius_acct_req_attr);
	os_free(conf->rsn_preauth_interfaces);
	os_free(conf->ctrl_interface);
	os_free(conf->ca_cert);
	os_free(conf->server_cert);
	os_free(conf->private_key);
	os_free(conf->private_key_passwd);
	os_free(conf->check_cert_subject);
	os_free(conf->ocsp_stapling_response);
	os_free(conf->ocsp_stapling_response_multi);
	os_free(conf->dh_file);
	os_free(conf->openssl_ciphers);
	os_free(conf->openssl_ecdh_curves);
	os_free(conf->pac_opaque_encr_key);
	os_free(conf->eap_fast_a_id);
	os_free(conf->eap_fast_a_id_info);
	os_free(conf->eap_sim_db);
	os_free(conf->radius_server_clients);
	os_free(conf->radius);
	os_free(conf->radius_das_shared_secret);
	hostapd_config_free_vlan(conf);
	os_free(conf->time_zone);

#ifdef CONFIG_IEEE80211R_AP
	{
		struct ft_remote_r0kh *r0kh, *r0kh_prev;
		struct ft_remote_r1kh *r1kh, *r1kh_prev;

		r0kh = conf->r0kh_list;
		conf->r0kh_list = NULL;
		while (r0kh) {
			r0kh_prev = r0kh;
			r0kh = r0kh->next;
			os_free(r0kh_prev);
		}

		r1kh = conf->r1kh_list;
		conf->r1kh_list = NULL;
		while (r1kh) {
			r1kh_prev = r1kh;
			r1kh = r1kh->next;
			os_free(r1kh_prev);
		}
	}
#endif /* CONFIG_IEEE80211R_AP */

#ifdef CONFIG_WPS
	os_free(conf->wps_pin_requests);
	os_free(conf->device_name);
	os_free(conf->manufacturer);
	os_free(conf->model_name);
	os_free(conf->model_number);
	os_free(conf->serial_number);
	os_free(conf->config_methods);
	os_free(conf->ap_pin);
	os_free(conf->extra_cred);
	os_free(conf->ap_settings);
	hostapd_config_clear_wpa_psk(&conf->multi_ap_backhaul_ssid.wpa_psk);
	str_clear_free(conf->multi_ap_backhaul_ssid.wpa_passphrase);
	os_free(conf->upnp_iface);
	os_free(conf->friendly_name);
	os_free(conf->manufacturer_url);
	os_free(conf->model_description);
	os_free(conf->model_url);
	os_free(conf->upc);
	{
		unsigned int i;

		for (i = 0; i < MAX_WPS_VENDOR_EXTENSIONS; i++)
			wpabuf_free(conf->wps_vendor_ext[i]);
	}
	wpabuf_free(conf->wps_nfc_dh_pubkey);
	wpabuf_free(conf->wps_nfc_dh_privkey);
	wpabuf_free(conf->wps_nfc_dev_pw);
#endif /* CONFIG_WPS */

	os_free(conf->roaming_consortium);
	os_free(conf->venue_name);
	os_free(conf->venue_url);
	os_free(conf->nai_realm_data);
	os_free(conf->network_auth_type);
	os_free(conf->anqp_3gpp_cell_net);
	os_free(conf->domain_name);
	hostapd_config_free_anqp_elem(conf);

#ifdef CONFIG_RADIUS_TEST
	os_free(conf->dump_msk_file);
#endif /* CONFIG_RADIUS_TEST */

#ifdef CONFIG_HS20
	os_free(conf->hs20_oper_friendly_name);
	os_free(conf->hs20_wan_metrics);
	os_free(conf->hs20_connection_capability);
	os_free(conf->hs20_operating_class);
	os_free(conf->hs20_icons);
	if (conf->hs20_osu_providers) {
		size_t i;
		for (i = 0; i < conf->hs20_osu_providers_count; i++) {
			struct hs20_osu_provider *p;
			size_t j;
			p = &conf->hs20_osu_providers[i];
			os_free(p->friendly_name);
			os_free(p->server_uri);
			os_free(p->method_list);
			for (j = 0; j < p->icons_count; j++)
				os_free(p->icons[j]);
			os_free(p->icons);
			os_free(p->osu_nai);
			os_free(p->osu_nai2);
			os_free(p->service_desc);
		}
		os_free(conf->hs20_osu_providers);
	}
	if (conf->hs20_operator_icon) {
		size_t i;

		for (i = 0; i < conf->hs20_operator_icon_count; i++)
			os_free(conf->hs20_operator_icon[i]);
		os_free(conf->hs20_operator_icon);
	}
	os_free(conf->subscr_remediation_url);
	os_free(conf->hs20_sim_provisioning_url);
	os_free(conf->t_c_filename);
	os_free(conf->t_c_server_url);
#endif /* CONFIG_HS20 */

	wpabuf_free(conf->vendor_elements);
	wpabuf_free(conf->authresp_elements);
	wpabuf_free(conf->assocresp_elements);

	os_free(conf->sae_groups);
#ifdef CONFIG_OWE
	os_free(conf->owe_groups);
#endif /* CONFIG_OWE */

	os_free(conf->wowlan_triggers);

	os_free(conf->server_id);

#ifdef CONFIG_TESTING_OPTIONS
	wpabuf_free(conf->own_ie_override);
	wpabuf_free(conf->sae_commit_override);
#endif /* CONFIG_TESTING_OPTIONS */

	os_free(conf->no_probe_resp_if_seen_on);
	os_free(conf->no_auth_if_seen_on);

	hostapd_config_free_fils_realms(conf);

#ifdef CONFIG_DPP
	os_free(conf->dpp_connector);
	wpabuf_free(conf->dpp_netaccesskey);
	wpabuf_free(conf->dpp_csign);
#endif /* CONFIG_DPP */

	hostapd_config_free_sae_passwords(conf);

#ifdef CONFIG_WDS_WPA
  os_free(conf->wds_wpa_sta);
#endif

	os_free(conf->sAggrConfig);

	os_free(conf);
}


/**
 * hostapd_config_free - Free hostapd configuration
 * @conf: Configuration data from hostapd_config_read().
 */
void hostapd_config_free(struct hostapd_config *conf)
{
	size_t i;

	if (conf == NULL)
		return;

	for (i = 0; i < conf->num_bss; i++)
		hostapd_config_free_bss(conf->bss[i]);
	os_free(conf->bss);
	os_free(conf->supported_rates);
	os_free(conf->basic_rates);
	os_free(conf->acs_ch_list.range);
	os_free(conf->driver_params);
#ifdef CONFIG_ACS
	os_free(conf->acs_chan_bias);
	os_free(conf->acs_numbss_info_file);
	os_free(conf->acs_numbss_coeflist);

	/* SmartACS */
	os_free(conf->acs_smart_info_file);
	os_free(conf->acs_history_file);
	os_free(conf->acs_penalty_factors);
	os_free(conf->acs_to_degradation);
	os_free(conf->acs_grp_priorities_throughput);
	os_free(conf->acs_grp_priorities_reach);
	os_free(conf->acs_bw_threshold);
#endif /* CONFIG_ACS */
	wpabuf_free(conf->lci);
	wpabuf_free(conf->civic);

	hostapd_atf_clean_config(&conf->atf_cfg);
	os_free(conf->atf_config_file);

	os_free(conf->sCoCPower);
	os_free(conf->sRTSmode);
	os_free(conf->sFixedRateCfg);
	os_free(conf->sInterferDetThresh);
	os_free(conf->sCcaAdapt);
	os_free(conf->sFWRecovery);
	conf->sFWRecoverySize = 0;

	os_free(conf);
}


/**
 * hostapd_maclist_found - Find a MAC address from a list
 * @list: MAC address list
 * @num_entries: Number of addresses in the list
 * @addr: Address to search for
 * @vlan_id: Buffer for returning VLAN ID or %NULL if not needed
 * Returns: 1 if address is in the list or 0 if not.
 *
 * Perform a binary search for given MAC address from a pre-sorted list.
 */
int hostapd_maclist_found(struct mac_acl_entry *list, int num_entries,
			  const u8 *addr, struct vlan_description *vlan_id)
{
	int start, end, middle, res;

	start = 0;
	end = num_entries - 1;

	while (start <= end) {
		middle = (start + end) / 2;
		res = os_memcmp(list[middle].addr, addr, ETH_ALEN);
		if (res == 0) {
			if (vlan_id)
				*vlan_id = list[middle].vlan_id;
			return 1;
		}
		if (res < 0)
			start = middle + 1;
		else
			end = middle - 1;
	}

	return 0;
}


int hostapd_rate_found(int *list, int rate)
{
	int i;

	if (list == NULL)
		return 0;

	for (i = 0; list[i] >= 0; i++)
		if (list[i] == rate)
			return 1;

	return 0;
}


int hostapd_vlan_valid(struct hostapd_vlan *vlan,
		       struct vlan_description *vlan_desc)
{
	struct hostapd_vlan *v = vlan;
	int i;

	if (!vlan_desc->notempty || vlan_desc->untagged < 0 ||
	    vlan_desc->untagged > MAX_VLAN_ID)
		return 0;
	for (i = 0; i < MAX_NUM_TAGGED_VLAN; i++) {
		if (vlan_desc->tagged[i] < 0 ||
		    vlan_desc->tagged[i] > MAX_VLAN_ID)
			return 0;
	}
	if (!vlan_desc->untagged && !vlan_desc->tagged[0])
		return 0;

	while (v) {
		if (!vlan_compare(&v->vlan_desc, vlan_desc) ||
		    v->vlan_id == VLAN_ID_WILDCARD)
			return 1;
		v = v->next;
	}
	return 0;
}


const char * hostapd_get_vlan_id_ifname(struct hostapd_vlan *vlan, int vlan_id)
{
	struct hostapd_vlan *v = vlan;
	while (v) {
		if (v->vlan_id == vlan_id)
			return v->ifname;
		v = v->next;
	}
	return NULL;
}


const u8 * hostapd_get_psk(const struct hostapd_bss_config *conf,
			   const u8 *addr, const u8 *p2p_dev_addr,
			   const u8 *prev_psk, int *vlan_id)
{
	struct hostapd_wpa_psk *psk;
	int next_ok = prev_psk == NULL;

	if (vlan_id)
		*vlan_id = 0;

	if (p2p_dev_addr && !is_zero_ether_addr(p2p_dev_addr)) {
		wpa_printf(MSG_DEBUG, "Searching a PSK for " MACSTR
			   " p2p_dev_addr=" MACSTR " prev_psk=%p",
			   MAC2STR(addr), MAC2STR(p2p_dev_addr), prev_psk);
		addr = NULL; /* Use P2P Device Address for matching */
	} else {
		wpa_printf(MSG_DEBUG, "Searching a PSK for " MACSTR
			   " prev_psk=%p",
			   MAC2STR(addr), prev_psk);
	}

	for (psk = conf->ssid.wpa_psk; psk != NULL; psk = psk->next) {
		if (next_ok &&
		    (psk->group ||
		     (addr && os_memcmp(psk->addr, addr, ETH_ALEN) == 0) ||
		     (!addr && p2p_dev_addr &&
		      os_memcmp(psk->p2p_dev_addr, p2p_dev_addr, ETH_ALEN) ==
		      0))) {
			if (vlan_id)
				*vlan_id = psk->vlan_id;
			return psk->psk;
		}

		if (psk->psk == prev_psk)
			next_ok = 1;
	}

	return NULL;
}


static int hostapd_config_check_bss(struct hostapd_bss_config *bss,
				    struct hostapd_config *conf,
				    int full_config)
{
	if (full_config && bss->ieee802_1x && !bss->eap_server &&
	    !bss->radius->auth_servers) {
		wpa_printf(MSG_ERROR, "Invalid IEEE 802.1X configuration (no "
			   "EAP authenticator configured).");
		return -1;
	}

	if (bss->wpa) {
		int wep, i;

		wep = bss->default_wep_key_len > 0 ||
		       bss->individual_wep_key_len > 0;
		for (i = 0; i < NUM_WEP_KEYS; i++) {
			if (bss->ssid.wep.keys_set) {
				wep = 1;
				break;
			}
		}

		if (wep) {
			wpa_printf(MSG_ERROR, "WEP configuration in a WPA network is not supported");
			return -1;
		}
	}

	if (full_config && bss->wpa &&
	    bss->wpa_psk_radius != PSK_RADIUS_IGNORED &&
	    bss->macaddr_acl != USE_EXTERNAL_RADIUS_AUTH) {
		wpa_printf(MSG_ERROR, "WPA-PSK using RADIUS enabled, but no "
			   "RADIUS checking (macaddr_acl=2) enabled.");
		return -1;
	}

	if (full_config && bss->wpa && (bss->wpa_key_mgmt & WPA_KEY_MGMT_PSK) &&
	    bss->ssid.wpa_psk == NULL && bss->ssid.wpa_passphrase == NULL &&
	    bss->ssid.wpa_psk_file == NULL &&
	    (bss->wpa_psk_radius != PSK_RADIUS_REQUIRED ||
	     bss->macaddr_acl != USE_EXTERNAL_RADIUS_AUTH)) {
		wpa_printf(MSG_ERROR, "WPA-PSK enabled, but PSK or passphrase "
			   "is not configured.");
		return -1;
	}

	if (full_config && !is_zero_ether_addr(bss->bssid)) {
		size_t i;

		for (i = 0; i < conf->num_bss; i++) {
			if (conf->bss[i] != bss &&
			    (hostapd_mac_comp(conf->bss[i]->bssid,
					      bss->bssid) == 0)) {
				wpa_printf(MSG_ERROR, "Duplicate BSSID " MACSTR
					   " on interface '%s' and '%s'.",
					   MAC2STR(bss->bssid),
					   conf->bss[i]->iface, bss->iface);
				return -1;
			}
		}
	}

#ifdef CONFIG_IEEE80211R_AP
	if (full_config && wpa_key_mgmt_ft(bss->wpa_key_mgmt) &&
	    (bss->nas_identifier == NULL ||
	     os_strlen(bss->nas_identifier) < 1 ||
	     os_strlen(bss->nas_identifier) > FT_R0KH_ID_MAX_LEN)) {
		wpa_printf(MSG_ERROR, "FT (IEEE 802.11r) requires "
			   "nas_identifier to be configured as a 1..48 octet "
			   "string");
		return -1;
	}
#endif /* CONFIG_IEEE80211R_AP */

#ifdef CONFIG_IEEE80211N
	if (full_config && conf->ieee80211n &&
	    conf->hw_mode == HOSTAPD_MODE_IEEE80211B && !conf->testbed_mode) {
		bss->disable_11n = 1;
		bss->disable_11ac = 1;
		bss->disable_11ax = 1;
		wpa_printf(MSG_ERROR, "HT (IEEE 802.11n) in 11b mode is not "
			   "allowed, disabling HT capabilities");
	}

	if (full_config && conf->ieee80211n &&
	    bss->ssid.security_policy == SECURITY_STATIC_WEP && !conf->testbed_mode) {
		bss->disable_11n = 1;
		bss->disable_11ac = 1;
		bss->disable_11ax = 1;
		wpa_printf(MSG_ERROR, "HT (IEEE 802.11n) with WEP is not "
			   "allowed, disabling HT capabilities");
	}

	if (full_config && conf->ieee80211n && bss->wpa &&
	    !(bss->wpa_pairwise & WPA_CIPHER_CCMP) &&
	    !(bss->rsn_pairwise & (WPA_CIPHER_CCMP | WPA_CIPHER_GCMP |
				   WPA_CIPHER_CCMP_256 | WPA_CIPHER_GCMP_256)) && !conf->testbed_mode)
	{
		bss->disable_11n = 1;
		bss->disable_11ac = 1;
		bss->disable_11ax = 1;
		wpa_printf(MSG_ERROR, "HT (IEEE 802.11n) with WPA/WPA2 "
			   "requires CCMP/GCMP to be enabled, disabling HT "
			   "capabilities");
	}
#endif /* CONFIG_IEEE80211N */

	if (full_config && bss->vendor_vht && !conf->ieee80211n && !conf->testbed_mode) {
		bss->disable_11n = 1;
		bss->disable_11ac = 1;
		bss->disable_11ax = 1;
		wpa_printf(MSG_ERROR, "VHT (IEEE 802.11ac) in 11b/11g mode is not allowed, "
				"disabling VHT capabilities");
	}

#ifdef CONFIG_IEEE80211AC
	if (full_config && conf->ieee80211ac &&
	    bss->ssid.security_policy == SECURITY_STATIC_WEP && !conf->testbed_mode) {
		bss->disable_11ac = 1;
		bss->disable_11ax = 1;
		wpa_printf(MSG_ERROR,
			   "VHT (IEEE 802.11ac) with WEP is not allowed, disabling VHT capabilities");
	}

	if (full_config && conf->ieee80211ac && bss->wpa &&
	    !(bss->wpa_pairwise & WPA_CIPHER_CCMP) &&
	    !(bss->rsn_pairwise & (WPA_CIPHER_CCMP | WPA_CIPHER_GCMP |
				   WPA_CIPHER_CCMP_256 | WPA_CIPHER_GCMP_256)))
	{
		bss->disable_11ac = 1;
		wpa_printf(MSG_ERROR,
			   "VHT (IEEE 802.11ac) with WPA/WPA2 requires CCMP/GCMP to be enabled, disabling VHT capabilities");
	}
#endif /* CONFIG_IEEE80211AC */
#ifdef CONFIG_IEEE80211AX
    if (full_config && conf->ieee80211ax && !conf->testbed_mode &&
        bss->ssid.security_policy == SECURITY_STATIC_WEP) {
        bss->disable_11ax = 1;
        wpa_printf(MSG_ERROR,
                "HE (IEEE 802.11ax) with WEP is not allowed, disabling HE capabilities");
        }
#endif /* CONFIG_IEEE80211AX */
#ifdef CONFIG_WPS
	if (full_config && bss->wps_state && bss->ignore_broadcast_ssid) {
		wpa_printf(MSG_INFO, "WPS: ignore_broadcast_ssid "
			   "configuration forced WPS to be disabled");
		bss->wps_state = 0;
	}

	if (full_config && bss->wps_state &&
	    bss->ssid.wep.keys_set && bss->wpa == 0) {
		wpa_printf(MSG_INFO, "WPS: WEP configuration forced WPS to be "
			   "disabled");
		bss->wps_state = 0;
	}

	if (full_config && bss->wps_state && bss->wpa &&
	    (!(bss->wpa & 2) ||
	     !(bss->rsn_pairwise & (WPA_CIPHER_CCMP | WPA_CIPHER_GCMP |
				    WPA_CIPHER_CCMP_256 |
				    WPA_CIPHER_GCMP_256)))) {
		wpa_printf(MSG_INFO, "WPS: WPA/TKIP configuration without "
			   "WPA2/CCMP/GCMP forced WPS to be disabled");
		bss->wps_state = 0;
	}
#endif /* CONFIG_WPS */

#ifdef CONFIG_HS20
	if (full_config && bss->hs20 &&
	    (!(bss->wpa & 2) ||
	     !(bss->rsn_pairwise & (WPA_CIPHER_CCMP | WPA_CIPHER_GCMP |
				    WPA_CIPHER_CCMP_256 |
				    WPA_CIPHER_GCMP_256)))) {
		wpa_printf(MSG_ERROR, "HS 2.0: WPA2-Enterprise/CCMP "
			   "configuration is required for Hotspot 2.0 "
			   "functionality");
		return -1;
	}
#endif /* CONFIG_HS20 */

#ifdef CONFIG_MBO
	if (!bss->mbo_pmf_bypass)
		if (full_config && bss->mbo_enabled && (bss->wpa & 2) &&
			bss->ieee80211w == NO_MGMT_FRAME_PROTECTION) {
			wpa_printf(MSG_ERROR,
				   "MBO: PMF needs to be enabled whenever using WPA2 with MBO");
			return -1;
		}

#endif /* CONFIG_MBO */

	if ((bss->mesh_mode == MESH_MODE_BACKHAUL_AP) && (bss->max_num_sta != 1)) {
		wpa_printf(MSG_ERROR, "Multi-AP: for backhaul AP max_num_sta should be set to 1");
		return -1;
	}
#ifdef CONFIG_OCV
	if (full_config && bss->ieee80211w == NO_MGMT_FRAME_PROTECTION &&
	    bss->ocv) {
		wpa_printf(MSG_ERROR,
			   "OCV: PMF needs to be enabled whenever using OCV");
		return -1;
	}
#endif /* CONFIG_OCV */

	return 0;
}


static int hostapd_config_check_cw(struct hostapd_config *conf, int queue)
{
	int tx_cwmin = conf->tx_queue[queue].cwmin;
	int tx_cwmax = conf->tx_queue[queue].cwmax;
	int ac_cwmin = conf->wmm_ac_params[queue].cwmin;
	int ac_cwmax = conf->wmm_ac_params[queue].cwmax;

	if (tx_cwmin > tx_cwmax) {
		wpa_printf(MSG_ERROR,
			   "Invalid TX queue cwMin/cwMax values. cwMin(%d) greater than cwMax(%d)",
			   tx_cwmin, tx_cwmax);
		return -1;
	}
	if (ac_cwmin > ac_cwmax) {
		wpa_printf(MSG_ERROR,
			   "Invalid WMM AC cwMin/cwMax values. cwMin(%d) greater than cwMax(%d)",
			   ac_cwmin, ac_cwmax);
		return -1;
	}
	return 0;
}


int hostapd_config_check(struct hostapd_config *conf, int full_config)
{
	int i;
	int ap_max_num_sta;

	if (full_config && conf->ieee80211d &&
	    (!conf->country[0] || !conf->country[1])) {
		wpa_printf(MSG_ERROR, "Cannot enable IEEE 802.11d without "
			   "setting the country_code");
		return -1;
	}

	if (full_config && conf->ieee80211h && !conf->ieee80211d) {
		wpa_printf(MSG_ERROR, "Cannot enable IEEE 802.11h without "
			   "IEEE 802.11d enabled");
		return -1;
	}

	if (full_config && conf->local_pwr_constraint != -1 &&
	    !conf->ieee80211d) {
		wpa_printf(MSG_ERROR, "Cannot add Power Constraint element without Country element");
		return -1;
	}

	if (full_config && conf->spectrum_mgmt_required &&
	    conf->local_pwr_constraint == -1) {
		wpa_printf(MSG_ERROR, "Cannot set Spectrum Management bit without Country and Power Constraint elements");
		return -1;
	}

	for (i = 0; i < NUM_TX_QUEUES; i++) {
		if (hostapd_config_check_cw(conf, i))
			return -1;
	}

	for (i = 0; i < conf->num_bss; i++) {
		if (hostapd_config_check_bss(conf->bss[i], conf, full_config))
			return -1;
	}

	ap_max_num_sta = conf->ap_max_num_sta;
	for (i = 0; i < conf->num_bss; i++) {
		if (conf->bss[i]->max_num_sta > conf->ap_max_num_sta) {
			wpa_printf(MSG_ERROR, "max_num_sta (%d) for BSS#%d is greater than "
				   "ap_max_num_sta (%d) for radio.", conf->bss[i]->max_num_sta, i, conf->ap_max_num_sta);
			return -1;
		}
		ap_max_num_sta -= conf->bss[i]->num_res_sta;
	}

	if (ap_max_num_sta < 0) {
		wpa_printf(MSG_ERROR, "Summ of num_res_sta for all BSS is greater than ap_max_num_sta (%d) ",
			   conf->ap_max_num_sta);
		return -1;
	}

	if(conf->multibss_enable) {
		conf->mbssid_aid_offset = MULTIBSS_AID_OFFSET;
		wpa_printf(MSG_DEBUG, "mbssid_aid_offset is set to (%d)", conf->mbssid_aid_offset);
	}

#ifdef CONFIG_IEEE80211AX
	if(conf->ieee80211ax &&
		(conf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP4_IDX] &
		HE_PHY_CAP4_SU_BEAMFORMEE)) {
		conf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP6_IDX] |=
			HE_PHY_CAP6_TRIGGERED_CQI_FEEDBACK;
		conf->vht_capab &= ~VHT_CAP_MU_BEAMFORMEE_CAPABLE;

		wpa_printf(MSG_DEBUG, "HE PHY Triggered CQI Feedback capability is enforced to (%d)",
					conf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP6_IDX] & HE_PHY_CAP6_TRIGGERED_CQI_FEEDBACK);

				wpa_printf(MSG_DEBUG, "VHT MU Beamformee capability is enforced to (%d)",
												(conf->vht_capab & VHT_CAP_MU_BEAMFORMEE_CAPABLE) ? 1 : 0);
	} else {
			conf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP4_IDX] |=
				set_he_cap(0, HE_PHY_CAP4_BF_STS_LESS_OR_EQ_80MHz);
			conf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP4_IDX] |=
				set_he_cap(0, HE_PHY_CAP4_BF_STS_GREATER_THAN_80MHz);
			conf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP5_IDX] |=
				set_he_cap(0, HE_PHY_CAP5_NUM_SOUND_DIM_LESS_80MHz);
			conf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP5_IDX] |=
				set_he_cap(0, HE_PHY_CAP5_NUM_SOUND_DIM_GREAT_80MHz);
	}

	conf->he_oper.vht_op_info_chwidth = conf->vht_oper_chwidth;
	conf->he_oper.vht_op_info_chan_center_freq_seg0_idx =
		conf->vht_oper_centr_freq_seg0_idx;
	conf->he_oper.vht_op_info_chan_center_freq_seg1_idx =
		conf->vht_oper_centr_freq_seg1_idx;
#endif /* CONFIG_IEEE80211AX */

	return 0;
}


void hostapd_set_security_params(struct hostapd_bss_config *bss,
				 int full_config)
{
	if (bss->individual_wep_key_len == 0) {
		/* individual keys are not use; can use key idx0 for
		 * broadcast keys */
		bss->broadcast_key_idx_min = 0;
	}

	if ((bss->wpa & 2) && bss->rsn_pairwise == 0)
		bss->rsn_pairwise = bss->wpa_pairwise;
	if (bss->group_cipher)
		bss->wpa_group = bss->group_cipher;
	else
		bss->wpa_group = wpa_select_ap_group_cipher(bss->wpa,
							    bss->wpa_pairwise,
							    bss->rsn_pairwise);
	if (!bss->wpa_group_rekey_set)
		bss->wpa_group_rekey = bss->wpa_group == WPA_CIPHER_TKIP ?
			600 : 86400;

	if (full_config) {
		bss->radius->auth_server = bss->radius->auth_servers;
		bss->radius->acct_server = bss->radius->acct_servers;
	}

	if (bss->wpa && bss->ieee802_1x) {
		bss->ssid.security_policy = SECURITY_WPA;
	} else if (bss->wpa) {
		bss->ssid.security_policy = SECURITY_WPA_PSK;
	} else if (bss->ieee802_1x) {
		int cipher = WPA_CIPHER_NONE;
		bss->ssid.security_policy = SECURITY_IEEE_802_1X;
		bss->ssid.wep.default_len = bss->default_wep_key_len;
		if (full_config && bss->default_wep_key_len) {
			cipher = bss->default_wep_key_len >= 13 ?
				WPA_CIPHER_WEP104 : WPA_CIPHER_WEP40;
		} else if (full_config && bss->ssid.wep.keys_set) {
			if (bss->ssid.wep.len[0] >= 13)
				cipher = WPA_CIPHER_WEP104;
			else
				cipher = WPA_CIPHER_WEP40;
		}
		bss->wpa_group = cipher;
		bss->wpa_pairwise = cipher;
		bss->rsn_pairwise = cipher;
		if (full_config)
			bss->wpa_key_mgmt = WPA_KEY_MGMT_IEEE8021X_NO_WPA;
	} else if (bss->ssid.wep.keys_set) {
		int cipher = WPA_CIPHER_WEP40;
		if (bss->ssid.wep.len[0] >= 13)
			cipher = WPA_CIPHER_WEP104;
		bss->ssid.security_policy = SECURITY_STATIC_WEP;
		bss->wpa_group = cipher;
		bss->wpa_pairwise = cipher;
		bss->rsn_pairwise = cipher;
		if (full_config)
			bss->wpa_key_mgmt = WPA_KEY_MGMT_NONE;
	} else if (bss->osen) {
		bss->ssid.security_policy = SECURITY_OSEN;
		bss->wpa_group = WPA_CIPHER_CCMP;
		bss->wpa_pairwise = 0;
		bss->rsn_pairwise = WPA_CIPHER_CCMP;
	} else {
		bss->ssid.security_policy = SECURITY_PLAINTEXT;
		if (full_config) {
			bss->wpa_group = WPA_CIPHER_NONE;
			bss->wpa_pairwise = WPA_CIPHER_NONE;
			bss->rsn_pairwise = WPA_CIPHER_NONE;
			bss->wpa_key_mgmt = WPA_KEY_MGMT_NONE;
		}
	}
}


int hostapd_sae_pw_id_in_use(struct hostapd_bss_config *conf)
{
	int with_id = 0, without_id = 0;
	struct sae_password_entry *pw;

	if (conf->ssid.wpa_passphrase)
		without_id = 1;

	for (pw = conf->sae_passwords; pw; pw = pw->next) {
		if (pw->identifier)
			with_id = 1;
		else
			without_id = 1;
		if (with_id && without_id)
			break;
	}

	if (with_id && !without_id)
		return 2;
	return with_id;
}
