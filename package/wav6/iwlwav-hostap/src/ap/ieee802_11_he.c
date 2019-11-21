/*
 * hostapd / IEEE 802.11ax HE
 * Copyright (c) 2016-2017, Qualcomm Atheros, Inc.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "utils/includes.h"

#include "utils/common.h"
#include "common/ieee802_11_defs.h"
#include "hostapd.h"
#include "ap_config.h"
#include "sta_info.h"
#include "beacon.h"
#include "ieee802_11.h"
#include "dfs.h"

static int get_he_ppe_size(const u8 *he_capab) {

	unsigned nof_sts, nof_ru, nbits, nbytes;
	const u8 *he_ppe_thresholds = he_capab;

	nof_sts = (he_ppe_thresholds[0] & HE_PPE_CAP0_NSS_M1) + 1;
	nof_ru = count_bits_set(he_ppe_thresholds[0] & HE_PPE_CAP0_RU_INDEX_BITMASK);
	nbits   = 3 /* nsts */ + 4 /* ru_index_bitmask */ + (6 * nof_sts * nof_ru);
	nbytes  = BITS_TO_BYTES(nbits);

	return nbytes;
}

u8 * hostapd_eid_he_capab(struct hostapd_data *hapd, u8 *eid)
{
	struct ieee80211_he_capabilities *cap;
	u8 *pos = eid;
	u8 *he_cap, *size_pos;
	u8 he_txrx_mcs_size;
	u8 size;
	u8 chanwidth;
	struct hostapd_hw_modes *mode = hapd->iface->current_mode;

	if (!hapd->iface->current_mode)
		return eid;

	/* Minimize HE Channel Width Set against supported by HW and configured values */
	chanwidth = mode->he_capab.he_phy_capab_info[HE_PHYCAP_CAP0_IDX] &
											hapd->iconf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP0_IDX];

	he_txrx_mcs_size = sizeof(cap->he_txrx_mcs_support)/3;

	if(chanwidth & HE_PHY_CAP0_CHANNEL_WIDTH_SET_B3)
		he_txrx_mcs_size += sizeof(cap->he_txrx_mcs_support)/3;

	if(chanwidth & HE_PHY_CAP0_CHANNEL_WIDTH_SET_B2)
		he_txrx_mcs_size += sizeof(cap->he_txrx_mcs_support)/3;

	size = sizeof(cap->he_mac_capab_info) +
			sizeof(cap->he_phy_capab_info) +
			he_txrx_mcs_size;

	*pos++ = WLAN_EID_EXTENSION;
	size_pos = pos;
	*pos++ = 0; /* Final size is not known yet */
	*pos++ = WLAN_EID_EXT_HE_CAPABILITIES;

	he_cap = pos;
	os_memset(he_cap, 0, sizeof(cap->he_mac_capab_info));
	/* HE MAC Capabilities Information field */
	os_memcpy(he_cap,
		&mode->he_capab.he_mac_capab_info,
		sizeof(cap->he_mac_capab_info));

	he_cap = (u8 *) (he_cap + sizeof(cap->he_mac_capab_info));
	/* HE PHY Capabilities Information field */
	os_memset(he_cap, 0, sizeof(cap->he_phy_capab_info));
	os_memcpy(he_cap,
		&mode->he_capab.he_phy_capab_info,
		sizeof(cap->he_phy_capab_info));


	he_cap[HE_PHYCAP_CAP0_IDX] = chanwidth;

	/* Update HE PHY MAX NC field with supported value by HW */
	he_cap[HE_PHYCAP_CAP7_IDX] &= ~(HE_PHY_CAP7_MAX_NC);
	he_cap[HE_PHYCAP_CAP7_IDX] |=
		(mode->he_capab.he_phy_capab_info[HE_PHYCAP_CAP7_IDX] & HE_PHY_CAP7_MAX_NC);

	he_cap = (u8 *) (he_cap + sizeof(cap->he_phy_capab_info));
	/* Supported HE-MCS And NSS Set field */
	os_memset(he_cap, 0, he_txrx_mcs_size);
	os_memcpy(he_cap,
		&mode->he_capab.he_txrx_mcs_support,
		he_txrx_mcs_size);

	if((hapd->iface->conf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP6_IDX] &
			HE_PHY_CAP6_PPE_THRESHOLD_PRESENT) &
			(mode->he_capab.he_phy_capab_info[HE_PHYCAP_CAP6_IDX] &
			HE_PHY_CAP6_PPE_THRESHOLD_PRESENT)) {
		he_cap = (u8 *) (he_cap + he_txrx_mcs_size);
		u8 ppe_th_size = get_he_ppe_size(mode->he_capab.he_ppe_thresholds);
		os_memset(he_cap, 0, ppe_th_size);
		os_memcpy(he_cap,
			&mode->he_capab.he_ppe_thresholds,
			ppe_th_size);
		size += ppe_th_size;
	}

	*size_pos = 1 + size;
	wpa_hexdump(MSG_DEBUG, "hostapd_eid_he_capab:", pos, size);
	pos += size;

	return pos;
}


u8 * hostapd_eid_he_operation(struct hostapd_data *hapd, u8 *eid)
{
	struct ieee80211_he_operation *oper;
	u8 *pos = eid;
	u8 *he_oper;

	if (!hapd->iface->current_mode)
		return eid;

	u8 size = sizeof(oper->he_oper_params) +
				sizeof(oper->bss_color_info) +
				sizeof(oper->he_mcs_nss_set);

	if(hapd->iface->conf->he_oper.he_oper_params[HE_OPERATION_CAP1_IDX] &
		HE_OPERATION_CAP1_VHT_OPER_INFO_PRESENT) {
		size += sizeof(oper->vht_op_info_chwidth) +
				sizeof(oper->vht_op_info_chan_center_freq_seg0_idx) +
				sizeof(oper->vht_op_info_chan_center_freq_seg1_idx);

		hapd->iface->conf->he_oper.vht_op_info_chwidth =
						hapd->iface->conf->vht_oper_chwidth;
		hapd->iface->conf->he_oper.vht_op_info_chan_center_freq_seg0_idx =
						hapd->iface->conf->vht_oper_centr_freq_seg0_idx;
		hapd->iface->conf->he_oper.vht_op_info_chan_center_freq_seg1_idx =
						hapd->iface->conf->vht_oper_centr_freq_seg1_idx;
	}

	if(hapd->iface->conf->he_oper.he_oper_params[HE_OPERATION_CAP1_IDX] &
		HE_OPERATION_CAP1_CO_LOCATED_BSS)
		size += sizeof(oper->max_co_located_bssid_ind);

	if(hapd->iface->conf->he_oper.he_oper_params[HE_OPERATION_CAP2_IDX] &
		HE_OPERATION_CAP2_6GHZ_OPERATION_INFO_PRESENT)
		size += sizeof(oper->he_6gh_operation_information);

	*pos++ = WLAN_EID_EXTENSION;
	*pos++ = 1 + size;
	*pos++ = WLAN_EID_EXT_HE_OPERATION;

	he_oper = pos;
	os_memset(he_oper, 0, size);
	os_memcpy(he_oper,
		&hapd->iface->conf->he_oper.he_oper_params,
		sizeof(oper->he_oper_params));

	he_oper = (u8 *) (he_oper + sizeof(oper->he_oper_params));
	*he_oper = hapd->iface->conf->he_oper.bss_color_info;

	he_oper = (u8 *) (he_oper + sizeof(oper->bss_color_info));
	os_memcpy(he_oper,
		&hapd->iface->conf->he_oper.he_mcs_nss_set,
		sizeof(oper->he_mcs_nss_set));

	he_oper = (u8 *) (he_oper + sizeof(oper->he_mcs_nss_set));
	if(hapd->iface->conf->he_oper.he_oper_params[HE_OPERATION_CAP1_IDX] &
		HE_OPERATION_CAP1_VHT_OPER_INFO_PRESENT) {
		*he_oper++ = hapd->iface->conf->he_oper.vht_op_info_chwidth;
		*he_oper++ =
			hapd->iface->conf->he_oper.vht_op_info_chan_center_freq_seg0_idx;
		*he_oper++ =
			hapd->iface->conf->he_oper.vht_op_info_chan_center_freq_seg1_idx;
	}

	if(hapd->iface->conf->he_oper.he_oper_params[HE_OPERATION_CAP1_IDX] &
		HE_OPERATION_CAP1_CO_LOCATED_BSS)
		*he_oper++ = hapd->iface->conf->he_oper.max_co_located_bssid_ind;

	if(hapd->iface->conf->he_oper.he_oper_params[HE_OPERATION_CAP2_IDX] &
		HE_OPERATION_CAP2_6GHZ_OPERATION_INFO_PRESENT)
		os_memcpy(he_oper,
		&hapd->iface->conf->he_oper.he_6gh_operation_information,
		sizeof(hapd->iface->conf->he_oper.he_6gh_operation_information));

	wpa_hexdump(MSG_DEBUG, "hostapd_eid_he_operation:", pos, size);

	pos += size;

	return pos;
}

u8 * hostapd_eid_he_mu_edca_parameter_set(struct hostapd_data *hapd, u8 *eid)
{
	struct ieee80211_he_mu_edca_parameter_set *edca;
	u8 *pos;
	size_t i;

	pos = (u8 *) &hapd->iface->conf->he_mu_edca;
	for (i = 0; i < sizeof(*edca); i++) {
		if (pos[i])
			break;
	}
	if (i == sizeof(*edca))
		return eid; /* no MU EDCA Parameters configured */

	pos = eid;
	*pos++ = WLAN_EID_EXTENSION;
	*pos++ = 1 + sizeof(*edca);
	*pos++ = WLAN_EID_EXT_HE_MU_EDCA_PARAMS;

	edca = (struct ieee80211_he_mu_edca_parameter_set *) pos;
	os_memcpy(edca, &hapd->iface->conf->he_mu_edca, sizeof(*edca));

	wpa_hexdump(MSG_DEBUG, "HE: MU EDCA Parameter Set element",
		    pos, sizeof(*edca));

	pos += sizeof(*edca);

	return pos;
}

u8 * hostapd_eid_he_spatial_reuse_parameter_set(struct hostapd_data *hapd, u8 *eid)
{
	struct ieee80211_he_spatial_reuse_parameter_set *spatial_reuse;
	u8 *pos = eid;
	u8 *he_spatial;

	if (!hapd->iface->current_mode)
		return eid;

	u8 size = sizeof(spatial_reuse->he_sr_control);

	if(hapd->iface->conf->he_spatial_reuse.he_sr_control & HE_SRP_NON_SRG_OFFSET_PRESENT)
		size += sizeof(spatial_reuse->he_non_srg_obss_pd_max_offset);

	if(hapd->iface->conf->he_spatial_reuse.he_sr_control & HE_SRP_SRG_INFO_PRESENT)
		size += sizeof(spatial_reuse->he_srg_obss_pd_min_offset) +
				sizeof(spatial_reuse->he_srg_obss_pd_max_offset) +
				sizeof(spatial_reuse->he_srg_bss_color_bitmap) +
				sizeof(spatial_reuse->he_srg_partial_bssid_bitmap);

	*pos++ = WLAN_EID_EXTENSION;
	*pos++ = 1 + size;
	*pos++ = WLAN_EID_EXT_HE_SPATIAL_REUSE_SET;

	he_spatial = pos;
	os_memset(he_spatial, 0, size);
	*he_spatial++ = hapd->iface->conf->he_spatial_reuse.he_sr_control;

	if(hapd->iface->conf->he_spatial_reuse.he_sr_control & HE_SRP_NON_SRG_OFFSET_PRESENT)
		*he_spatial++ = hapd->iface->conf->he_spatial_reuse.he_non_srg_obss_pd_max_offset;

	if(hapd->iface->conf->he_spatial_reuse.he_sr_control & HE_SRP_SRG_INFO_PRESENT) {
		*he_spatial++ = hapd->iface->conf->he_spatial_reuse.he_srg_obss_pd_min_offset;
		*he_spatial++ = hapd->iface->conf->he_spatial_reuse.he_srg_obss_pd_max_offset;
		os_memcpy(he_spatial,
		&hapd->iface->conf->he_spatial_reuse.he_srg_bss_color_bitmap,
		sizeof(spatial_reuse->he_srg_bss_color_bitmap));
		he_spatial += sizeof(spatial_reuse->he_srg_bss_color_bitmap);
		os_memcpy(he_spatial,
		&hapd->iface->conf->he_spatial_reuse.he_srg_partial_bssid_bitmap,
		sizeof(spatial_reuse->he_srg_partial_bssid_bitmap));
	}
	wpa_hexdump(MSG_DEBUG, "HE: Spatial Reuse Parameter Set:", pos, size);

	pos += size;

	return pos;

}

u8 * hostapd_eid_he_ndp_feedback_report_parameters_set(struct hostapd_data *hapd, u8 *eid)
{
	u8 *pos = eid;
	*pos++ = WLAN_EID_EXTENSION;
	*pos++ = sizeof(u8) * 2;
	*pos++ = WLAN_EID_EXT_HE_NDP_FEEDBACK_REPORT_PARAMETER_SET;
	*pos++ = hapd->iface->conf->he_nfr_buffer_threshold;
	wpa_printf(MSG_DEBUG, "hostapd_eid_he_ndp_feedback_report_parameters_set: 0x%02x",
																					 hapd->iface->conf->he_nfr_buffer_threshold);
	return pos;
}

/*
  Given bit pairs a0a1 and b0b1:
  If a0a1 == 3 or b0b1 == 3:
    c0c1 = 3
  else c0c1 = min(a0a1, b0b1)
  (c0c1 being the output)
 */
void hostapd_get_he_mcs_nss(const u8 *our_he_mcs_nss,
								const u8 *sta_he_mcs_nss, u8 *out_he_mcs_nss)
{
	u8 i = 0;
	while(i < 2) {
		u8 j, k, a, b, c;
		for(j = 0, k = 3; j < 4; j++, k = k<<2) {
			a = (our_he_mcs_nss[i] & k)>>(j*2);
			b = (sta_he_mcs_nss[i] & k)>>(j*2);
			c = MIN(a,b);
			if((a == 3) || (b == 3))
				out_he_mcs_nss[i] |= k;
			else
				out_he_mcs_nss[i] |= (k & c<<(j*2));
		}
		i++;
	}
}

static int get_he_mac_phy_size() {
	struct ieee80211_he_capabilities he_cap;
	return (sizeof(he_cap.he_mac_capab_info) + sizeof(he_cap.he_phy_capab_info));
}

static int get_he_mcs_nss_size(const u8 *he_capab) {
	const struct ieee80211_he_capabilities *sta_he_capab
		= (struct ieee80211_he_capabilities*)he_capab;
	u8 he_txrx_mcs_size;

	if(sta_he_capab->he_phy_capab_info[HE_PHYCAP_CAP0_IDX] &
		HE_PHY_CAP0_CHANNEL_WIDTH_SET_B3)
		he_txrx_mcs_size = sizeof(sta_he_capab->he_txrx_mcs_support);
	else if(sta_he_capab->he_phy_capab_info[HE_PHYCAP_CAP0_IDX] &
		HE_PHY_CAP0_CHANNEL_WIDTH_SET_B2)
		he_txrx_mcs_size = 2*(sizeof(sta_he_capab->he_txrx_mcs_support)/3);
	else
		he_txrx_mcs_size = sizeof(sta_he_capab->he_txrx_mcs_support)/3;

	return he_txrx_mcs_size;
}

static int get_he_ppe_th_size(const u8 *he_capab) {

	const struct ieee80211_he_capabilities *sta_he_capab
		 = (struct ieee80211_he_capabilities*)he_capab;

	if(sta_he_capab->he_phy_capab_info[HE_PHYCAP_CAP6_IDX] &
		HE_PHY_CAP6_PPE_THRESHOLD_PRESENT) {
		u8 *he_ppe_thresholds =
			(u8*)(he_capab + get_he_mac_phy_size() + get_he_mcs_nss_size(he_capab));

		return get_he_ppe_size(he_ppe_thresholds);
	}
	return 0;
}

static int check_valid_he_ie_length(const u8 *he_capab, u8 he_capab_len)
{
	struct ieee80211_he_capabilities *sta_he_capab =
		(struct ieee80211_he_capabilities*)he_capab;

	u8 min_he_capab_len = sizeof(sta_he_capab->he_mac_capab_info) +
							sizeof(sta_he_capab->he_phy_capab_info) +
							sizeof(sta_he_capab->he_txrx_mcs_support)/3;

	wpa_printf(MSG_DEBUG, "IE HE Capabilities minimum length for 80MHz is (%d)", min_he_capab_len);

	if(sta_he_capab->he_phy_capab_info[HE_PHYCAP_CAP0_IDX] &
		HE_PHY_CAP0_CHANNEL_WIDTH_SET_B2) {
			min_he_capab_len += sizeof(sta_he_capab->he_txrx_mcs_support)/3;
		wpa_printf(MSG_DEBUG, "IE HE Capabilities minimum length for 160MHz is (%d)", min_he_capab_len);
	}

	if(sta_he_capab->he_phy_capab_info[HE_PHYCAP_CAP0_IDX] &
		HE_PHY_CAP0_CHANNEL_WIDTH_SET_B3) {
		if(!get_he_cap(sta_he_capab->he_phy_capab_info[HE_PHYCAP_CAP0_IDX],
		HE_PHY_CAP0_CHANNEL_WIDTH_SET_B2)) {
			wpa_printf(MSG_DEBUG, "HE PHY Channel Width Set field is invalid");
			return 1;
		}
		min_he_capab_len += (sizeof(sta_he_capab->he_txrx_mcs_support)/3);
		wpa_printf(MSG_DEBUG, "IE HE Capabilities minimum length for 80+80MHz is (%d)", min_he_capab_len);
	}

	if(sta_he_capab->he_phy_capab_info[HE_PHYCAP_CAP6_IDX] &
		HE_PHY_CAP6_PPE_THRESHOLD_PRESENT) {
		unsigned nof_sts, nof_ru, nbits, nbytes;
		const u8 *he_ppe_thresholds = he_capab + min_he_capab_len;

		nof_sts = (he_ppe_thresholds[0] & HE_PPE_CAP0_NSS_M1) + 1;
		nof_ru  = count_bits_set(he_ppe_thresholds[0] & HE_PPE_CAP0_RU_INDEX_BITMASK);
		nbits   = 3 /* nsts */ + 4 /* ru_index_bitmask */ + (6 * nof_sts * nof_ru);
		nbytes  = BITS_TO_BYTES(nbits);
		wpa_printf(MSG_DEBUG, "IE HE Capabilities PPE: NSS %u, NRU %u, length %u bits -> %u bytes",
			nof_sts, nof_ru, nbits, nbytes);

		min_he_capab_len += nbytes;
		wpa_printf(MSG_DEBUG, "IE HE Capabilities minimum length is %u", min_he_capab_len);
	}

	if(he_capab_len != min_he_capab_len) {
		wpa_printf(MSG_DEBUG, "HE IE length (%d)!=(%d)", he_capab_len, min_he_capab_len);
		wpa_hexdump(MSG_DEBUG, "HE IE received from STA:", he_capab, he_capab_len);
		return 1;
	}

	return 0;
}

u16 copy_sta_he_capab(struct hostapd_data *hapd, struct sta_info *sta,
			const u8 *he_capab, u8 he_capab_len)
{
	if (!he_capab || hapd->conf->disable_11ax || !hapd->iconf->ieee80211ax ||
		check_valid_he_ie_length(he_capab, he_capab_len)) {
		sta->flags &= ~WLAN_STA_HE;
		os_free(sta->he_capabilities);
		sta->he_capabilities = NULL;
		wpa_printf(MSG_DEBUG, "copy_sta_he_capab - he_capab: %p", he_capab);
		wpa_printf(MSG_DEBUG, "copy_sta_he_capab - disable_11ax: %d", hapd->conf->disable_11ax);
		return WLAN_STATUS_SUCCESS;
	}


	if (sta->he_capabilities == NULL) {
		sta->he_capabilities =
			os_zalloc(sizeof(struct ieee80211_he_capabilities));
		if (sta->he_capabilities == NULL)
			return WLAN_STATUS_UNSPECIFIED_FAILURE;
	}

	sta->flags |= WLAN_STA_HE;
	u8 he_mac_phy_size = get_he_mac_phy_size();
	wpa_printf(MSG_DEBUG, "copy_sta_he_capab - he_mac_phy_size:%d", he_mac_phy_size);
	u8 mcs_nss_size = get_he_mcs_nss_size(he_capab);
	wpa_printf(MSG_DEBUG, "copy_sta_he_capab - mcs_nss_size:%d", mcs_nss_size);
	u8 ppe_th_size = get_he_ppe_th_size(he_capab);
	wpa_printf(MSG_DEBUG, "copy_sta_he_capab - ppe_th_size:%d", ppe_th_size);

	const u8 *he_capabilities = he_capab;

	os_memcpy((void*)&sta->he_capabilities->he_mac_capab_info, he_capabilities, sizeof(sta->he_capabilities->he_mac_capab_info));
	he_capabilities += sizeof(sta->he_capabilities->he_mac_capab_info);

	os_memcpy((void*)&sta->he_capabilities->he_phy_capab_info, he_capabilities, sizeof(sta->he_capabilities->he_phy_capab_info));
	he_capabilities += sizeof(sta->he_capabilities->he_phy_capab_info);

	os_memcpy((void*)&sta->he_capabilities->he_txrx_mcs_support,
		he_capabilities,
		(sizeof(sta->he_capabilities->he_txrx_mcs_support)/3));
	he_capabilities += sizeof(sta->he_capabilities->he_txrx_mcs_support)/3;

	if(mcs_nss_size > 4) {
		os_memcpy((void*)&sta->he_capabilities->he_txrx_mcs_support[4],
		he_capabilities,
		(sizeof(sta->he_capabilities->he_txrx_mcs_support)/3));
	he_capabilities += sizeof(sta->he_capabilities->he_txrx_mcs_support)/3;
	}

	if(mcs_nss_size > 8) {
		os_memcpy((void*)&sta->he_capabilities->he_txrx_mcs_support[8],
		he_capabilities,
	(sizeof(sta->he_capabilities->he_txrx_mcs_support)/3));
	he_capabilities += sizeof(sta->he_capabilities->he_txrx_mcs_support)/3;
	}

	os_memcpy((void*)&sta->he_capabilities->he_ppe_thresholds, he_capabilities, ppe_th_size);

	sta->he_capabilities_len_from_sta = he_capab_len;
	wpa_hexdump(MSG_DEBUG, "copy_sta_he_capab - he_capabilities", he_capab, he_capab_len);
	wpa_hexdump(MSG_DEBUG, "copy_sta_he_capab - he_capabilities copied", sta->he_capabilities, sizeof(sta->he_capabilities));
	return WLAN_STATUS_SUCCESS;
}

u16 copy_sta_he_operation(struct hostapd_data *hapd, struct sta_info *sta,
			const u8 *he_operation, u8 he_operation_len) {

	if (!he_operation || hapd->conf->disable_11ax || !hapd->iconf->ieee80211ax) {
		os_free(sta->he_operation);
		sta->he_operation = NULL;
		return WLAN_STATUS_SUCCESS;
	}

	if (sta->he_operation == NULL) {
		sta->he_operation =
			os_zalloc(sizeof(struct ieee80211_he_operation));
		if (sta->he_operation == NULL)
			return WLAN_STATUS_UNSPECIFIED_FAILURE;
	}

	u8 he_operation_copy_size = MIN(sizeof(struct ieee80211_he_operation), he_operation_len);
	os_memcpy((void*)sta->he_operation, he_operation, he_operation_copy_size);
	sta->he_operation_len_from_sta = he_operation_copy_size;

	if(he_operation_copy_size != he_operation_len)
		wpa_printf(MSG_DEBUG, "copy_sta_he_operation - he_operation_copy_size (%d) !=  he_operation_len (%d)",
							 he_operation_copy_size, he_operation_len);

	wpa_hexdump(MSG_DEBUG, "copy_sta_he_operation - he_operation", he_operation, sta->he_operation_len_from_sta);

	return WLAN_STATUS_SUCCESS;
}

void hostapd_get_he_capab(struct hostapd_data *hapd,
		const struct ieee80211_he_capabilities *sta_he_elems,
		struct ieee80211_he_capabilities *out_he_elems,
		u32 he_capabilities_len_from_sta)
{
	if (sta_he_elems == NULL) {
		wpa_printf(MSG_ERROR,
				"No matching HE capabilities from driver");
		return;
	}

	u8 cap = 0, min = 0, chanwidth;
	struct ieee80211_he_capabilities *advertised_our_caps = &hapd->iface->current_mode->he_capab;
	struct ieee80211_he_capabilities *non_advertised_our_caps = &hapd->iconf->he_non_advertised_caps;
	struct ieee80211_he_capabilities *sta_caps = (struct ieee80211_he_capabilities*)sta_he_elems;
	struct ieee80211_he_capabilities *output_caps = (struct ieee80211_he_capabilities *)out_he_elems;
	struct hostapd_hw_modes 				 *mode = hapd->iface->current_mode;

	chanwidth = mode->he_capab.he_phy_capab_info[HE_PHYCAP_CAP0_IDX] &
											hapd->iconf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP0_IDX];

	/* No processing of PPE thresholds for now */
	os_memcpy((void*)&output_caps->he_ppe_thresholds,
	(const void*)&sta_caps->he_ppe_thresholds,
	sizeof(sta_caps->he_ppe_thresholds));

	os_memset(out_he_elems, 0, sizeof(*out_he_elems));
	output_caps->he_mac_capab_info[HE_MACCAP_CAP0_IDX] |=
		min_he_cap(sta_caps->he_mac_capab_info[HE_MACCAP_CAP0_IDX],
		non_advertised_our_caps->he_mac_capab_info[HE_MACCAP_CAP0_IDX],
		HE_MAC_CAP0_HTC_HE_SUPPORT);
	output_caps->he_mac_capab_info[HE_MACCAP_CAP0_IDX] |=
		min_he_cap(sta_caps->he_mac_capab_info[HE_MACCAP_CAP0_IDX],
		advertised_our_caps->he_mac_capab_info[HE_MACCAP_CAP0_IDX],
		HE_MAC_CAP0_TWT_RESPONDER_SUPPORT);
	output_caps->he_mac_capab_info[HE_MACCAP_CAP0_IDX] |=
		min_he_cap(sta_caps->he_mac_capab_info[HE_MACCAP_CAP0_IDX],
		advertised_our_caps->he_mac_capab_info[HE_MACCAP_CAP0_IDX],
		HE_MAC_CAP0_TWT_REQUESTER_SUPPORT);
	output_caps->he_mac_capab_info[HE_MACCAP_CAP0_IDX] |=
		min_he_cap(sta_caps->he_mac_capab_info[HE_MACCAP_CAP0_IDX],
		non_advertised_our_caps->he_mac_capab_info[HE_MACCAP_CAP0_IDX],
		HE_MAC_CAP0_FRAGMENTATION_SUPPORT);
	output_caps->he_mac_capab_info[HE_MACCAP_CAP0_IDX] |=
		min_he_cap(sta_caps->he_mac_capab_info[HE_MACCAP_CAP0_IDX],
		non_advertised_our_caps->he_mac_capab_info[HE_MACCAP_CAP0_IDX],
		HE_MAC_CAP0_MAX_NUM_OF_FRAG_MSDU);
	output_caps->he_mac_capab_info[HE_MACCAP_CAP1_IDX] |=
		min_he_cap(sta_caps->he_mac_capab_info[HE_MACCAP_CAP1_IDX],
		non_advertised_our_caps->he_mac_capab_info[HE_MACCAP_CAP1_IDX],
		HE_MAC_CAP1_MINIMUM_FRAGMENT_SIZE);
	output_caps->he_mac_capab_info[HE_MACCAP_CAP1_IDX] |=
		min_he_cap(sta_caps->he_mac_capab_info[HE_MACCAP_CAP1_IDX],
		non_advertised_our_caps->he_mac_capab_info[HE_MACCAP_CAP1_IDX],
		HE_MAC_CAP1_TRIGGER_FRAME_MAC_PAD_DUR);
	cap |=
		get_he_cap(advertised_our_caps->he_mac_capab_info[HE_MACCAP_CAP4_IDX],
		HE_MAC_CAP4_MULTI_TID_AGGR_TX_SUPPORT);
	cap |=
		(get_he_cap(advertised_our_caps->he_mac_capab_info[HE_MACCAP_CAP5_IDX],
			HE_MAC_CAP5_MULTI_TID_AGGR_TX_SUPPORT) << 1);
	min =
		MIN(cap, get_he_cap(sta_caps->he_mac_capab_info[HE_MACCAP_CAP1_IDX],
			HE_MAC_CAP1_MULTI_TID_AGGR_RX_SUPPORT));
	output_caps->he_mac_capab_info[HE_MACCAP_CAP1_IDX] |=
					set_he_cap(min, HE_MAC_CAP1_MULTI_TID_AGGR_RX_SUPPORT);
	/* this is split field due to location on byte border */
	output_caps->he_mac_capab_info[HE_MACCAP_CAP4_IDX] |=
			set_he_cap(cap, HE_MAC_CAP4_MULTI_TID_AGGR_TX_SUPPORT);
	output_caps->he_mac_capab_info[HE_MACCAP_CAP5_IDX] |=
			set_he_cap((cap >> 1), HE_MAC_CAP5_MULTI_TID_AGGR_TX_SUPPORT);
	if(sta_caps->he_mac_capab_info[HE_MACCAP_CAP0_IDX] &
									HE_MAC_CAP0_HTC_HE_SUPPORT) {
		output_caps->he_mac_capab_info[HE_MACCAP_CAP1_IDX] |=
			min_he_cap(sta_caps->he_mac_capab_info[HE_MACCAP_CAP1_IDX],
			advertised_our_caps->he_mac_capab_info[HE_MACCAP_CAP1_IDX],
			HE_MAC_CAP1_HE_LINK_ADAPTION_SUPPORT);
		output_caps->he_mac_capab_info[HE_MACCAP_CAP2_IDX] |=
			min_he_cap(sta_caps->he_mac_capab_info[HE_MACCAP_CAP2_IDX],
			advertised_our_caps->he_mac_capab_info[HE_MACCAP_CAP2_IDX],
			HE_MAC_CAP2_HE_LINK_ADAPTION_SUPPORT);
		output_caps->he_mac_capab_info[HE_MACCAP_CAP2_IDX] |=
			min_he_cap(sta_caps->he_mac_capab_info[HE_MACCAP_CAP2_IDX],
			advertised_our_caps->he_mac_capab_info[HE_MACCAP_CAP2_IDX],
			HE_MAC_CAP2_BSR_SUPPORT);
		output_caps->he_mac_capab_info[HE_MACCAP_CAP3_IDX] |=
			min_he_cap(sta_caps->he_mac_capab_info[HE_MACCAP_CAP3_IDX],
			non_advertised_our_caps->he_mac_capab_info[HE_MACCAP_CAP3_IDX],
			HE_MAC_CAP3_OM_CONTROL_SUPPORT);
		output_caps->he_mac_capab_info[HE_MACCAP_CAP2_IDX] |=
			min_he_cap(sta_caps->he_mac_capab_info[HE_MACCAP_CAP2_IDX],
			non_advertised_our_caps->he_mac_capab_info[HE_MACCAP_CAP2_IDX],
			HE_MAC_CAP2_TRS_SUPPORT);
	}
	output_caps->he_mac_capab_info[HE_MACCAP_CAP2_IDX] |=
		min_he_cap(sta_caps->he_mac_capab_info[HE_MACCAP_CAP2_IDX],
		non_advertised_our_caps->he_mac_capab_info[HE_MACCAP_CAP2_IDX],
		HE_MAC_CAP2_ALL_ACK_SUPPORT);
	output_caps->he_mac_capab_info[HE_MACCAP_CAP2_IDX] |=
		min_he_cap(sta_caps->he_mac_capab_info[HE_MACCAP_CAP2_IDX],
		advertised_our_caps->he_mac_capab_info[HE_MACCAP_CAP2_IDX],
		HE_MAC_CAP2_BROADCAST_TWT_SUPPORT);
	output_caps->he_mac_capab_info[HE_MACCAP_CAP2_IDX] |=
		min_he_cap(sta_caps->he_mac_capab_info[HE_MACCAP_CAP2_IDX],
		non_advertised_our_caps->he_mac_capab_info[HE_MACCAP_CAP2_IDX],
		HE_MAC_CAP2_32BIT_BA_BITMAP_SUPPORT);
	output_caps->he_mac_capab_info[HE_MACCAP_CAP2_IDX] |=
		min_he_cap(sta_caps->he_mac_capab_info[HE_MACCAP_CAP2_IDX],
		advertised_our_caps->he_mac_capab_info[HE_MACCAP_CAP2_IDX],
		HE_MAC_CAP2_MU_CASCADING_SUPPORT);
	output_caps->he_mac_capab_info[HE_MACCAP_CAP2_IDX] |=
		min_he_cap(sta_caps->he_mac_capab_info[HE_MACCAP_CAP2_IDX],
		non_advertised_our_caps->he_mac_capab_info[HE_MACCAP_CAP2_IDX],
		HE_MAC_CAP2_ACK_ENABLED_AGGREGATION_SUPPORT);
	output_caps->he_mac_capab_info[HE_MACCAP_CAP3_IDX] |=
		min_he_cap(sta_caps->he_mac_capab_info[HE_MACCAP_CAP3_IDX],
		advertised_our_caps->he_mac_capab_info[HE_MACCAP_CAP3_IDX],
		HE_MAC_CAP3_OFDMA_RA_SUPPORT);
	output_caps->he_mac_capab_info[HE_MACCAP_CAP3_IDX] |=
		min_he_cap(sta_caps->he_mac_capab_info[HE_MACCAP_CAP3_IDX],
		non_advertised_our_caps->he_mac_capab_info[HE_MACCAP_CAP3_IDX],
		HE_MAC_CAP3_MAX_AMPDU_LEN_EXP_EXT);
	output_caps->he_mac_capab_info[HE_MACCAP_CAP3_IDX] |=
		min_he_cap(sta_caps->he_mac_capab_info[HE_MACCAP_CAP3_IDX],
		non_advertised_our_caps->he_mac_capab_info[HE_MACCAP_CAP3_IDX],
		HE_MAC_CAP3_AMSDU_FRGMENTATION_SUPPORT);
	output_caps->he_mac_capab_info[HE_MACCAP_CAP3_IDX] |=
		min_he_cap(sta_caps->he_mac_capab_info[HE_MACCAP_CAP3_IDX],
		non_advertised_our_caps->he_mac_capab_info[HE_MACCAP_CAP3_IDX],
		HE_MAC_CAP3_FLEXIBLE_TWT_SCHEDULE_SUPPORT);
	output_caps->he_mac_capab_info[HE_MACCAP_CAP3_IDX] |=
		min_he_cap(sta_caps->he_mac_capab_info[HE_MACCAP_CAP3_IDX],
		non_advertised_our_caps->he_mac_capab_info[HE_MACCAP_CAP3_IDX],
		HE_MAC_CAP3_RX_CONTROL_FRAME_TO_MULTIBSS);
	output_caps->he_mac_capab_info[HE_MACCAP_CAP4_IDX] |=
		min_he_cap(sta_caps->he_mac_capab_info[HE_MACCAP_CAP4_IDX],
		non_advertised_our_caps->he_mac_capab_info[HE_MACCAP_CAP4_IDX],
		HE_MAC_CAP4_BSRP_BQRP_AMPDU_AGGREGATION);
	output_caps->he_mac_capab_info[HE_MACCAP_CAP4_IDX] |=
		min_he_cap(sta_caps->he_mac_capab_info[HE_MACCAP_CAP4_IDX],
		advertised_our_caps->he_mac_capab_info[HE_MACCAP_CAP4_IDX],
		HE_MAC_CAP4_QTP_SUPPORT);
	output_caps->he_mac_capab_info[HE_MACCAP_CAP4_IDX] |=
		min_he_cap(sta_caps->he_mac_capab_info[HE_MACCAP_CAP4_IDX],
		advertised_our_caps->he_mac_capab_info[HE_MACCAP_CAP4_IDX],
		HE_MAC_CAP4_SRP_RESPONDER);
	output_caps->he_mac_capab_info[HE_MACCAP_CAP4_IDX] |=
		min_he_cap(sta_caps->he_mac_capab_info[HE_MACCAP_CAP4_IDX],
		advertised_our_caps->he_mac_capab_info[HE_MACCAP_CAP4_IDX],
		HE_MAC_CAP4_NDP_FEEDBACK_REPORT_SUPPORT);
	output_caps->he_mac_capab_info[HE_MACCAP_CAP4_IDX] |=
		min_he_cap(sta_caps->he_mac_capab_info[HE_MACCAP_CAP4_IDX],
		advertised_our_caps->he_mac_capab_info[HE_MACCAP_CAP4_IDX],
		HE_MAC_CAP4_OPS_SUPPORT);
	output_caps->he_mac_capab_info[HE_MACCAP_CAP4_IDX] |=
		min_he_cap(sta_caps->he_mac_capab_info[HE_MACCAP_CAP4_IDX],
		non_advertised_our_caps->he_mac_capab_info[HE_MACCAP_CAP4_IDX],
		HE_MAC_CAP4_AMSDU_IN_AMPDU_SUPPORT);
	output_caps->he_mac_capab_info[HE_MACCAP_CAP5_IDX] |=
		min_he_cap(sta_caps->he_mac_capab_info[HE_MACCAP_CAP5_IDX],
		advertised_our_caps->he_mac_capab_info[HE_MACCAP_CAP5_IDX],
		HE_MAC_CAP5_HE_SUBCHANNEL_SELE_TRANS_SUP);
	output_caps->he_mac_capab_info[HE_MACCAP_CAP5_IDX] |=
		min_he_cap(sta_caps->he_mac_capab_info[HE_MACCAP_CAP5_IDX],
		non_advertised_our_caps->he_mac_capab_info[HE_MACCAP_CAP5_IDX],
		HE_MAC_CAP5_UL_2X996TONE_RU_SUPPORT);
	output_caps->he_mac_capab_info[HE_MACCAP_CAP5_IDX] |=
		min_he_cap(sta_caps->he_mac_capab_info[HE_MACCAP_CAP5_IDX],
		advertised_our_caps->he_mac_capab_info[HE_MACCAP_CAP5_IDX],
		HE_MAC_CAP5_OM_CONTROL_UL_MU_DATA_DIS_RX_SUP);

	/* PHY */
	output_caps->he_phy_capab_info[HE_PHYCAP_CAP0_IDX] |=
		min_he_cap(sta_caps->he_phy_capab_info[HE_PHYCAP_CAP0_IDX],
		chanwidth,
		HE_PHY_CAP0_CHANNEL_WIDTH_SET_B0);
	output_caps->he_phy_capab_info[HE_PHYCAP_CAP0_IDX] |=
		min_he_cap(sta_caps->he_phy_capab_info[HE_PHYCAP_CAP0_IDX],
		chanwidth,
		HE_PHY_CAP0_CHANNEL_WIDTH_SET_B1);
	output_caps->he_phy_capab_info[HE_PHYCAP_CAP0_IDX] |=
		min_he_cap(sta_caps->he_phy_capab_info[HE_PHYCAP_CAP0_IDX],
		chanwidth,
		HE_PHY_CAP0_CHANNEL_WIDTH_SET_B2);
	output_caps->he_phy_capab_info[HE_PHYCAP_CAP0_IDX] |=
		min_he_cap(sta_caps->he_phy_capab_info[HE_PHYCAP_CAP0_IDX],
		chanwidth,
		HE_PHY_CAP0_CHANNEL_WIDTH_SET_B3);
	if(~((sta_caps->he_phy_capab_info[HE_PHYCAP_CAP0_IDX] & HE_PHY_CAP0_CHANNEL_WIDTH_SET_B0) |
			(sta_caps->he_phy_capab_info[HE_PHYCAP_CAP0_IDX] & HE_PHY_CAP0_CHANNEL_WIDTH_SET_B1) |
			(sta_caps->he_phy_capab_info[HE_PHYCAP_CAP0_IDX] & HE_PHY_CAP0_CHANNEL_WIDTH_SET_B2) |
			(sta_caps->he_phy_capab_info[HE_PHYCAP_CAP0_IDX] & HE_PHY_CAP0_CHANNEL_WIDTH_SET_B3)) &
			(sta_caps->he_phy_capab_info[HE_PHYCAP_CAP8_IDX] & HE_PHY_CAP8_20MHZ_IN_40MHZ_HE_PPDU_2_4_GHZ_BAND)) {
			output_caps->he_phy_capab_info[HE_PHYCAP_CAP0_IDX] |=
				min_he_cap(sta_caps->he_phy_capab_info[HE_PHYCAP_CAP0_IDX],
				chanwidth,
				HE_PHY_CAP0_CHANNEL_WIDTH_SET_B4);
	}
	if(~((sta_caps->he_phy_capab_info[HE_PHYCAP_CAP0_IDX] & HE_PHY_CAP0_CHANNEL_WIDTH_SET_B0) |
			(sta_caps->he_phy_capab_info[HE_PHYCAP_CAP0_IDX] & HE_PHY_CAP0_CHANNEL_WIDTH_SET_B1) |
			(sta_caps->he_phy_capab_info[HE_PHYCAP_CAP0_IDX] & HE_PHY_CAP0_CHANNEL_WIDTH_SET_B2) |
			(sta_caps->he_phy_capab_info[HE_PHYCAP_CAP0_IDX] & HE_PHY_CAP0_CHANNEL_WIDTH_SET_B3)) &
			(sta_caps->he_phy_capab_info[HE_PHYCAP_CAP8_IDX] & HE_PHY_CAP8_20MHZ_IN_160MHZ_HE_PPDU)) {
			output_caps->he_phy_capab_info[HE_PHYCAP_CAP0_IDX] |=
				min_he_cap(sta_caps->he_phy_capab_info[HE_PHYCAP_CAP0_IDX],
				chanwidth,
				HE_PHY_CAP0_CHANNEL_WIDTH_SET_B5);
	}
	output_caps->he_phy_capab_info[HE_PHYCAP_CAP0_IDX] |=
		min_he_cap(sta_caps->he_phy_capab_info[HE_PHYCAP_CAP0_IDX],
		chanwidth,
		HE_PHY_CAP0_CHANNEL_WIDTH_SET_B6);
	output_caps->he_phy_capab_info[HE_PHYCAP_CAP1_IDX] |=
		min_he_cap(sta_caps->he_phy_capab_info[HE_PHYCAP_CAP1_IDX],
		non_advertised_our_caps->he_phy_capab_info[HE_PHYCAP_CAP1_IDX],
		HE_PHY_CAP1_PUN_PREAM_RX);
	output_caps->he_phy_capab_info[HE_PHYCAP_CAP1_IDX] |=
		min_he_cap(sta_caps->he_phy_capab_info[HE_PHYCAP_CAP1_IDX],
		non_advertised_our_caps->he_phy_capab_info[HE_PHYCAP_CAP1_IDX],
		HE_PHY_CAP1_DEVICE_CLASS);
	output_caps->he_phy_capab_info[HE_PHYCAP_CAP1_IDX] |=
		min_he_cap(sta_caps->he_phy_capab_info[HE_PHYCAP_CAP1_IDX],
		advertised_our_caps->he_phy_capab_info[HE_PHYCAP_CAP1_IDX],
		HE_PHY_CAP1_LDPC_CODING_IN_PAYLOAD);
	output_caps->he_phy_capab_info[HE_PHYCAP_CAP1_IDX] |=
		min_he_cap(sta_caps->he_phy_capab_info[HE_PHYCAP_CAP1_IDX],
		non_advertised_our_caps->he_phy_capab_info[HE_PHYCAP_CAP1_IDX],
		HE_PHY_CAP1_SU_PPDU_1XHE_LTF_0_8US_GI);
	if(sta_caps->he_phy_capab_info[HE_PHYCAP_CAP4_IDX] & HE_PHY_CAP4_SU_BEAMFORMEE) {
		output_caps->he_phy_capab_info[HE_PHYCAP_CAP7_IDX] |=
			min_he_cap(sta_caps->he_phy_capab_info[HE_PHYCAP_CAP7_IDX],
			mode->he_capab.he_phy_capab_info[HE_PHYCAP_CAP7_IDX],
			HE_PHY_CAP7_MAX_NC);
		output_caps->he_phy_capab_info[HE_PHYCAP_CAP2_IDX] |=
			min_he_cap(sta_caps->he_phy_capab_info[HE_PHYCAP_CAP2_IDX],
			non_advertised_our_caps->he_phy_capab_info[HE_PHYCAP_CAP2_IDX],
			HE_PHY_CAP2_NDP_4X_HE_LTF_AND_3_2MS_GI);
		min = MIN(get_he_cap(sta_caps->he_phy_capab_info[HE_PHYCAP_CAP4_IDX],
					HE_PHY_CAP4_BF_STS_LESS_OR_EQ_80MHz),
				get_he_cap(advertised_our_caps->he_phy_capab_info[HE_PHYCAP_CAP5_IDX],
					HE_PHY_CAP5_NUM_SOUND_DIM_LESS_80MHz));
		output_caps->he_phy_capab_info[HE_PHYCAP_CAP4_IDX] |=
			set_he_cap(min, HE_PHY_CAP4_BF_STS_LESS_OR_EQ_80MHz);
		min = MIN(get_he_cap(sta_caps->he_phy_capab_info[HE_PHYCAP_CAP4_IDX],
				HE_PHY_CAP4_BF_STS_GREATER_THAN_80MHz),
				get_he_cap(advertised_our_caps->he_phy_capab_info[HE_PHYCAP_CAP5_IDX],
				HE_PHY_CAP5_NUM_SOUND_DIM_GREAT_80MHz));
		output_caps->he_phy_capab_info[HE_PHYCAP_CAP4_IDX] |=
			set_he_cap(min, HE_PHY_CAP4_BF_STS_GREATER_THAN_80MHz);
	}
	if(sta_caps->he_phy_capab_info[HE_PHYCAP_CAP2_IDX] & HE_PHY_CAP2_DOPPLER_TX ||
			sta_caps->he_phy_capab_info[HE_PHYCAP_CAP2_IDX] & HE_PHY_CAP2_DOPPLER_RX) {
		output_caps->he_phy_capab_info[HE_PHYCAP_CAP1_IDX] |=
			min_he_cap(sta_caps->he_phy_capab_info[HE_PHYCAP_CAP1_IDX],
			advertised_our_caps->he_phy_capab_info[HE_PHYCAP_CAP1_IDX],
			HE_PHY_CAP1_MIDAMBLE_TXRX_MAX_NSTS);
		output_caps->he_phy_capab_info[HE_PHYCAP_CAP2_IDX] |=
			min_he_cap(sta_caps->he_phy_capab_info[HE_PHYCAP_CAP2_IDX],
			advertised_our_caps->he_phy_capab_info[HE_PHYCAP_CAP2_IDX],
			HE_PHY_CAP2_MIDAMBLE_TXRX_MAX_NSTS);
		output_caps->he_phy_capab_info[HE_PHYCAP_CAP8_IDX] |=
			min_he_cap(sta_caps->he_phy_capab_info[HE_PHYCAP_CAP8_IDX],
			advertised_our_caps->he_phy_capab_info[HE_PHYCAP_CAP8_IDX],
			HE_PHY_CAP8_MIDAMBLE_TX_RX_2X_AND_1X_HE_LTF);
	}
	output_caps->he_phy_capab_info[HE_PHYCAP_CAP2_IDX] |=
		min_he_cap(sta_caps->he_phy_capab_info[HE_PHYCAP_CAP2_IDX],
		advertised_our_caps->he_phy_capab_info[HE_PHYCAP_CAP2_IDX],
		HE_PHY_CAP2_STBC_TX_LESS_OR_EQUAL_80MHz);
	min = MIN(
			get_he_cap(sta_caps->he_phy_capab_info[HE_PHYCAP_CAP2_IDX], HE_PHY_CAP2_STBC_TX_LESS_OR_EQUAL_80MHz),
			get_he_cap(advertised_our_caps->he_phy_capab_info[HE_PHYCAP_CAP2_IDX], HE_PHY_CAP2_STBC_RX_LESS_OR_EQUAL_80MHz));
	output_caps->he_phy_capab_info[HE_PHYCAP_CAP2_IDX] |=
		set_he_cap(min, HE_PHY_CAP2_STBC_TX_LESS_OR_EQUAL_80MHz);
	min = MIN(
			get_he_cap(sta_caps->he_phy_capab_info[HE_PHYCAP_CAP2_IDX], HE_PHY_CAP2_STBC_RX_LESS_OR_EQUAL_80MHz),
			get_he_cap(advertised_our_caps->he_phy_capab_info[HE_PHYCAP_CAP2_IDX], HE_PHY_CAP2_STBC_TX_LESS_OR_EQUAL_80MHz));
	output_caps->he_phy_capab_info[HE_PHYCAP_CAP2_IDX] |=
		set_he_cap(min, HE_PHY_CAP2_STBC_RX_LESS_OR_EQUAL_80MHz);
	min = MIN(
			get_he_cap(sta_caps->he_phy_capab_info[HE_PHYCAP_CAP2_IDX], HE_PHY_CAP2_DOPPLER_TX),
			get_he_cap(advertised_our_caps->he_phy_capab_info[HE_PHYCAP_CAP2_IDX], HE_PHY_CAP2_DOPPLER_RX));
	output_caps->he_phy_capab_info[HE_PHYCAP_CAP2_IDX] |=
		set_he_cap(min, HE_PHY_CAP2_DOPPLER_TX);
	min = MIN(
			get_he_cap(sta_caps->he_phy_capab_info[HE_PHYCAP_CAP2_IDX], HE_PHY_CAP2_DOPPLER_RX),
			get_he_cap(advertised_our_caps->he_phy_capab_info[HE_PHYCAP_CAP2_IDX], HE_PHY_CAP2_DOPPLER_TX));
	output_caps->he_phy_capab_info[HE_PHYCAP_CAP2_IDX] |=
		set_he_cap(min, HE_PHY_CAP2_DOPPLER_RX);
	output_caps->he_phy_capab_info[HE_PHYCAP_CAP2_IDX] |=
		min_he_cap(sta_caps->he_phy_capab_info[HE_PHYCAP_CAP2_IDX],
		advertised_our_caps->he_phy_capab_info[HE_PHYCAP_CAP2_IDX],
		HE_PHY_CAP2_FULL_BANDWIDTH_UL_MU_MIMO);
	output_caps->he_phy_capab_info[HE_PHYCAP_CAP2_IDX] |=
		min_he_cap(sta_caps->he_phy_capab_info[HE_PHYCAP_CAP2_IDX],
		advertised_our_caps->he_phy_capab_info[HE_PHYCAP_CAP2_IDX],
		HE_PHY_CAP2_PARTIAL_BANDWIDTH_UL_MU_MIMO);
	min = MIN(
			get_he_cap(sta_caps->he_phy_capab_info[HE_PHYCAP_CAP3_IDX], HE_PHY_CAP3_DCM_MAX_CONSTELLATION_TX),
			get_he_cap(non_advertised_our_caps->he_phy_capab_info[HE_PHYCAP_CAP3_IDX], HE_PHY_CAP3_DCM_MAX_CONSTELLATION_RX));
	output_caps->he_phy_capab_info[HE_PHYCAP_CAP3_IDX] |=
		set_he_cap(min, HE_PHY_CAP3_DCM_MAX_CONSTELLATION_TX);
	min = MIN(
			get_he_cap(sta_caps->he_phy_capab_info[HE_PHYCAP_CAP3_IDX], HE_PHY_CAP3_DCM_MAX_CONSTELLATION_RX),
			get_he_cap(non_advertised_our_caps->he_phy_capab_info[HE_PHYCAP_CAP3_IDX], HE_PHY_CAP3_DCM_MAX_CONSTELLATION_TX));
	output_caps->he_phy_capab_info[HE_PHYCAP_CAP3_IDX] |=
		set_he_cap(min, HE_PHY_CAP3_DCM_MAX_CONSTELLATION_RX);
	min = MIN(
			get_he_cap(sta_caps->he_phy_capab_info[HE_PHYCAP_CAP3_IDX], HE_PHY_CAP3_DCM_MAX_NSS_TX),
			get_he_cap(non_advertised_our_caps->he_phy_capab_info[HE_PHYCAP_CAP3_IDX], HE_PHY_CAP3_DCM_MAX_NSS_RX));
	output_caps->he_phy_capab_info[HE_PHYCAP_CAP3_IDX] |=
		set_he_cap(min, HE_PHY_CAP3_DCM_MAX_NSS_TX);
	min = MIN(
			get_he_cap(sta_caps->he_phy_capab_info[HE_PHYCAP_CAP3_IDX], HE_PHY_CAP3_DCM_MAX_NSS_RX),
			get_he_cap(non_advertised_our_caps->he_phy_capab_info[HE_PHYCAP_CAP3_IDX], HE_PHY_CAP3_DCM_MAX_NSS_TX));
	output_caps->he_phy_capab_info[HE_PHYCAP_CAP3_IDX] |=
		set_he_cap(min, HE_PHY_CAP3_DCM_MAX_NSS_RX);
	output_caps->he_phy_capab_info[HE_PHYCAP_CAP3_IDX] |=
		min_he_cap(sta_caps->he_phy_capab_info[HE_PHYCAP_CAP3_IDX],
		advertised_our_caps->he_phy_capab_info[HE_PHYCAP_CAP3_IDX],
		HE_PHY_CAP3_RX_HE_MUPPDU_FROM_NON_AP_STA);
	min = MIN(
			get_he_cap(sta_caps->he_phy_capab_info[HE_PHYCAP_CAP3_IDX], HE_PHY_CAP3_SU_BEAMFORMER),
			get_he_cap(advertised_our_caps->he_phy_capab_info[HE_PHYCAP_CAP4_IDX], HE_PHY_CAP4_SU_BEAMFORMEE));
	output_caps->he_phy_capab_info[HE_PHYCAP_CAP3_IDX] |=
		set_he_cap(min, HE_PHY_CAP3_SU_BEAMFORMER);
	min = MIN(
			get_he_cap(sta_caps->he_phy_capab_info[HE_PHYCAP_CAP4_IDX], HE_PHY_CAP4_SU_BEAMFORMEE),
			get_he_cap(advertised_our_caps->he_phy_capab_info[HE_PHYCAP_CAP3_IDX], HE_PHY_CAP3_SU_BEAMFORMER));
	output_caps->he_phy_capab_info[HE_PHYCAP_CAP4_IDX] |=
		set_he_cap(min, HE_PHY_CAP4_SU_BEAMFORMEE);
	output_caps->he_phy_capab_info[HE_PHYCAP_CAP4_IDX] |=
		min_he_cap(sta_caps->he_phy_capab_info[HE_PHYCAP_CAP4_IDX],
		advertised_our_caps->he_phy_capab_info[HE_PHYCAP_CAP4_IDX],
		HE_PHY_CAP4_MU_BEAMFORMER);

	if(sta_caps->he_phy_capab_info[HE_PHYCAP_CAP4_IDX] &
		HE_PHY_CAP3_SU_BEAMFORMER) {
		min = MIN(
			get_he_cap(sta_caps->he_phy_capab_info[HE_PHYCAP_CAP5_IDX], HE_PHY_CAP5_NUM_SOUND_DIM_LESS_80MHz),
			get_he_cap(advertised_our_caps->he_phy_capab_info[HE_PHYCAP_CAP4_IDX], HE_PHY_CAP4_BF_STS_LESS_OR_EQ_80MHz));
		output_caps->he_phy_capab_info[HE_PHYCAP_CAP5_IDX] |=
			set_he_cap(min, HE_PHY_CAP5_NUM_SOUND_DIM_LESS_80MHz);
		min = MIN(
			get_he_cap(sta_caps->he_phy_capab_info[HE_PHYCAP_CAP5_IDX], HE_PHY_CAP5_NUM_SOUND_DIM_GREAT_80MHz),
			get_he_cap(advertised_our_caps->he_phy_capab_info[HE_PHYCAP_CAP4_IDX], HE_PHY_CAP4_BF_STS_GREATER_THAN_80MHz));
		output_caps->he_phy_capab_info[HE_PHYCAP_CAP5_IDX] |=
			set_he_cap(min, HE_PHY_CAP5_NUM_SOUND_DIM_GREAT_80MHz);
	}
	output_caps->he_phy_capab_info[HE_PHYCAP_CAP5_IDX] |=
		min_he_cap(sta_caps->he_phy_capab_info[HE_PHYCAP_CAP5_IDX],
		non_advertised_our_caps->he_phy_capab_info[HE_PHYCAP_CAP5_IDX],
		HE_PHY_CAP5_NG_16_FOR_SU_FB_SUPPORT);
	output_caps->he_phy_capab_info[HE_PHYCAP_CAP5_IDX] |=
		min_he_cap(sta_caps->he_phy_capab_info[HE_PHYCAP_CAP5_IDX],
		non_advertised_our_caps->he_phy_capab_info[HE_PHYCAP_CAP5_IDX],
		HE_PHY_CAP5_NG_16_FOR_MU_FB_SUPPORT);
	output_caps->he_phy_capab_info[HE_PHYCAP_CAP6_IDX] |=
		min_he_cap(sta_caps->he_phy_capab_info[HE_PHYCAP_CAP6_IDX],
		non_advertised_our_caps->he_phy_capab_info[HE_PHYCAP_CAP6_IDX],
		HE_PHY_CAP6_CODEBOOK_SIZE42_FOR_SU_SUPPORT);
	output_caps->he_phy_capab_info[HE_PHYCAP_CAP6_IDX] |=
		min_he_cap(sta_caps->he_phy_capab_info[HE_PHYCAP_CAP6_IDX],
		non_advertised_our_caps->he_phy_capab_info[HE_PHYCAP_CAP6_IDX],
		HE_PHY_CAP6_CODEBOOK_SIZE75_FOR_MU_SUPPORT);
	output_caps->he_phy_capab_info[HE_PHYCAP_CAP6_IDX] |=
		min_he_cap(sta_caps->he_phy_capab_info[HE_PHYCAP_CAP6_IDX],
		advertised_our_caps->he_phy_capab_info[HE_PHYCAP_CAP6_IDX],
		HE_PHY_CAP6_TRIGGERED_SU_BEAMFORMING_FEEDBACK);
	output_caps->he_phy_capab_info[HE_PHYCAP_CAP6_IDX] |=
		min_he_cap(sta_caps->he_phy_capab_info[HE_PHYCAP_CAP6_IDX],
		advertised_our_caps->he_phy_capab_info[HE_PHYCAP_CAP6_IDX],
		HE_PHY_CAP6_TRIGGERED_MU_BEAMFORMING_PARTIAL_BW_FEEDBACK);
	output_caps->he_phy_capab_info[HE_PHYCAP_CAP6_IDX] |=
		min_he_cap(sta_caps->he_phy_capab_info[HE_PHYCAP_CAP6_IDX],
		advertised_our_caps->he_phy_capab_info[HE_PHYCAP_CAP6_IDX],
		HE_PHY_CAP6_TRIGGERED_CQI_FEEDBACK);
	output_caps->he_phy_capab_info[HE_PHYCAP_CAP6_IDX] |=
		min_he_cap(sta_caps->he_phy_capab_info[HE_PHYCAP_CAP6_IDX],
		advertised_our_caps->he_phy_capab_info[HE_PHYCAP_CAP6_IDX],
		HE_PHY_CAP6_PARTIAL_BANDWIDTH_EXTENDED_RANGE);
	output_caps->he_phy_capab_info[HE_PHYCAP_CAP6_IDX] |=
		min_he_cap(sta_caps->he_phy_capab_info[HE_PHYCAP_CAP6_IDX],
		non_advertised_our_caps->he_phy_capab_info[HE_PHYCAP_CAP6_IDX],
		HE_PHY_CAP6_PARTIAL_BANDWIDTH_DL_MU_MIMO);
	output_caps->he_phy_capab_info[HE_PHYCAP_CAP6_IDX] |=
		min_he_cap(sta_caps->he_phy_capab_info[HE_PHYCAP_CAP6_IDX],
		non_advertised_our_caps->he_phy_capab_info[HE_PHYCAP_CAP6_IDX],
		HE_PHY_CAP6_PPE_THRESHOLD_PRESENT);
	output_caps->he_phy_capab_info[HE_PHYCAP_CAP7_IDX] |=
		min_he_cap(sta_caps->he_phy_capab_info[HE_PHYCAP_CAP7_IDX],
		advertised_our_caps->he_phy_capab_info[HE_PHYCAP_CAP7_IDX],
		HE_PHY_CAP7_SRP_BASED_SR_SUPPORT);
	output_caps->he_phy_capab_info[HE_PHYCAP_CAP7_IDX] |=
		min_he_cap(sta_caps->he_phy_capab_info[HE_PHYCAP_CAP7_IDX],
		advertised_our_caps->he_phy_capab_info[HE_PHYCAP_CAP7_IDX],
		HE_PHY_CAP7_POWER_BOOST_FACTOR_SUPPORT);
	output_caps->he_phy_capab_info[HE_PHYCAP_CAP7_IDX] |=
		min_he_cap(sta_caps->he_phy_capab_info[HE_PHYCAP_CAP7_IDX],
		non_advertised_our_caps->he_phy_capab_info[HE_PHYCAP_CAP7_IDX],
		HE_PHY_CAP7_SU_PPDU_AND_HE_MU_WITH_4X_HE_LTF_0_8US_GI);
	min = MIN(
			get_he_cap(sta_caps->he_phy_capab_info[HE_PHYCAP_CAP7_IDX], HE_PHY_CAP7_STBC_TX_GREATER_THAN_80MHz),
			get_he_cap(advertised_our_caps->he_phy_capab_info[HE_PHYCAP_CAP7_IDX], HE_PHY_CAP7_STBC_RX_GREATER_THAN_80MHz));
	output_caps->he_phy_capab_info[HE_PHYCAP_CAP7_IDX] |=
		set_he_cap(min, HE_PHY_CAP7_STBC_TX_GREATER_THAN_80MHz);
	min = MIN(
			get_he_cap(sta_caps->he_phy_capab_info[HE_PHYCAP_CAP7_IDX], HE_PHY_CAP7_STBC_RX_GREATER_THAN_80MHz),
			get_he_cap(advertised_our_caps->he_phy_capab_info[HE_PHYCAP_CAP7_IDX], HE_PHY_CAP7_STBC_TX_GREATER_THAN_80MHz));
	output_caps->he_phy_capab_info[HE_PHYCAP_CAP7_IDX] |=
		set_he_cap(min, HE_PHY_CAP7_STBC_RX_GREATER_THAN_80MHz);
	if(sta_caps->he_phy_capab_info[HE_PHYCAP_CAP7_IDX] &
			HE_PHY_CAP7_SU_PPDU_AND_HE_MU_WITH_4X_HE_LTF_0_8US_GI)
			output_caps->he_phy_capab_info[HE_PHYCAP_CAP8_IDX] |=
				min_he_cap(sta_caps->he_phy_capab_info[HE_PHYCAP_CAP8_IDX],
				non_advertised_our_caps->he_phy_capab_info[HE_PHYCAP_CAP8_IDX],
				HE_PHY_CAP8_HE_ER_SU_PPDU_4X_HE_LTF_0_8_US_GI);
	if(sta_caps->he_phy_capab_info[HE_PHYCAP_CAP0_IDX] & HE_PHY_CAP0_CHANNEL_WIDTH_SET_B0)
		output_caps->he_phy_capab_info[HE_PHYCAP_CAP8_IDX] |=
			min_he_cap(sta_caps->he_phy_capab_info[HE_PHYCAP_CAP8_IDX],
			non_advertised_our_caps->he_phy_capab_info[HE_PHYCAP_CAP8_IDX],
			HE_PHY_CAP8_20MHZ_IN_40MHZ_HE_PPDU_2_4_GHZ_BAND);
	if(sta_caps->he_phy_capab_info[HE_PHYCAP_CAP0_IDX] & HE_PHY_CAP0_CHANNEL_WIDTH_SET_B1)
		output_caps->he_phy_capab_info[HE_PHYCAP_CAP8_IDX] |=
			min_he_cap(sta_caps->he_phy_capab_info[HE_PHYCAP_CAP8_IDX],
			non_advertised_our_caps->he_phy_capab_info[HE_PHYCAP_CAP8_IDX],
			HE_PHY_CAP8_20MHZ_IN_160MHZ_HE_PPDU);
	if(sta_caps->he_phy_capab_info[HE_PHYCAP_CAP0_IDX] & HE_PHY_CAP0_CHANNEL_WIDTH_SET_B2)
		output_caps->he_phy_capab_info[HE_PHYCAP_CAP8_IDX] |=
			min_he_cap(sta_caps->he_phy_capab_info[HE_PHYCAP_CAP8_IDX],
			non_advertised_our_caps->he_phy_capab_info[HE_PHYCAP_CAP8_IDX],
			HE_PHY_CAP8_80MHZ_IN_160MHZ_HE_PPDU);
	output_caps->he_phy_capab_info[HE_PHYCAP_CAP8_IDX] |=
		min_he_cap(sta_caps->he_phy_capab_info[HE_PHYCAP_CAP8_IDX],
		non_advertised_our_caps->he_phy_capab_info[HE_PHYCAP_CAP8_IDX],
		HE_PHY_CAP8_HE_ER_SU_PPDU_1X_HE_LTF_0_8_US_GI);
	if((sta_caps->he_phy_capab_info[HE_PHYCAP_CAP3_IDX] & HE_PHY_CAP3_DCM_MAX_CONSTELLATION_TX) ||
		(sta_caps->he_phy_capab_info[HE_PHYCAP_CAP3_IDX] & HE_PHY_CAP3_DCM_MAX_CONSTELLATION_RX))
		output_caps->he_phy_capab_info[HE_PHYCAP_CAP8_IDX] |=
			min_he_cap(sta_caps->he_phy_capab_info[HE_PHYCAP_CAP8_IDX],
			advertised_our_caps->he_phy_capab_info[HE_PHYCAP_CAP8_IDX],
			HE_PHY_CAP8_DCM_MAX_BW);
	min = MIN(
			get_he_cap(sta_caps->he_phy_capab_info[HE_PHYCAP_CAP9_IDX], HE_PHY_CAP9_TX_1024_QAM_LESS_THAN_242_TONE_RU_SUPPORT),
			get_he_cap(advertised_our_caps->he_phy_capab_info[HE_PHYCAP_CAP9_IDX], HE_PHY_CAP9_RX_1024_QAM_LESS_THAN_242_TONE_RU_SUPPORT));
	output_caps->he_phy_capab_info[HE_PHYCAP_CAP9_IDX] |=
		set_he_cap(min, HE_PHY_CAP9_TX_1024_QAM_LESS_THAN_242_TONE_RU_SUPPORT);
		min = MIN(
			get_he_cap(sta_caps->he_phy_capab_info[HE_PHYCAP_CAP9_IDX], HE_PHY_CAP9_RX_1024_QAM_LESS_THAN_242_TONE_RU_SUPPORT),
			get_he_cap(advertised_our_caps->he_phy_capab_info[HE_PHYCAP_CAP9_IDX], HE_PHY_CAP9_TX_1024_QAM_LESS_THAN_242_TONE_RU_SUPPORT));
	output_caps->he_phy_capab_info[HE_PHYCAP_CAP9_IDX] |=
		set_he_cap(min, HE_PHY_CAP9_RX_1024_QAM_LESS_THAN_242_TONE_RU_SUPPORT);
	output_caps->he_phy_capab_info[HE_PHYCAP_CAP9_IDX] |=
		min_he_cap(sta_caps->he_phy_capab_info[HE_PHYCAP_CAP9_IDX],
		non_advertised_our_caps->he_phy_capab_info[HE_PHYCAP_CAP9_IDX],
		HE_PHY_CAP9_LONGER_THAN_16_HE_SIGB_OFDM_SYMBOLS_SUPPORT);
	output_caps->he_phy_capab_info[HE_PHYCAP_CAP9_IDX] |=
		min_he_cap(sta_caps->he_phy_capab_info[HE_PHYCAP_CAP9_IDX],
		non_advertised_our_caps->he_phy_capab_info[HE_PHYCAP_CAP9_IDX],
		HE_PHY_CAP9_RX_FULL_BW_SU_USING_MU_PPDU_COMP_SIGB);
	output_caps->he_phy_capab_info[HE_PHYCAP_CAP9_IDX] |=
		min_he_cap(sta_caps->he_phy_capab_info[HE_PHYCAP_CAP9_IDX],
		non_advertised_our_caps->he_phy_capab_info[HE_PHYCAP_CAP9_IDX],
		HE_PHY_CAP9_RX_FULL_BW_SU_USING_MU_PPDU_NON_COMP_SIGB);
	output_caps->he_phy_capab_info[HE_PHYCAP_CAP9_IDX] |=
		min_he_cap(sta_caps->he_phy_capab_info[HE_PHYCAP_CAP9_IDX],
		advertised_our_caps->he_phy_capab_info[HE_PHYCAP_CAP9_IDX],
		HE_PHY_CAP9_NON_TRIGGERED_CQI_FEEDBACK);

	/* Negotiate HE-MCS and NSS set between STA and AP for 80 MHz */
	u8 *our_he_mcs_nss, *sta_he_mcs_nss, *out_he_mcs_nss;
	/* AP RX 80, STA TX 80, ADD_STA TX 80 */
	our_he_mcs_nss = (u8 *)&advertised_our_caps->he_txrx_mcs_support[HE_MCS_NSS_RX_MCS_MAP_LESS_EQ_80_MHZ_PART1_IDX];
	sta_he_mcs_nss = (u8 *)&sta_caps->he_txrx_mcs_support[HE_MCS_NSS_TX_MCS_MAP_LESS_EQ_80_MHZ_PART1_IDX];
	out_he_mcs_nss = (u8 *)&output_caps->he_txrx_mcs_support[HE_MCS_NSS_TX_MCS_MAP_LESS_EQ_80_MHZ_PART1_IDX];
	hostapd_get_he_mcs_nss(our_he_mcs_nss, sta_he_mcs_nss, out_he_mcs_nss);

	/* AP TX 80, STA RX 80, ADD_STA RX 80 */
	our_he_mcs_nss = (u8 *)&advertised_our_caps->he_txrx_mcs_support[HE_MCS_NSS_TX_MCS_MAP_LESS_EQ_80_MHZ_PART1_IDX];
	sta_he_mcs_nss = (u8 *)&sta_caps->he_txrx_mcs_support[HE_MCS_NSS_RX_MCS_MAP_LESS_EQ_80_MHZ_PART1_IDX];
	out_he_mcs_nss = (u8 *)&output_caps->he_txrx_mcs_support[HE_MCS_NSS_RX_MCS_MAP_LESS_EQ_80_MHZ_PART1_IDX];
	hostapd_get_he_mcs_nss(our_he_mcs_nss, sta_he_mcs_nss, out_he_mcs_nss);

	/* Negotiate HE-MCS and NSS set between STA and AP for 160 MHz */
	if (sta_caps->he_phy_capab_info[HE_PHYCAP_CAP0_IDX] & HE_PHY_CAP0_CHANNEL_WIDTH_SET_B2) {
		/* AP RX 160, STA TX 160, ADD_STA TX 160 */
		our_he_mcs_nss = (u8 *)&advertised_our_caps->he_txrx_mcs_support[HE_MCS_NSS_RX_MCS_MAP_160_MHZ_PART1_IDX];
		sta_he_mcs_nss = (u8 *)&sta_caps->he_txrx_mcs_support[HE_MCS_NSS_TX_MCS_MAP_160_MHZ_PART1_IDX];
		out_he_mcs_nss = (u8 *)&output_caps->he_txrx_mcs_support[HE_MCS_NSS_TX_MCS_MAP_160_MHZ_PART1_IDX];
		hostapd_get_he_mcs_nss(our_he_mcs_nss, sta_he_mcs_nss, out_he_mcs_nss);

		/* AP TX 160, STA RX 160, ADD_STA RX 160 */
		our_he_mcs_nss = (u8 *)&advertised_our_caps->he_txrx_mcs_support[HE_MCS_NSS_TX_MCS_MAP_160_MHZ_PART1_IDX];
		sta_he_mcs_nss = (u8 *)&sta_caps->he_txrx_mcs_support[HE_MCS_NSS_RX_MCS_MAP_160_MHZ_PART1_IDX];
		out_he_mcs_nss = (u8 *)&output_caps->he_txrx_mcs_support[HE_MCS_NSS_RX_MCS_MAP_160_MHZ_PART1_IDX];
		hostapd_get_he_mcs_nss(our_he_mcs_nss, sta_he_mcs_nss, out_he_mcs_nss);
	}

	/* Negotiate HE-MCS and NSS set between STA and AP for 80+80 MHz */
	if (sta_caps->he_phy_capab_info[HE_PHYCAP_CAP0_IDX] & HE_PHY_CAP0_CHANNEL_WIDTH_SET_B3) {
		/* AP RX 80+80, STA TX 80+80, ADD_STA TX 80+80 */
		our_he_mcs_nss = (u8 *)&advertised_our_caps->he_txrx_mcs_support[HE_MCS_NSS_RX_MCS_MAP_80P_80_MHZ_PART1_IDX];
		sta_he_mcs_nss = (u8 *)&sta_caps->he_txrx_mcs_support[HE_MCS_NSS_TX_MCS_MAP_80P_80_MHZ_PART1_IDX];
		out_he_mcs_nss = (u8 *)&output_caps->he_txrx_mcs_support[HE_MCS_NSS_TX_MCS_MAP_80P_80_MHZ_PART1_IDX];
		hostapd_get_he_mcs_nss(our_he_mcs_nss, sta_he_mcs_nss, out_he_mcs_nss);

		/* AP TX 80+80, STA RX 80+80, ADD_STA RX 80+80 */
		our_he_mcs_nss = (u8 *)&advertised_our_caps->he_txrx_mcs_support[HE_MCS_NSS_TX_MCS_MAP_80P_80_MHZ_PART1_IDX];
		sta_he_mcs_nss = (u8 *)&sta_caps->he_txrx_mcs_support[HE_MCS_NSS_RX_MCS_MAP_80P_80_MHZ_PART1_IDX];
		out_he_mcs_nss = (u8 *)&output_caps->he_txrx_mcs_support[HE_MCS_NSS_RX_MCS_MAP_80P_80_MHZ_PART1_IDX];
		hostapd_get_he_mcs_nss(our_he_mcs_nss, sta_he_mcs_nss, out_he_mcs_nss);
	}

	wpa_hexdump(MSG_DEBUG, "AP HE elements advertised:", advertised_our_caps , sizeof(*advertised_our_caps));
	wpa_hexdump(MSG_DEBUG, "AP HE elements non-advertised:", non_advertised_our_caps , sizeof(*non_advertised_our_caps));
	wpa_hexdump(MSG_DEBUG, "STA HE elements received:", sta_caps, he_capabilities_len_from_sta);
	wpa_hexdump(MSG_DEBUG, "Negotiated HE elements:", out_he_elems, sizeof(*out_he_elems));
}

u8 * hostapd_eid_he_assoc_response(struct hostapd_data *hapd, struct sta_info *sta, u8 *eid)
{
	u8 *he_mac, *he_phy, *he_mcs, *he_ppe;
	u8 cap = 0, min = 0, chanwidth;
	struct ieee80211_he_capabilities he_cap;

	u8 size = sizeof(he_cap.he_mac_capab_info) + sizeof(he_cap.he_phy_capab_info) +
			(sizeof(he_cap.he_txrx_mcs_support)/3) + sizeof(he_cap.he_ppe_thresholds);

	u8 *pos = eid;
	*pos++ = WLAN_EID_EXTENSION;

	struct hostapd_hw_modes *mode = hapd->iface->current_mode;

	chanwidth = mode->he_capab.he_phy_capab_info[HE_PHYCAP_CAP0_IDX] &
											hapd->iconf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP0_IDX];

	if(chanwidth & HE_PHY_CAP0_CHANNEL_WIDTH_SET_B2) {
		size += sizeof(he_cap.he_txrx_mcs_support)/3;
	}

	if(chanwidth & HE_PHY_CAP0_CHANNEL_WIDTH_SET_B3) {
		size += sizeof(he_cap.he_txrx_mcs_support)/3;
	}

	*pos++ = size + 1;
	*pos++ = WLAN_EID_EXT_HE_CAPABILITIES;

	he_mac = pos;

	os_memset(he_mac, 0, get_he_mac_phy_size());

	/* Negotiate elements to ensure capability match between STA and AP */
	/* MAC */
	he_mac[HE_MACCAP_CAP0_IDX] |=
		min_he_cap(sta->he_capabilities->he_mac_capab_info[HE_MACCAP_CAP0_IDX],
		hapd->iface->conf->he_capab.he_mac_capab_info[HE_MACCAP_CAP0_IDX],
		HE_MAC_CAP0_HTC_HE_SUPPORT);
	he_mac[HE_MACCAP_CAP0_IDX] |=
		min_he_cap(sta->he_capabilities->he_mac_capab_info[HE_MACCAP_CAP0_IDX],
		hapd->iface->conf->he_capab.he_mac_capab_info[HE_MACCAP_CAP0_IDX],
		HE_MAC_CAP0_TWT_REQUESTER_SUPPORT);
	he_mac[HE_MACCAP_CAP0_IDX] |=
		min_he_cap(sta->he_capabilities->he_mac_capab_info[HE_MACCAP_CAP0_IDX],
		hapd->iface->conf->he_capab.he_mac_capab_info[HE_MACCAP_CAP0_IDX],
		HE_MAC_CAP0_TWT_RESPONDER_SUPPORT);
	he_mac[HE_MACCAP_CAP0_IDX] |=
		min_he_cap(sta->he_capabilities->he_mac_capab_info[HE_MACCAP_CAP0_IDX],
		hapd->iface->conf->he_capab.he_mac_capab_info[HE_MACCAP_CAP0_IDX],
		HE_MAC_CAP0_FRAGMENTATION_SUPPORT);
	he_mac[HE_MACCAP_CAP0_IDX] |=
		min_he_cap(sta->he_capabilities->he_mac_capab_info[HE_MACCAP_CAP0_IDX],
		hapd->iface->conf->he_capab.he_mac_capab_info[HE_MACCAP_CAP0_IDX],
		HE_MAC_CAP0_MAX_NUM_OF_FRAG_MSDU);
	if(hapd->iface->conf->he_capab.he_mac_capab_info[HE_MACCAP_CAP0_IDX] & HE_MAC_CAP0_FRAGMENTATION_SUPPORT) {
		he_mac[HE_MACCAP_CAP0_IDX] |=
			min_he_cap(sta->he_capabilities->he_mac_capab_info[HE_MACCAP_CAP0_IDX],
				hapd->iface->conf->he_capab.he_mac_capab_info[HE_MACCAP_CAP0_IDX],
				HE_MAC_CAP0_MAX_NUM_OF_FRAG_MSDU);
		he_mac[HE_MACCAP_CAP1_IDX] |=
			min_he_cap(sta->he_capabilities->he_mac_capab_info[HE_MACCAP_CAP1_IDX],
				hapd->iface->conf->he_capab.he_mac_capab_info[HE_MACCAP_CAP1_IDX],
				HE_MAC_CAP1_MINIMUM_FRAGMENT_SIZE);
	}
	he_mac[HE_PHYCAP_CAP1_IDX] |=
		min_he_cap(sta->he_capabilities->he_mac_capab_info[HE_MACCAP_CAP1_IDX],
			hapd->iface->conf->he_capab.he_mac_capab_info[HE_MACCAP_CAP1_IDX],
			HE_MAC_CAP1_TRIGGER_FRAME_MAC_PAD_DUR);

	cap |=
		get_he_cap(sta->he_capabilities->he_mac_capab_info[HE_MACCAP_CAP4_IDX],
		HE_MAC_CAP4_MULTI_TID_AGGR_TX_SUPPORT);
	cap |=
		(get_he_cap(sta->he_capabilities->he_mac_capab_info[HE_MACCAP_CAP5_IDX],
			HE_MAC_CAP5_MULTI_TID_AGGR_TX_SUPPORT) << 1);
	min =
		MIN(cap, get_he_cap(hapd->iface->conf->he_capab.he_mac_capab_info[HE_MACCAP_CAP1_IDX],
			HE_MAC_CAP1_MULTI_TID_AGGR_RX_SUPPORT));
	he_mac[HE_PHYCAP_CAP1_IDX] |=
					set_he_cap(min, HE_MAC_CAP1_MULTI_TID_AGGR_RX_SUPPORT);
	he_mac[HE_MACCAP_CAP4_IDX] |=
			set_he_cap(min, HE_MAC_CAP4_MULTI_TID_AGGR_TX_SUPPORT);
	he_mac[HE_MACCAP_CAP5_IDX] |=
			set_he_cap((min >> 1), HE_MAC_CAP5_MULTI_TID_AGGR_TX_SUPPORT);

	if(hapd->iface->conf->he_capab.he_mac_capab_info[HE_MACCAP_CAP0_IDX] &
		HE_MAC_CAP0_HTC_HE_SUPPORT) {
		he_mac[HE_MACCAP_CAP1_IDX] |=
			min_he_cap(sta->he_capabilities->he_mac_capab_info[HE_MACCAP_CAP1_IDX],
				hapd->iface->conf->he_capab.he_mac_capab_info[HE_MACCAP_CAP1_IDX],
				HE_MAC_CAP1_HE_LINK_ADAPTION_SUPPORT);
		he_mac[HE_MACCAP_CAP2_IDX] |=
			min_he_cap(sta->he_capabilities->he_mac_capab_info[HE_MACCAP_CAP2_IDX],
				hapd->iface->conf->he_capab.he_mac_capab_info[HE_MACCAP_CAP2_IDX],
				HE_MAC_CAP2_HE_LINK_ADAPTION_SUPPORT);
		he_mac[HE_MACCAP_CAP4_IDX] |=
			min_he_cap(sta->he_capabilities->he_mac_capab_info[HE_MACCAP_CAP4_IDX],
				hapd->iface->conf->he_capab.he_mac_capab_info[HE_MACCAP_CAP4_IDX],
				HE_MAC_CAP4_BQR_SUPPORT);
		he_mac[HE_MACCAP_CAP3_IDX] |=
			min_he_cap(sta->he_capabilities->he_mac_capab_info[HE_MACCAP_CAP3_IDX],
				hapd->iface->conf->he_capab.he_mac_capab_info[HE_MACCAP_CAP3_IDX],
				HE_MAC_CAP3_OM_CONTROL_SUPPORT);
		he_mac[HE_MACCAP_CAP2_IDX] |=
			min_he_cap(sta->he_capabilities->he_mac_capab_info[HE_MACCAP_CAP2_IDX],
				hapd->iface->conf->he_capab.he_mac_capab_info[HE_MACCAP_CAP2_IDX],
				HE_MAC_CAP2_TRS_SUPPORT);
		he_mac[HE_MACCAP_CAP2_IDX] |=
			min_he_cap(sta->he_capabilities->he_mac_capab_info[HE_MACCAP_CAP2_IDX],
				hapd->iface->conf->he_capab.he_mac_capab_info[HE_MACCAP_CAP2_IDX],
				HE_MAC_CAP2_BSR_SUPPORT);
	}
	he_mac[HE_MACCAP_CAP2_IDX] |=
		min_he_cap(sta->he_capabilities->he_mac_capab_info[HE_MACCAP_CAP2_IDX],
			hapd->iface->conf->he_capab.he_mac_capab_info[HE_MACCAP_CAP2_IDX],
			HE_MAC_CAP2_ALL_ACK_SUPPORT);
	he_mac[HE_MACCAP_CAP2_IDX] |=
		min_he_cap(sta->he_capabilities->he_mac_capab_info[HE_MACCAP_CAP2_IDX],
			hapd->iface->conf->he_capab.he_mac_capab_info[HE_MACCAP_CAP2_IDX],
			HE_MAC_CAP2_BROADCAST_TWT_SUPPORT);
	he_mac[HE_MACCAP_CAP2_IDX] |=
		min_he_cap(sta->he_capabilities->he_mac_capab_info[HE_MACCAP_CAP2_IDX],
			hapd->iface->conf->he_capab.he_mac_capab_info[HE_MACCAP_CAP2_IDX],
			HE_MAC_CAP2_32BIT_BA_BITMAP_SUPPORT);
	he_mac[HE_MACCAP_CAP2_IDX] |=
		min_he_cap(sta->he_capabilities->he_mac_capab_info[HE_MACCAP_CAP2_IDX],
			hapd->iface->conf->he_capab.he_mac_capab_info[HE_MACCAP_CAP2_IDX],
			HE_MAC_CAP2_MU_CASCADING_SUPPORT);
	he_mac[HE_MACCAP_CAP2_IDX] |=
		min_he_cap(sta->he_capabilities->he_mac_capab_info[HE_MACCAP_CAP2_IDX],
			hapd->iface->conf->he_capab.he_mac_capab_info[HE_MACCAP_CAP2_IDX],
			HE_MAC_CAP2_ACK_ENABLED_AGGREGATION_SUPPORT);
	he_mac[HE_MACCAP_CAP3_IDX] |=
		min_he_cap(sta->he_capabilities->he_mac_capab_info[HE_MACCAP_CAP3_IDX],
			hapd->iface->conf->he_capab.he_mac_capab_info[HE_MACCAP_CAP3_IDX],
			HE_MAC_CAP3_OFDMA_RA_SUPPORT);
	he_mac[HE_MACCAP_CAP3_IDX] |=
		min_he_cap(sta->he_capabilities->he_mac_capab_info[HE_MACCAP_CAP3_IDX],
			hapd->iface->conf->he_capab.he_mac_capab_info[HE_MACCAP_CAP3_IDX],
			HE_MAC_CAP3_MAX_AMPDU_LEN_EXP_EXT);
	he_mac[HE_MACCAP_CAP3_IDX] |=
		min_he_cap(sta->he_capabilities->he_mac_capab_info[HE_MACCAP_CAP3_IDX],
			hapd->iface->conf->he_capab.he_mac_capab_info[HE_MACCAP_CAP3_IDX],
			HE_MAC_CAP3_AMSDU_FRGMENTATION_SUPPORT);
	he_mac[HE_MACCAP_CAP3_IDX] |=
		min_he_cap(sta->he_capabilities->he_mac_capab_info[HE_MACCAP_CAP3_IDX],
			hapd->iface->conf->he_capab.he_mac_capab_info[HE_MACCAP_CAP3_IDX],
			HE_MAC_CAP3_FLEXIBLE_TWT_SCHEDULE_SUPPORT);
	he_mac[HE_MACCAP_CAP3_IDX] |=
		min_he_cap(sta->he_capabilities->he_mac_capab_info[HE_MACCAP_CAP3_IDX],
			hapd->iface->conf->he_capab.he_mac_capab_info[HE_MACCAP_CAP3_IDX],
			HE_MAC_CAP3_RX_CONTROL_FRAME_TO_MULTIBSS);
	he_mac[HE_MACCAP_CAP4_IDX] |=
		min_he_cap(sta->he_capabilities->he_mac_capab_info[HE_MACCAP_CAP4_IDX],
			hapd->iface->conf->he_capab.he_mac_capab_info[HE_MACCAP_CAP4_IDX],
			HE_MAC_CAP4_BSRP_BQRP_AMPDU_AGGREGATION);
	he_mac[HE_MACCAP_CAP4_IDX] |=
		min_he_cap(sta->he_capabilities->he_mac_capab_info[HE_MACCAP_CAP4_IDX],
			hapd->iface->conf->he_capab.he_mac_capab_info[HE_MACCAP_CAP4_IDX],
			HE_MAC_CAP4_QTP_SUPPORT);
	he_mac[HE_MACCAP_CAP4_IDX] |=
		min_he_cap(sta->he_capabilities->he_mac_capab_info[HE_MACCAP_CAP4_IDX],
			hapd->iface->conf->he_capab.he_mac_capab_info[HE_MACCAP_CAP4_IDX],
			HE_MAC_CAP4_SRP_RESPONDER);
	he_mac[HE_MACCAP_CAP4_IDX] |=
		min_he_cap(sta->he_capabilities->he_mac_capab_info[HE_MACCAP_CAP4_IDX],
			hapd->iface->conf->he_capab.he_mac_capab_info[HE_MACCAP_CAP4_IDX],
			HE_MAC_CAP4_NDP_FEEDBACK_REPORT_SUPPORT);
	he_mac[HE_MACCAP_CAP4_IDX] |=
		min_he_cap(sta->he_capabilities->he_mac_capab_info[HE_MACCAP_CAP4_IDX],
			hapd->iface->conf->he_capab.he_mac_capab_info[HE_MACCAP_CAP4_IDX],
			HE_MAC_CAP4_OPS_SUPPORT);
	he_mac[HE_MACCAP_CAP4_IDX] |=
		min_he_cap(sta->he_capabilities->he_mac_capab_info[HE_MACCAP_CAP4_IDX],
			hapd->iface->conf->he_capab.he_mac_capab_info[HE_MACCAP_CAP4_IDX],
			HE_MAC_CAP4_AMSDU_IN_AMPDU_SUPPORT);
	he_mac[HE_MACCAP_CAP5_IDX] |=
		min_he_cap(sta->he_capabilities->he_mac_capab_info[HE_MACCAP_CAP5_IDX],
			hapd->iface->conf->he_capab.he_mac_capab_info[HE_MACCAP_CAP5_IDX],
			HE_MAC_CAP5_HE_SUBCHANNEL_SELE_TRANS_SUP);
	he_mac[HE_MACCAP_CAP5_IDX] |=
		min_he_cap(sta->he_capabilities->he_mac_capab_info[HE_MACCAP_CAP5_IDX],
			hapd->iface->conf->he_capab.he_mac_capab_info[HE_MACCAP_CAP5_IDX],
			HE_MAC_CAP5_UL_2X996TONE_RU_SUPPORT);
	he_mac[HE_MACCAP_CAP5_IDX] |=
		min_he_cap(sta->he_capabilities->he_mac_capab_info[HE_MACCAP_CAP5_IDX],
			hapd->iface->conf->he_capab.he_mac_capab_info[HE_MACCAP_CAP5_IDX],
			HE_MAC_CAP5_OM_CONTROL_UL_MU_DATA_DIS_RX_SUP);
	he_phy = he_mac + sizeof(he_cap.he_mac_capab_info);
	/* PHY */
	he_phy[HE_PHYCAP_CAP0_IDX] |=
		min_he_cap(sta->he_capabilities->he_phy_capab_info[HE_PHYCAP_CAP0_IDX],
			chanwidth,
			HE_PHY_CAP0_CHANNEL_WIDTH_SET_B0);
	he_phy[HE_PHYCAP_CAP0_IDX] |=
		min_he_cap(sta->he_capabilities->he_phy_capab_info[HE_PHYCAP_CAP0_IDX],
			chanwidth,
			HE_PHY_CAP0_CHANNEL_WIDTH_SET_B1);
	he_phy[HE_PHYCAP_CAP0_IDX] |=
		min_he_cap(sta->he_capabilities->he_phy_capab_info[HE_PHYCAP_CAP0_IDX],
			chanwidth,
			HE_PHY_CAP0_CHANNEL_WIDTH_SET_B2);
	he_phy[HE_PHYCAP_CAP0_IDX] |=
		min_he_cap(sta->he_capabilities->he_phy_capab_info[HE_PHYCAP_CAP0_IDX],
			chanwidth,
			HE_PHY_CAP0_CHANNEL_WIDTH_SET_B3);
	if(~((chanwidth & HE_PHY_CAP0_CHANNEL_WIDTH_SET_B0) |
		(chanwidth & HE_PHY_CAP0_CHANNEL_WIDTH_SET_B1) |
		(chanwidth & HE_PHY_CAP0_CHANNEL_WIDTH_SET_B2) |
		(chanwidth & HE_PHY_CAP0_CHANNEL_WIDTH_SET_B3)) &
		(chanwidth & HE_PHY_CAP8_20MHZ_IN_40MHZ_HE_PPDU_2_4_GHZ_BAND)) {
			he_phy[HE_PHYCAP_CAP0_IDX] |=
				min_he_cap(sta->he_capabilities->he_phy_capab_info[HE_PHYCAP_CAP0_IDX],
				chanwidth,
				HE_PHY_CAP0_CHANNEL_WIDTH_SET_B4);
	}
	if(~((chanwidth & HE_PHY_CAP0_CHANNEL_WIDTH_SET_B0) |
		(chanwidth & HE_PHY_CAP0_CHANNEL_WIDTH_SET_B1) |
		(chanwidth & HE_PHY_CAP0_CHANNEL_WIDTH_SET_B2) |
		(chanwidth & HE_PHY_CAP0_CHANNEL_WIDTH_SET_B3)) &
		(chanwidth & HE_PHY_CAP8_20MHZ_IN_160MHZ_HE_PPDU)) {
			he_phy[HE_PHYCAP_CAP0_IDX] |=
				min_he_cap(sta->he_capabilities->he_phy_capab_info[HE_PHYCAP_CAP0_IDX],
				chanwidth,
				HE_PHY_CAP0_CHANNEL_WIDTH_SET_B5);
	}
	he_phy[HE_PHYCAP_CAP0_IDX] |=
		min_he_cap(sta->he_capabilities->he_phy_capab_info[HE_PHYCAP_CAP0_IDX],
			chanwidth,
			HE_PHY_CAP0_CHANNEL_WIDTH_SET_B6);
	he_phy[HE_PHYCAP_CAP1_IDX] |=
		min_he_cap(sta->he_capabilities->he_phy_capab_info[HE_PHYCAP_CAP1_IDX],
			hapd->iface->conf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP1_IDX],
			HE_PHY_CAP1_PUN_PREAM_RX);
	he_phy[HE_PHYCAP_CAP1_IDX] |=
		min_he_cap(sta->he_capabilities->he_phy_capab_info[HE_PHYCAP_CAP1_IDX],
			hapd->iface->conf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP1_IDX],
			HE_PHY_CAP1_DEVICE_CLASS);
	he_phy[HE_PHYCAP_CAP1_IDX] |=
		min_he_cap(sta->he_capabilities->he_phy_capab_info[HE_PHYCAP_CAP1_IDX],
			hapd->iface->conf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP1_IDX],
			HE_PHY_CAP1_LDPC_CODING_IN_PAYLOAD);
	he_phy[HE_PHYCAP_CAP1_IDX] |=
		min_he_cap(sta->he_capabilities->he_phy_capab_info[HE_PHYCAP_CAP1_IDX],
			hapd->iface->conf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP1_IDX],
			HE_PHY_CAP1_SU_PPDU_1XHE_LTF_0_8US_GI);
	if(hapd->iface->conf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP4_IDX] & HE_PHY_CAP4_SU_BEAMFORMEE) {
		he_phy[HE_PHYCAP_CAP7_IDX] |=
				min_he_cap(sta->he_capabilities->he_phy_capab_info[HE_PHYCAP_CAP7_IDX],
				mode->he_capab.he_phy_capab_info[HE_PHYCAP_CAP7_IDX],
				HE_PHY_CAP7_MAX_NC);
		he_phy[HE_PHYCAP_CAP7_IDX] |=
				min_he_cap(sta->he_capabilities->he_phy_capab_info[HE_PHYCAP_CAP7_IDX],
				hapd->iface->conf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP7_IDX],
				HE_PHY_CAP2_NDP_4X_HE_LTF_AND_3_2MS_GI);
		min = MIN(
			get_he_cap(sta->he_capabilities->he_phy_capab_info[HE_PHYCAP_CAP5_IDX], HE_PHY_CAP5_NUM_SOUND_DIM_LESS_80MHz),
			get_he_cap(hapd->iface->conf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP4_IDX], HE_PHY_CAP4_BF_STS_LESS_OR_EQ_80MHz));
		he_phy[HE_PHYCAP_CAP4_IDX] |=
			set_he_cap(min, HE_PHY_CAP4_BF_STS_LESS_OR_EQ_80MHz);
		min = MIN(
			get_he_cap(sta->he_capabilities->he_phy_capab_info[HE_PHYCAP_CAP5_IDX], HE_PHY_CAP5_NUM_SOUND_DIM_GREAT_80MHz),
			get_he_cap(hapd->iface->conf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP4_IDX], HE_PHY_CAP4_BF_STS_GREATER_THAN_80MHz));
		he_phy[HE_PHYCAP_CAP4_IDX] |=
			set_he_cap(min, HE_PHY_CAP4_BF_STS_GREATER_THAN_80MHz);
	}
	if((hapd->iface->conf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP2_IDX] & HE_PHY_CAP2_DOPPLER_TX) ||
			(hapd->iface->conf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP2_IDX] & HE_PHY_CAP2_DOPPLER_RX)) {
		he_phy[HE_PHYCAP_CAP1_IDX] |=
			min_he_cap(sta->he_capabilities->he_phy_capab_info[HE_PHYCAP_CAP1_IDX],
				hapd->iface->conf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP1_IDX],
				HE_PHY_CAP1_MIDAMBLE_TXRX_MAX_NSTS);
		he_phy[HE_PHYCAP_CAP2_IDX] |=
			min_he_cap(sta->he_capabilities->he_phy_capab_info[HE_PHYCAP_CAP2_IDX],
				hapd->iface->conf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP2_IDX],
				HE_PHY_CAP2_MIDAMBLE_TXRX_MAX_NSTS);
		he_phy[HE_PHYCAP_CAP8_IDX] |=
			min_he_cap(sta->he_capabilities->he_phy_capab_info[HE_PHYCAP_CAP8_IDX],
				hapd->iface->conf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP8_IDX],
				HE_PHY_CAP8_MIDAMBLE_TX_RX_2X_AND_1X_HE_LTF);
	}
	min = MIN(
			get_he_cap(sta->he_capabilities->he_phy_capab_info[HE_PHYCAP_CAP2_IDX], HE_PHY_CAP2_STBC_TX_LESS_OR_EQUAL_80MHz),
			get_he_cap(hapd->iface->conf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP2_IDX], HE_PHY_CAP2_STBC_RX_LESS_OR_EQUAL_80MHz));
		he_phy[HE_PHYCAP_CAP2_IDX] |=
			set_he_cap(min, HE_PHY_CAP2_STBC_TX_LESS_OR_EQUAL_80MHz);
	min = MIN(
			get_he_cap(sta->he_capabilities->he_phy_capab_info[HE_PHYCAP_CAP2_IDX], HE_PHY_CAP2_STBC_RX_LESS_OR_EQUAL_80MHz),
			get_he_cap(hapd->iface->conf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP2_IDX], HE_PHY_CAP2_STBC_TX_LESS_OR_EQUAL_80MHz));
		he_phy[HE_PHYCAP_CAP2_IDX] |=
			set_he_cap(min, HE_PHY_CAP2_STBC_RX_LESS_OR_EQUAL_80MHz);
	min = MIN(
			get_he_cap(sta->he_capabilities->he_phy_capab_info[HE_PHYCAP_CAP2_IDX], HE_PHY_CAP2_DOPPLER_RX),
			get_he_cap(hapd->iface->conf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP2_IDX], HE_PHY_CAP2_DOPPLER_TX));
		he_phy[HE_PHYCAP_CAP2_IDX] |=
			set_he_cap(min, HE_PHY_CAP2_DOPPLER_TX);
		min = MIN(
			get_he_cap(sta->he_capabilities->he_phy_capab_info[HE_PHYCAP_CAP2_IDX], HE_PHY_CAP2_DOPPLER_TX),
			get_he_cap(hapd->iface->conf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP2_IDX], HE_PHY_CAP2_DOPPLER_RX));
		he_phy[HE_PHYCAP_CAP2_IDX] |=
			set_he_cap(min, HE_PHY_CAP2_DOPPLER_RX);
	he_phy[HE_PHYCAP_CAP2_IDX] |=
		min_he_cap(sta->he_capabilities->he_phy_capab_info[HE_PHYCAP_CAP2_IDX],
			hapd->iface->conf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP2_IDX],
			HE_PHY_CAP2_FULL_BANDWIDTH_UL_MU_MIMO);
	he_phy[HE_PHYCAP_CAP2_IDX] |=
		min_he_cap(sta->he_capabilities->he_phy_capab_info[HE_PHYCAP_CAP2_IDX],
			hapd->iface->conf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP2_IDX],
			HE_PHY_CAP2_PARTIAL_BANDWIDTH_UL_MU_MIMO);
	he_phy[HE_PHYCAP_CAP3_IDX] |=
		min_he_cap(sta->he_capabilities->he_phy_capab_info[HE_PHYCAP_CAP3_IDX],
			hapd->iface->conf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP3_IDX],
			HE_PHY_CAP3_DCM_MAX_CONSTELLATION_TX);
	he_phy[HE_PHYCAP_CAP3_IDX] |=
		min_he_cap(sta->he_capabilities->he_phy_capab_info[HE_PHYCAP_CAP3_IDX],
			hapd->iface->conf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP3_IDX],
			HE_PHY_CAP3_DCM_MAX_CONSTELLATION_RX);
	he_phy[HE_PHYCAP_CAP3_IDX] |=
		min_he_cap(sta->he_capabilities->he_phy_capab_info[HE_PHYCAP_CAP3_IDX],
			hapd->iface->conf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP3_IDX],
			HE_PHY_CAP3_DCM_MAX_NSS_TX);
	he_phy[HE_PHYCAP_CAP3_IDX] |=
		min_he_cap(sta->he_capabilities->he_phy_capab_info[HE_PHYCAP_CAP3_IDX],
			hapd->iface->conf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP3_IDX],
			HE_PHY_CAP3_DCM_MAX_NSS_RX);
	he_phy[HE_PHYCAP_CAP3_IDX] |=
		min_he_cap(sta->he_capabilities->he_phy_capab_info[HE_PHYCAP_CAP3_IDX],
			hapd->iface->conf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP3_IDX],
			HE_PHY_CAP3_RX_HE_MUPPDU_FROM_NON_AP_STA);
	min = MIN(
		get_he_cap(sta->he_capabilities->he_phy_capab_info[HE_PHYCAP_CAP4_IDX], HE_PHY_CAP4_SU_BEAMFORMEE),
			get_he_cap(hapd->iface->conf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP3_IDX], HE_PHY_CAP3_SU_BEAMFORMER));
	he_phy[HE_PHYCAP_CAP3_IDX] |=
			set_he_cap(min, HE_PHY_CAP3_SU_BEAMFORMER);
	min = MIN(
		get_he_cap(sta->he_capabilities->he_phy_capab_info[HE_PHYCAP_CAP3_IDX], HE_PHY_CAP3_SU_BEAMFORMER),
			get_he_cap(hapd->iface->conf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP4_IDX], HE_PHY_CAP4_SU_BEAMFORMEE));
	he_phy[HE_PHYCAP_CAP4_IDX] |=
		set_he_cap(min, HE_PHY_CAP4_SU_BEAMFORMEE);
	he_phy[HE_PHYCAP_CAP4_IDX] |=
		min_he_cap(sta->he_capabilities->he_phy_capab_info[HE_PHYCAP_CAP4_IDX],
			hapd->iface->conf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP4_IDX],
			HE_PHY_CAP4_MU_BEAMFORMER);
	if(hapd->iface->conf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP3_IDX] & HE_PHY_CAP3_SU_BEAMFORMER) {
		min = MIN(
			get_he_cap(sta->he_capabilities->he_phy_capab_info[HE_PHYCAP_CAP4_IDX], HE_PHY_CAP4_BF_STS_LESS_OR_EQ_80MHz),
			get_he_cap(hapd->iface->conf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP5_IDX], HE_PHY_CAP5_NUM_SOUND_DIM_LESS_80MHz));
		he_phy[HE_PHYCAP_CAP5_IDX] |=
			set_he_cap(min, HE_PHY_CAP5_NUM_SOUND_DIM_LESS_80MHz);
		min = MIN(
			get_he_cap(sta->he_capabilities->he_phy_capab_info[HE_PHYCAP_CAP4_IDX], HE_PHY_CAP4_BF_STS_GREATER_THAN_80MHz),
			get_he_cap(hapd->iface->conf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP5_IDX], HE_PHY_CAP5_NUM_SOUND_DIM_GREAT_80MHz));
		he_phy[HE_PHYCAP_CAP5_IDX] |=
			set_he_cap(min, HE_PHY_CAP5_NUM_SOUND_DIM_GREAT_80MHz);
	}
	he_phy[HE_PHYCAP_CAP5_IDX] |=
		min_he_cap(sta->he_capabilities->he_phy_capab_info[HE_PHYCAP_CAP5_IDX],
			hapd->iface->conf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP5_IDX],
			HE_PHY_CAP5_NG_16_FOR_SU_FB_SUPPORT);
	he_phy[HE_PHYCAP_CAP5_IDX] |=
		min_he_cap(sta->he_capabilities->he_phy_capab_info[HE_PHYCAP_CAP5_IDX],
			hapd->iface->conf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP5_IDX],
			HE_PHY_CAP5_NG_16_FOR_MU_FB_SUPPORT);
	he_phy[HE_PHYCAP_CAP6_IDX] |=
		min_he_cap(sta->he_capabilities->he_phy_capab_info[HE_PHYCAP_CAP6_IDX],
			hapd->iface->conf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP6_IDX],
			HE_PHY_CAP6_CODEBOOK_SIZE42_FOR_SU_SUPPORT);
	he_phy[HE_PHYCAP_CAP6_IDX] |=
		min_he_cap(sta->he_capabilities->he_phy_capab_info[HE_PHYCAP_CAP6_IDX],
			hapd->iface->conf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP6_IDX],
			HE_PHY_CAP6_CODEBOOK_SIZE75_FOR_MU_SUPPORT);
	he_phy[HE_PHYCAP_CAP6_IDX] |=
		min_he_cap(sta->he_capabilities->he_phy_capab_info[HE_PHYCAP_CAP6_IDX],
			hapd->iface->conf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP6_IDX],
			HE_PHY_CAP6_TRIGGERED_SU_BEAMFORMING_FEEDBACK);
	he_phy[HE_PHYCAP_CAP6_IDX] |=
		min_he_cap(sta->he_capabilities->he_phy_capab_info[HE_PHYCAP_CAP6_IDX],
			hapd->iface->conf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP6_IDX],
			HE_PHY_CAP6_TRIGGERED_MU_BEAMFORMING_PARTIAL_BW_FEEDBACK);
	he_phy[HE_PHYCAP_CAP6_IDX] |=
		min_he_cap(sta->he_capabilities->he_phy_capab_info[HE_PHYCAP_CAP6_IDX],
			hapd->iface->conf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP6_IDX],
			HE_PHY_CAP6_TRIGGERED_CQI_FEEDBACK);
	he_phy[HE_PHYCAP_CAP6_IDX] |=
		min_he_cap(sta->he_capabilities->he_phy_capab_info[HE_PHYCAP_CAP6_IDX],
			hapd->iface->conf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP6_IDX],
			HE_PHY_CAP6_PARTIAL_BANDWIDTH_EXTENDED_RANGE);
	he_phy[HE_PHYCAP_CAP6_IDX] |=
		min_he_cap(sta->he_capabilities->he_phy_capab_info[HE_PHYCAP_CAP6_IDX],
			hapd->iface->conf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP6_IDX],
			HE_PHY_CAP6_PARTIAL_BANDWIDTH_DL_MU_MIMO);
	he_phy[HE_PHYCAP_CAP6_IDX] |=
		min_he_cap(sta->he_capabilities->he_phy_capab_info[HE_PHYCAP_CAP6_IDX],
			hapd->iface->conf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP6_IDX],
			HE_PHY_CAP6_PPE_THRESHOLD_PRESENT);
	he_phy[HE_PHYCAP_CAP7_IDX] |=
		min_he_cap(sta->he_capabilities->he_phy_capab_info[HE_PHYCAP_CAP7_IDX],
			hapd->iface->conf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP7_IDX],
			HE_PHY_CAP7_SRP_BASED_SR_SUPPORT);
	he_phy[HE_PHYCAP_CAP7_IDX] |=
		min_he_cap(sta->he_capabilities->he_phy_capab_info[HE_PHYCAP_CAP7_IDX],
			hapd->iface->conf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP7_IDX],
			HE_PHY_CAP7_POWER_BOOST_FACTOR_SUPPORT);
	he_phy[HE_PHYCAP_CAP7_IDX] |=
		min_he_cap(sta->he_capabilities->he_phy_capab_info[HE_PHYCAP_CAP7_IDX],
			hapd->iface->conf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP7_IDX],
			HE_PHY_CAP7_SU_PPDU_AND_HE_MU_WITH_4X_HE_LTF_0_8US_GI);
	min = MIN(
		get_he_cap(sta->he_capabilities->he_phy_capab_info[HE_PHYCAP_CAP7_IDX], HE_PHY_CAP7_STBC_TX_GREATER_THAN_80MHz),
			get_he_cap(hapd->iface->conf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP7_IDX], HE_PHY_CAP7_STBC_RX_GREATER_THAN_80MHz));
	he_phy[HE_PHYCAP_CAP7_IDX] |=
		set_he_cap(min, HE_PHY_CAP7_STBC_TX_GREATER_THAN_80MHz);
	min = MIN(
		get_he_cap(sta->he_capabilities->he_phy_capab_info[HE_PHYCAP_CAP7_IDX], HE_PHY_CAP7_STBC_RX_GREATER_THAN_80MHz),
			get_he_cap(hapd->iface->conf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP7_IDX], HE_PHY_CAP7_STBC_TX_GREATER_THAN_80MHz));
	he_phy[HE_PHYCAP_CAP7_IDX] |=
		set_he_cap(min, HE_PHY_CAP7_STBC_RX_GREATER_THAN_80MHz);
	he_phy[HE_PHYCAP_CAP8_IDX] |=
		min_he_cap(sta->he_capabilities->he_phy_capab_info[HE_PHYCAP_CAP8_IDX],
			hapd->iface->conf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP8_IDX],
			HE_PHY_CAP8_HE_ER_SU_PPDU_4X_HE_LTF_0_8_US_GI);
	if(chanwidth & HE_PHY_CAP0_CHANNEL_WIDTH_SET_B0)
		he_phy[HE_PHYCAP_CAP8_IDX] |=
		min_he_cap(sta->he_capabilities->he_phy_capab_info[HE_PHYCAP_CAP8_IDX],
			hapd->iface->conf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP8_IDX],
			HE_PHY_CAP8_20MHZ_IN_40MHZ_HE_PPDU_2_4_GHZ_BAND);
	if(chanwidth & HE_PHY_CAP0_CHANNEL_WIDTH_SET_B1)
		he_phy[HE_PHYCAP_CAP8_IDX] |=
		min_he_cap(sta->he_capabilities->he_phy_capab_info[HE_PHYCAP_CAP8_IDX],
			hapd->iface->conf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP8_IDX],
			HE_PHY_CAP8_20MHZ_IN_160MHZ_HE_PPDU);
	if(chanwidth & HE_PHY_CAP0_CHANNEL_WIDTH_SET_B2)
		he_phy[HE_PHYCAP_CAP8_IDX] |=
		min_he_cap(sta->he_capabilities->he_phy_capab_info[HE_PHYCAP_CAP8_IDX],
			hapd->iface->conf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP8_IDX],
			HE_PHY_CAP8_80MHZ_IN_160MHZ_HE_PPDU);
	he_phy[HE_PHYCAP_CAP8_IDX] |=
		min_he_cap(sta->he_capabilities->he_phy_capab_info[HE_PHYCAP_CAP8_IDX],
			hapd->iface->conf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP8_IDX],
			HE_PHY_CAP8_HE_ER_SU_PPDU_1X_HE_LTF_0_8_US_GI);
	if((hapd->iface->conf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP3_IDX] & HE_PHY_CAP3_DCM_MAX_CONSTELLATION_TX) ||
		(sta->he_capabilities->he_phy_capab_info[HE_PHYCAP_CAP3_IDX] & HE_PHY_CAP3_DCM_MAX_CONSTELLATION_RX))
		he_phy[HE_PHYCAP_CAP8_IDX] |=
		min_he_cap(sta->he_capabilities->he_phy_capab_info[HE_PHYCAP_CAP8_IDX],
			hapd->iface->conf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP8_IDX],
			HE_PHY_CAP8_DCM_MAX_BW);
	min = MIN(
		get_he_cap(sta->he_capabilities->he_phy_capab_info[HE_PHYCAP_CAP9_IDX], HE_PHY_CAP9_TX_1024_QAM_LESS_THAN_242_TONE_RU_SUPPORT),
			get_he_cap(hapd->iface->conf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP9_IDX], HE_PHY_CAP9_RX_1024_QAM_LESS_THAN_242_TONE_RU_SUPPORT));
	he_phy[HE_PHYCAP_CAP9_IDX] |=
		set_he_cap(min, HE_PHY_CAP9_TX_1024_QAM_LESS_THAN_242_TONE_RU_SUPPORT);
	min = MIN(
		get_he_cap(sta->he_capabilities->he_phy_capab_info[HE_PHYCAP_CAP9_IDX], HE_PHY_CAP9_TX_1024_QAM_LESS_THAN_242_TONE_RU_SUPPORT),
			get_he_cap(hapd->iface->conf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP9_IDX], HE_PHY_CAP9_RX_1024_QAM_LESS_THAN_242_TONE_RU_SUPPORT));
	he_phy[HE_PHYCAP_CAP9_IDX] |=
		set_he_cap(min, HE_PHY_CAP9_RX_1024_QAM_LESS_THAN_242_TONE_RU_SUPPORT);
	he_phy[HE_PHYCAP_CAP8_IDX] |=
		min_he_cap(sta->he_capabilities->he_phy_capab_info[HE_PHYCAP_CAP8_IDX],
			hapd->iface->conf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP8_IDX],
			HE_PHY_CAP9_LONGER_THAN_16_HE_SIGB_OFDM_SYMBOLS_SUPPORT);
	he_phy[HE_PHYCAP_CAP9_IDX] |=
		min_he_cap(sta->he_capabilities->he_phy_capab_info[HE_PHYCAP_CAP9_IDX],
			hapd->iface->conf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP9_IDX],
			HE_PHY_CAP9_RX_FULL_BW_SU_USING_MU_PPDU_NON_COMP_SIGB);
	he_phy[HE_PHYCAP_CAP9_IDX] |=
		min_he_cap(sta->he_capabilities->he_phy_capab_info[HE_PHYCAP_CAP9_IDX],
			hapd->iface->conf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP9_IDX],
			HE_PHY_CAP9_RX_FULL_BW_SU_USING_MU_PPDU_COMP_SIGB);

	he_phy[HE_PHYCAP_CAP6_IDX] |=
			min_he_cap(sta->he_capabilities->he_phy_capab_info[HE_PHYCAP_CAP6_IDX],
				hapd->iface->conf->he_capab.he_phy_capab_info[HE_PHYCAP_CAP6_IDX],
				HE_PHY_CAP6_TRIGGERED_CQI_FEEDBACK);
	he_mcs = he_phy + sizeof(he_cap.he_phy_capab_info);
	u8 *our_he_mcs_nss, *sta_he_mcs_nss, *out_he_mcs_nss;
	/* AP RX 80, STA TX 80, assoc resp RX 80 */
	our_he_mcs_nss = (u8 *)&mode->he_capab.he_txrx_mcs_support[HE_MCS_NSS_RX_MCS_MAP_LESS_EQ_80_MHZ_PART1_IDX];
	sta_he_mcs_nss = (u8 *)&sta->he_capabilities->he_txrx_mcs_support[HE_MCS_NSS_TX_MCS_MAP_LESS_EQ_80_MHZ_PART1_IDX];
	out_he_mcs_nss = (u8 *)&he_mcs[HE_MCS_NSS_RX_MCS_MAP_LESS_EQ_80_MHZ_PART1_IDX];
	hostapd_get_he_mcs_nss(our_he_mcs_nss, sta_he_mcs_nss, out_he_mcs_nss);

	/* AP TX 80, STA RX 80, assoc resp TX 80 */
	our_he_mcs_nss = (u8 *)&mode->he_capab.he_txrx_mcs_support[HE_MCS_NSS_TX_MCS_MAP_LESS_EQ_80_MHZ_PART1_IDX];
	sta_he_mcs_nss = (u8 *)&sta->he_capabilities->he_txrx_mcs_support[HE_MCS_NSS_RX_MCS_MAP_LESS_EQ_80_MHZ_PART1_IDX];
	out_he_mcs_nss = (u8 *)&he_mcs[HE_MCS_NSS_TX_MCS_MAP_LESS_EQ_80_MHZ_PART1_IDX];
	hostapd_get_he_mcs_nss(our_he_mcs_nss, sta_he_mcs_nss, out_he_mcs_nss);

	he_ppe = (pos + sizeof(he_cap.he_mac_capab_info) + sizeof(he_cap.he_phy_capab_info) + sizeof(he_cap.he_txrx_mcs_support)/3);
	if(sta->he_capabilities->he_phy_capab_info[HE_PHYCAP_CAP0_IDX] & HE_PHY_CAP0_CHANNEL_WIDTH_SET_B2) {
		/* AP RX 160, STA TX 160, assoc resp RX 160 */
		our_he_mcs_nss = (u8 *)&mode->he_capab.he_txrx_mcs_support[HE_MCS_NSS_RX_MCS_MAP_160_MHZ_PART1_IDX];
		sta_he_mcs_nss = (u8 *)&sta->he_capabilities->he_txrx_mcs_support[HE_MCS_NSS_TX_MCS_MAP_160_MHZ_PART1_IDX];
		out_he_mcs_nss = (u8 *)&he_mcs[HE_MCS_NSS_RX_MCS_MAP_160_MHZ_PART1_IDX];
		hostapd_get_he_mcs_nss(our_he_mcs_nss, sta_he_mcs_nss, out_he_mcs_nss);

		/* AP TX 160, STA RX 160, assoc resp TX 160 */
		our_he_mcs_nss = (u8 *)&mode->he_capab.he_txrx_mcs_support[HE_MCS_NSS_TX_MCS_MAP_160_MHZ_PART1_IDX];
		sta_he_mcs_nss = (u8 *)&sta->he_capabilities->he_txrx_mcs_support[HE_MCS_NSS_RX_MCS_MAP_160_MHZ_PART1_IDX];
		out_he_mcs_nss = (u8 *)&he_mcs[HE_MCS_NSS_TX_MCS_MAP_160_MHZ_PART1_IDX];
		hostapd_get_he_mcs_nss(our_he_mcs_nss, sta_he_mcs_nss, out_he_mcs_nss);
		he_ppe += sizeof(he_cap.he_txrx_mcs_support)/3;
	}

	if(sta->he_capabilities->he_phy_capab_info[HE_PHYCAP_CAP0_IDX] & HE_PHY_CAP0_CHANNEL_WIDTH_SET_B3) {
		/* AP RX 80+80, STA TX 80+80, assoc resp RX 80+80 */
		our_he_mcs_nss = (u8 *)&mode->he_capab.he_txrx_mcs_support[HE_MCS_NSS_RX_MCS_MAP_80P_80_MHZ_PART1_IDX];
		sta_he_mcs_nss = (u8 *)&sta->he_capabilities->he_txrx_mcs_support[HE_MCS_NSS_TX_MCS_MAP_80P_80_MHZ_PART1_IDX];
		out_he_mcs_nss = (u8 *)&he_mcs[HE_MCS_NSS_RX_MCS_MAP_80P_80_MHZ_PART1_IDX];
		hostapd_get_he_mcs_nss(our_he_mcs_nss, sta_he_mcs_nss, out_he_mcs_nss);

		/* AP TX 80+80, STA RX 80+80, assoc resp TX 80+80 */
		our_he_mcs_nss = (u8 *)&mode->he_capab.he_txrx_mcs_support[HE_MCS_NSS_TX_MCS_MAP_80P_80_MHZ_PART1_IDX];
		sta_he_mcs_nss = (u8 *)&sta->he_capabilities->he_txrx_mcs_support[HE_MCS_NSS_RX_MCS_MAP_80P_80_MHZ_PART1_IDX];
		out_he_mcs_nss = (u8 *)&he_mcs[HE_MCS_NSS_TX_MCS_MAP_80P_80_MHZ_PART1_IDX];
		hostapd_get_he_mcs_nss(our_he_mcs_nss, sta_he_mcs_nss, out_he_mcs_nss);
		he_ppe += sizeof(he_cap.he_txrx_mcs_support)/3;
	}

	/* PPE Thresholds */
	os_memcpy(he_ppe, (const u8*)&sta->he_capabilities->he_ppe_thresholds, sizeof(he_cap.he_ppe_thresholds));
	struct ieee80211_he_capabilities *advertised_our_caps = &hapd->iconf->he_capab;
	struct ieee80211_he_capabilities *sta_caps = (struct ieee80211_he_capabilities*)sta->he_capabilities;

	wpa_hexdump(MSG_DEBUG, "AP HE elements advertised:", advertised_our_caps , sizeof(*advertised_our_caps));
	wpa_hexdump(MSG_DEBUG, "STA HE elements received:", sta_caps, sta->he_capabilities_len_from_sta);
	wpa_hexdump(MSG_DEBUG, "Negotiated Assoc Response:", pos, size);

	return pos;
}


