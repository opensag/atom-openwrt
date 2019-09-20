#!/bin/sh
. /lib/netifd/netifd-wireless.sh
. /lib/netifd/hostapd.sh

init_wireless_driver "$@"

MP_CONFIG_INT="mesh_retry_timeout mesh_confirm_timeout mesh_holding_timeout mesh_max_peer_links
	       mesh_max_retries mesh_ttl mesh_element_ttl mesh_hwmp_max_preq_retries
	       mesh_path_refresh_time mesh_min_discovery_timeout mesh_hwmp_active_path_timeout
	       mesh_hwmp_preq_min_interval mesh_hwmp_net_diameter_traversal_time mesh_hwmp_rootmode
	       mesh_hwmp_rann_interval mesh_gate_announcements mesh_sync_offset_max_neighor
	       mesh_rssi_threshold mesh_hwmp_active_path_to_root_timeout mesh_hwmp_root_interval
	       mesh_hwmp_confirmation_interval mesh_awake_window mesh_plink_timeout"
MP_CONFIG_BOOL="mesh_auto_open_plinks mesh_fwding"
MP_CONFIG_STRING="mesh_power_mode"

drv_mac80211_init_device_config() {
	hostapd_common_add_device_config

	config_add_string path phy 'macaddr:macaddr'
	config_add_string hwmode band atf_config_file
	config_add_string acs_smart_info_file acs_history_file
	config_add_int beacon_int chanbw frag rts dfs_debug_chan externally_managed testbed_mode
	config_add_int rxantenna txantenna txpower distance
	config_add_array ht_capab
	config_add_array channels acs_fallback_chan
	config_add_boolean \
		rxldpc \
		short_gi_80 \
		short_gi_160 \
		tx_stbc_2by1 \
		su_beamformer \
		su_beamformee \
		mu_beamformer \
		mu_beamformee \
		vht_txop_ps \
		htc_vht \
		rx_antenna_pattern \
		tx_antenna_pattern
	config_add_int vht_max_a_mpdu_len_exp vht_max_mpdu vht_link_adapt vht160 rx_stbc tx_stbc
	config_add_boolean \
		ldpc \
		greenfield \
		short_gi_20 \
		short_gi_40 \
		max_amsdu \
		dsss_cck_40
	config_add_boolean atf
	config_add_int atf_interval atf_free_time atf_debug
	config_add_int obss_interval

	if [ -f /lib/netifd/debug_infrastructure.sh ]; then
		config_add_string hostapd_log_level
		debug_infrastructure_config_add_string debug_iw_pre_up_
		debug_infrastructure_config_add_string debug_iw_post_up_
	fi
}

drv_mac80211_init_iface_config() {
	hostapd_common_add_bss_config

	config_add_string 'macaddr:macaddr' ifname

	config_add_boolean wds powersave
	config_add_int maxassoc
	config_add_int max_listen_int
	config_add_int dtim_period
	config_add_int start_disabled
	config_add_int atf_vap_grant
	config_add_array 'atf_sta_grants:list(macaddr,int)'

	# mesh
	config_add_string mesh_id
	config_add_int $MP_CONFIG_INT
	config_add_boolean $MP_CONFIG_BOOL
	config_add_string $MP_CONFIG_STRING
}

mac80211_add_capabilities() {
	local __var="$1"; shift
	local __mask="$1"; shift
	local __out= oifs

	oifs="$IFS"
	IFS=:
	for capab in "$@"; do
		set -- $capab

		[ "$(($4))" -gt 0 ] || continue
		[ "$(($__mask & $2))" -eq "$((${3:-$2}))" ] || continue
		__out="$__out[$1]"
	done
	IFS="$oifs"

	export -n -- "$__var=$__out"
}

get_ht_capab() {
	local ht_capab=

	case "$htmode" in
		VHT20|HT20) ;;
		HT40+|VHT40+) ht_capab="[HT40+]" ;;
		HT40-|VHT40-) ht_capab="[HT40-]" ;;
		*) # 80 or above, add HT40+ if channel allows it
			ht_capab="[HT40+]"
			if [ "$auto_channel" -eq 0 ]; then
				case "$channel" in
					8|9|10|11|12|13|40|48|56|64|104|112|120|128|136|144|153|161) ht_capab="[HT40-]" ;;
				esac
			fi
		;;
	esac

	echo "$ht_capab"
}

mac80211_hostapd_prepare_atf_config() {
	local config_file="$1"
	local atf_cfg=

	json_get_vars atf atf_interval atf_free_time atf_debug

	rm -f "$config_file"

	set_default atf_debug 0
	set_default atf 0
	set_default atf_interval 1000
	set_default atf_free_time 0
	append atf_cfg "debug=$atf_debug" "$N"
	append atf_cfg "distr_type=$atf" "$N"
	append atf_cfg "algo_type=1" "$N"
	append atf_cfg "vap_enabled=1" "$N"
	append atf_cfg "station_enabled=1" "$N"
	append atf_cfg "weighted_type=0" "$N"
	append atf_cfg "interval=$atf_interval" "$N"
	append atf_cfg "free_time=$atf_free_time" "$N"

	cat >> "$config_file" <<EOF
$atf_cfg

EOF
}

mac80211_hostapd_setup_base() {
	local phy="$1"

	json_select config
	json_get_vars band

	[ "$auto_channel" -gt 0 ] && {
		channel=acs_smart
		json_get_values channel_list channels
		json_get_values acs_fallback_chan_list acs_fallback_chan
	}

	json_get_values ht_capab

	ieee80211n=
	ieee80211ac=
	ieee80211ax=
	case "$hwmode" in
		ng|bgn|n|an) ieee80211n=1 ;;
		ac) ieee80211ac=1 ;;
		nac|anac)
			ieee80211n=1
			ieee80211ac=1
		;;
		ax) ieee80211ax=ax ;;
		bgnax)
			ieee80211n=1
			ieee80211ax=1
		;;
		anacax)
			ieee80211n=1
			ieee80211ac=1
			ieee80211ax=1
		;;
	esac

	case "$hwmode" in
		*g*) hwmode=g ;;
		*b*) hwmode=b ;;
		*a*) hwmode=a ;;
	esac

	ht_capab=`get_ht_capab`

	[ -n "$ieee80211n" ] && {
		append base_cfg "ieee80211n=1" "$N"

		json_get_vars \
			ldpc:1 \
			greenfield:0 \
			short_gi_20:1 \
			short_gi_40:1 \
			tx_stbc:1 \
			rx_stbc:3 \
			max_amsdu:1 \
			dsss_cck_40:1

		ht_cap_mask=0
		for cap in $(iw phy "$phy" info | grep 'Capabilities:' | cut -d: -f2); do
			ht_cap_mask="$(($ht_cap_mask | $cap))"
		done

		cap_rx_stbc=$((($ht_cap_mask >> 8) & 3))
		[ "$rx_stbc" -lt "$cap_rx_stbc" ] && cap_rx_stbc="$rx_stbc"
		ht_cap_mask="$(( ($ht_cap_mask & ~(0x300)) | ($cap_rx_stbc << 8) ))"

		mac80211_add_capabilities ht_capab_flags $ht_cap_mask \
			LDPC:0x1::$ldpc \
			GF:0x10::$greenfield \
			SHORT-GI-20:0x20::$short_gi_20 \
			SHORT-GI-40:0x40::$short_gi_40 \
			TX-STBC:0x80::$tx_stbc \
			RX-STBC1:0x300:0x100:1 \
			RX-STBC12:0x300:0x200:1 \
			RX-STBC123:0x300:0x300:1 \
			MAX-AMSDU-7935:0x800::$max_amsdu \
			DSSS_CCK-40:0x1000::$dsss_cck_40

		ht_capab="$ht_capab$ht_capab_flags"
		[ -n "$ht_capab" ] && append base_cfg "ht_capab=$ht_capab" "$N"
	}

	enable_vht=0
	if [ "$band" = "2.4GHz" ]; then
		json_get_vars obss_interval
		[ -n "$obss_interval" ] && append base_cfg "obss_interval=$obss_interval" "$N"
		enable_vht=1
	fi

	# 802.11ac
	vht_oper_centr_freq_seg0_idx=0
	vht_oper_chwidth=0
	if [ "$band" = "5GHz" ]; then
		case "$htmode" in
			VHT20|VHT40*)
				vht_oper_chwidth=0
				[ "$htmode" = "VHT40+" ] && vht_oper_centr_freq_seg0_idx=$($channel+2)
				[ "$htmode" = "VHT40-" ] && vht_oper_centr_freq_seg0_idx=$($channel-2)
			;;
			VHT80)
				vht_oper_chwidth=1
				case "$channel" in
					36|40|44|48) vht_oper_centr_freq_seg0_idx=42 ;;
					52|56|60|64) vht_oper_centr_freq_seg0_idx=58 ;;
					100|104|108|112) vht_oper_centr_freq_seg0_idx=106 ;;
					116|120|124|128) vht_oper_centr_freq_seg0_idx=122 ;;
					132|136|140|144) vht_oper_centr_freq_seg0_idx=138 ;;
					149|153|157|161) vht_oper_centr_freq_seg0_idx=155 ;;
				esac
			;;
			VHT160)
				vht_oper_chwidth=2
				case "$channel" in
					36|40|44|48|52|56|60|64) vht_oper_centr_freq_seg0_idx=50 ;;
					100|104|108|112|116|120|124|128) vht_oper_centr_freq_seg0_idx=114 ;;
				esac
			;;
		esac
	fi

	if [ -n "$ieee80211ac" ]; then
		if [ "$auto_channel" -gt 0 ]; then
			vht_oper_centr_freq_seg0_idx=0
		fi

		append base_cfg "vht_oper_chwidth=$vht_oper_chwidth" "$N"
		append base_cfg "vht_oper_centr_freq_seg0_idx=$vht_oper_centr_freq_seg0_idx" "$N"
		append base_cfg "ieee80211ac=1" "$N"
		append base_cfg "opmode_notif=1" "$N"
		enable_vht=1
	fi

	if [ "$enable_vht" != "0" ]; then
		json_get_vars \
			rxldpc:1 \
			short_gi_80:1 \
			short_gi_160:1 \
			tx_stbc_2by1:1 \
			su_beamformer:1 \
			su_beamformee:1 \
			mu_beamformer:1 \
			mu_beamformee:1 \
			vht_txop_ps:1 \
			htc_vht:1 \
			rx_antenna_pattern:1 \
			tx_antenna_pattern:1 \
			vht_max_a_mpdu_len_exp:7 \
			vht_max_mpdu:11454 \
			rx_stbc:4 \
			vht_link_adapt:3 \
			vht160:2

		vht_cap=0
		for cap in $(iw phy "$phy" info | awk -F "[()]" '/VHT Capabilities/ { print $2 }'); do
			vht_cap="$(($vht_cap | $cap))"
		done

		cap_rx_stbc=$((($vht_cap >> 8) & 7))
		[ "$rx_stbc" -lt "$cap_rx_stbc" ] && cap_rx_stbc="$rx_stbc"
		vht_cap="$(( ($vht_cap & ~(0x700)) | ($cap_rx_stbc << 8) ))"

		mac80211_add_capabilities vht_capab $vht_cap \
			RXLDPC:0x10::$rxldpc \
			SHORT-GI-80:0x20::$short_gi_80 \
			SHORT-GI-160:0x40::$short_gi_160 \
			TX-STBC-2BY1:0x80::$tx_stbc_2by1 \
			SU-BEAMFORMER:0x800::$su_beamformer \
			SU-BEAMFORMEE:0x1000::$su_beamformee \
			MU-BEAMFORMER:0x80000::$mu_beamformer \
			MU-BEAMFORMEE:0x100000::$mu_beamformee \
			VHT-TXOP-PS:0x200000::$vht_txop_ps \
			HTC-VHT:0x400000::$htc_vht \
			RX-ANTENNA-PATTERN:0x10000000::$rx_antenna_pattern \
			TX-ANTENNA-PATTERN:0x20000000::$tx_antenna_pattern \
			RX-STBC-1:0x700:0x100:1 \
			RX-STBC-12:0x700:0x200:1 \
			RX-STBC-123:0x700:0x300:1 \
			RX-STBC-1234:0x700:0x400:1 \

		# supported Channel widths
		vht160_hw=0
		[ "$(($vht_cap & 12))" -eq 4 -a 1 -le "$vht160" ] && \
			vht160_hw=1
		[ "$(($vht_cap & 12))" -eq 8 -a 2 -le "$vht160" ] && \
			vht160_hw=2
		[ "$vht160_hw" = 1 ] && vht_capab="$vht_capab[VHT160]"
		[ "$vht160_hw" = 2 ] && vht_capab="$vht_capab[VHT160-80PLUS80]"

		# maximum MPDU length
		vht_max_mpdu_hw=3895
		[ "$(($vht_cap & 3))" -ge 1 -a 7991 -le "$vht_max_mpdu" ] && \
			vht_max_mpdu_hw=7991
		[ "$(($vht_cap & 3))" -ge 2 -a 11454 -le "$vht_max_mpdu" ] && \
			vht_max_mpdu_hw=11454
		[ "$vht_max_mpdu_hw" != 3895 ] && \
			vht_capab="$vht_capab[MAX-MPDU-$vht_max_mpdu_hw]"

		# maximum A-MPDU length exponent
		vht_max_a_mpdu_len_exp_hw=0
		[ "$(($vht_cap & 58720256))" -ge 8388608 -a 1 -le "$vht_max_a_mpdu_len_exp" ] && \
			vht_max_a_mpdu_len_exp_hw=1
		[ "$(($vht_cap & 58720256))" -ge 16777216 -a 2 -le "$vht_max_a_mpdu_len_exp" ] && \
			vht_max_a_mpdu_len_exp_hw=2
		[ "$(($vht_cap & 58720256))" -ge 25165824 -a 3 -le "$vht_max_a_mpdu_len_exp" ] && \
			vht_max_a_mpdu_len_exp_hw=3
		[ "$(($vht_cap & 58720256))" -ge 33554432 -a 4 -le "$vht_max_a_mpdu_len_exp" ] && \
			vht_max_a_mpdu_len_exp_hw=4
		[ "$(($vht_cap & 58720256))" -ge 41943040 -a 5 -le "$vht_max_a_mpdu_len_exp" ] && \
			vht_max_a_mpdu_len_exp_hw=5
		[ "$(($vht_cap & 58720256))" -ge 50331648 -a 6 -le "$vht_max_a_mpdu_len_exp" ] && \
			vht_max_a_mpdu_len_exp_hw=6
		[ "$(($vht_cap & 58720256))" -ge 58720256 -a 7 -le "$vht_max_a_mpdu_len_exp" ] && \
			vht_max_a_mpdu_len_exp_hw=7
		vht_capab="$vht_capab[MAX-A-MPDU-LEN-EXP$vht_max_a_mpdu_len_exp_hw]"

		# whether or not the STA supports link adaptation using VHT variant
		vht_link_adapt_hw=0
		[ "$(($vht_cap & 201326592))" -ge 134217728 -a 2 -le "$vht_link_adapt" ] && \
			vht_link_adapt_hw=2
		[ "$(($vht_cap & 201326592))" -ge 201326592 -a 3 -le "$vht_link_adapt" ] && \
			vht_link_adapt_hw=3
		[ "$vht_link_adapt_hw" != 0 ] && \
			vht_capab="$vht_capab[VHT-LINK-ADAPT-$vht_link_adapt_hw]"

		vht_capab="$vht_capab[BF-ANTENNA-4]"

		num_antennas_in_hex=`iw phy "$phy" info | grep Configured | awk '{print $4}' | tr -d '0' | tr -d 'x'`
		case "$num_antennas_in_hex" in
			[f]) sounding_dimension=4 ;;
			[7bde]) sounding_dimension=3 ;;
			[3569ac]) sounding_dimension=2 ;;
			[1248]) sounding_dimension=1 ;;
		esac
		vht_capab="$vht_capab[SOUNDING-DIMENSION-$sounding_dimension]"

		[ -n "$vht_capab" ] && append base_cfg "vht_capab=$vht_capab" "$N"
	fi

	# TODO: HE parameters
	[ -n "$ieee80211ax" ] && append base_cfg "ieee80211ax=1" "$N"

	[ "$auto_channel" -gt 0 ] && {
		json_get_vars acs_smart_info_file acs_history_file

		set_default acs_smart_info_file "/var/run/acs_smart_info_wlan${phy#phy}.txt"
		set_default acs_history_file "/var/run/acs_history_wlan${phy#phy}.txt"
		append base_cfg "acs_num_scans=1" "$N"
		append base_cfg "acs_smart_info_file=$acs_smart_info_file" "$N"
		append base_cfg "acs_history_file=$acs_history_file" "$N"
	}

	if [ -f /lib/netifd/debug_infrastructure.sh ]; then
		debug_infrastructure_json_get_vars debug_hostap_conf_
		debug_infrastructure_append debug_hostap_conf_ base_cfg
	fi

	hostapd_prepare_device_config "$hostapd_conf_file" nl80211
	cat >> "$hostapd_conf_file" <<EOF
${channel:+channel=$channel}
${channel_list:+chanlist=$channel_list}
${acs_fallback_chan_list:+acs_fallback_chan=$acs_fallback_chan_list}
$base_cfg

EOF
	mac80211_hostapd_prepare_atf_config "$hostapd_atf_conf_file"
	json_select ..
}

mac80211_hostapd_setup_atf_bss() {
	local config_file="$1"
	local ifname="$2"
	local atf_cfg=
	local sta_mac
	local sta_precent

	has_content=0
	json_get_vars atf_vap_grant
	json_get_values atf_sta_grants atf_sta_grants

	append atf_cfg "[$ifname]" "$N"
	for atf_sta_grant in $atf_sta_grants; do
		sta_mac=`echo ${atf_sta_grant} | cut -d"," -f1`
		sta_precent=`echo ${atf_sta_grant} | cut -d"," -f2`
		sta_precent=`expr $sta_precent \* 100` # Driver expects it to be multiplied by 100
		append atf_cfg "sta=$sta_mac,$sta_precent" "$N"
		has_content=1
	done
	[ -n "$atf_vap_grant" ] && {
		atf_vap_grant=`expr $atf_vap_grant \* 100` # Driver expects it to be multiplied by 100
		append atf_cfg "vap_grant=$atf_vap_grant" "$N"
		has_content=1
	}

	if [ $has_content -eq 0 ]; then # Prevent having an empty section
		return
	fi

	cat >> "$config_file" <<EOF
$atf_cfg

EOF
}

mac80211_hostapd_setup_bss() {
	local phy="$1"
	local ifname="$2"
	local macaddr="$3"
	local type="$4"

	hostapd_cfg=
	append hostapd_cfg "$type=$ifname" "$N"

	hostapd_set_bss_options hostapd_cfg "$vif" || return 1
	json_get_vars wds dtim_period max_listen_int start_disabled

	set_default wds 0
	set_default start_disabled 0

	[ "$wds" -gt 0 ] && append hostapd_cfg "wds_sta=1" "$N"
	[ "$staidx" -gt 0 -o "$start_disabled" -eq 1 ] && append hostapd_cfg "start_disabled=1" "$N"

	cat >> /var/run/hostapd-$phy.conf <<EOF
$hostapd_cfg
bssid=$macaddr
${dtim_period:+dtim_period=$dtim_period}
${max_listen_int:+max_listen_interval=$max_listen_int}
EOF

	mac80211_hostapd_setup_atf_bss "$hostapd_atf_conf_file" "$ifname"
}

mac80211_get_addr() {
	local phy="$1"
	local idx="$(($2 + 1))"

	head -n $(($macidx + 1)) /sys/class/ieee80211/${phy}/addresses | tail -n1
}

mac80211_generate_mac() {
	local phy="$1"
	local id="${macidx:-0}"

	local ref="$(cat /sys/class/ieee80211/${phy}/macaddress)"
	local mask="$(cat /sys/class/ieee80211/${phy}/address_mask)"

	[ "$mask" = "00:00:00:00:00:00" ] && {
		mask="ff:ff:ff:ff:ff:ff";

		[ "$(wc -l < /sys/class/ieee80211/${phy}/addresses)" -gt 1 ] && {
			addr="$(mac80211_get_addr "$phy" "$id")"
			[ -n "$addr" ] && {
				echo "$addr"
				return
			}
		}
	}

	local oIFS="$IFS"; IFS=":"; set -- $mask; IFS="$oIFS"

	local mask1=$1
	local mask6=$6

	local oIFS="$IFS"; IFS=":"; set -- $ref; IFS="$oIFS"

	macidx=$(($id + 1))
	[ "$((0x$mask1))" -gt 0 ] && {
		b1="0x$1"
		[ "$id" -gt 0 ] && \
			b1=$(($b1 ^ ((($id - 1) << 2) | 0x2)))
		printf "%02x:%s:%s:%s:%s:%s" $b1 $2 $3 $4 $5 $6
		return
	}

	[ "$((0x$mask6))" -lt 255 ] && {
		printf "%s:%s:%s:%s:%s:%02x" $1 $2 $3 $4 $5 $(( 0x$6 ^ $id ))
		return
	}

	off2=$(( (0x$6 + $id) / 0x100 ))
	printf "%s:%s:%s:%s:%02x:%02x" \
		$1 $2 $3 $4 \
		$(( (0x$5 + $off2) % 0x100 )) \
		$(( (0x$6 + $id) % 0x100 ))
}

find_phy() {
	[ -n "$phy" -a -d /sys/class/ieee80211/$phy ] && return 0
	[ -n "$path" ] && {
		for phy in $(ls /sys/class/ieee80211 2>/dev/null); do
			case "$(readlink -f /sys/class/ieee80211/$phy/device)" in
				*$path) return 0;;
			esac
		done
	}
	[ -n "$macaddr" ] && {
		for phy in $(ls /sys/class/ieee80211 2>/dev/null); do
			grep -i -q "$macaddr" "/sys/class/ieee80211/${phy}/macaddress" && return 0
		done
	}
	return 1
}

mac80211_check_ap() {
	has_ap=1
}

mac80211_prepare_vif() {
	json_select config

	json_get_vars ifname mode ssid wds powersave macaddr

	[ -n "$ifname" ] || ifname="wlan${phy#phy}${if_idx:+-$if_idx}"
	if_idx=$((${if_idx:-0} + 1))

	set_default wds 0
	set_default powersave 0

	json_select ..

	[ -n "$macaddr" ] || {
		macaddr="$(mac80211_generate_mac $phy)"
		macidx="$(($macidx + 1))"
	}

	json_add_object data
	json_add_string ifname "$ifname"
	json_close_object
	json_select config

	# It is far easier to delete and create the desired interface
	case "$mode" in
		adhoc)
			iw phy "$phy" interface add "$ifname" type adhoc
		;;
		ap)
			# Hostapd will handle recreating the interface and
			# subsequent virtual APs belonging to the same PHY
			if [ -n "$hostapd_ctrl" ]; then
				type=bss
			else
				type=interface
			fi

			mac80211_hostapd_setup_bss "$phy" "$ifname" "$macaddr" "$type" || return

			[ -n "$hostapd_ctrl" ] || {
				#TODO: enable it back when driver will be single whipy
				#iw phy "$phy" interface add "$ifname" type __ap
				hostapd_ctrl="${hostapd_ctrl:-/var/run/hostapd/$ifname}"
			}
		;;
		mesh)
			iw phy "$phy" interface add "$ifname" type mp
		;;
		monitor)
			iw phy "$phy" interface add "$ifname" type monitor
		;;
		sta)
			local wdsflag=
			staidx="$(($staidx + 1))"
			[ "$wds" -gt 0 ] && wdsflag="4addr on"
			iw phy "$phy" interface add "$ifname" type managed $wdsflag
			[ "$powersave" -gt 0 ] && powersave="on" || powersave="off"
			iw "$ifname" set power_save "$powersave"
		;;
	esac

	case "$mode" in
		monitor|mesh)
			[ "$auto_channel" -gt 0 ] || iw dev "$ifname" set channel "$channel" $htmode
		;;
	esac

	if [ "$mode" != "ap" ]; then
		# ALL ap functionality will be passed to hostapd
		# All interfaces must have unique mac addresses
		# which can either be explicitly set in the device
		# section, or automatically generated
		ip link set dev "$ifname" address "$macaddr"
	fi

	json_select ..
}

mac80211_setup_supplicant() {
	wpa_supplicant_prepare_interface "$ifname" nl80211 || return 1
	wpa_supplicant_add_network "$ifname"
	wpa_supplicant_run "$ifname" ${hostapd_ctrl:+-H $hostapd_ctrl}
}

mac80211_setup_adhoc_htmode() {
	case "$htmode" in
		VHT20|HT20) ibss_htmode=HT20;;
		HT40*|VHT40|VHT160)
			case "$hwmode" in
				a)
					case "$(( ($channel / 4) % 2 ))" in
						1) ibss_htmode="HT40+" ;;
						0) ibss_htmode="HT40-";;
					esac
				;;
				*)
					case "$htmode" in
						HT40+) ibss_htmode="HT40+";;
						HT40-) ibss_htmode="HT40-";;
						*)
							if [ "$channel" -lt 7 ]; then
								ibss_htmode="HT40+"
							else
								ibss_htmode="HT40-"
							fi
						;;
					esac
				;;
			esac
			[ "$auto_channel" -gt 0 ] && ibss_htmode="HT40+"
		;;
		VHT80)
			ibss_htmode="80MHZ"
		;;
		NONE|NOHT)
			ibss_htmode="NOHT"
		;;
		*) ibss_htmode="" ;;
	esac

}

mac80211_setup_adhoc() {
	json_get_vars bssid ssid key mcast_rate

	keyspec=
	[ "$auth_type" = "wep" ] && {
		set_default key 1
		case "$key" in
			[1234])
				local idx
				for idx in 1 2 3 4; do
					json_get_var ikey "key$idx"

					[ -n "$ikey" ] && {
						ikey="$(($idx - 1)):$(prepare_key_wep "$ikey")"
						[ $idx -eq $key ] && ikey="d:$ikey"
						append keyspec "$ikey"
					}
				done
			;;
			*)
				append keyspec "d:0:$(prepare_key_wep "$key")"
			;;
		esac
	}

	brstr=
	for br in $basic_rate_list; do
		wpa_supplicant_add_rate brstr "$br"
	done

	mcval=
	[ -n "$mcast_rate" ] && wpa_supplicant_add_rate mcval "$mcast_rate"

	iw dev "$ifname" ibss join "$ssid" $freq $ibss_htmode fixed-freq $bssid \
		beacon-interval $beacon_int \
		${brstr:+basic-rates $brstr} \
		${mcval:+mcast-rate $mcval} \
		${keyspec:+keys $keyspec}
}

mac80211_setup_vif() {
	local name="$1"
	local failed

	json_select data
	json_get_vars ifname
	json_select ..

	json_select config
	json_get_vars mode
	json_get_var vif_txpower txpower

	ip link set dev "$ifname" up || {
		wireless_setup_vif_failed IFUP_ERROR
		json_select ..
		return
	}

	set_default vif_txpower "$txpower"
	case "$vif_txpower" in
		"12") vif_txpower=9 ;;
		"25") vif_txpower=6 ;;
		"50") vif_txpower=3 ;;
		"100") vif_txpower=0 ;;
		*) vif_txpower= ;;
	esac
	[ -n "$vif_txpower" ] && iw "$ifname" iwlwav sPowerSelection $vif_txpower

	case "$mode" in
		mesh)
			# authsae or wpa_supplicant
			json_get_vars key
			if [ -n "$key" ]; then
				if [ -e "/lib/wifi/authsae.sh" ]; then
					. /lib/wifi/authsae.sh
					authsae_start_interface || failed=1
				else
					wireless_vif_parse_encryption
					mac80211_setup_supplicant || failed=1
				fi
			else
				json_get_vars mesh_id mcast_rate

				mcval=
				[ -n "$mcast_rate" ] && wpa_supplicant_add_rate mcval "$mcast_rate"

				case "$htmode" in
					VHT20|HT20) mesh_htmode=HT20;;
					HT40*|VHT40)
						case "$hwmode" in
							a)
								case "$(( ($channel / 4) % 2 ))" in
									1) mesh_htmode="HT40+" ;;
									0) mesh_htmode="HT40-";;
								esac
							;;
							*)
								case "$htmode" in
									HT40+) mesh_htmode="HT40+";;
									HT40-) mesh_htmode="HT40-";;
									*)
										if [ "$channel" -lt 7 ]; then
											mesh_htmode="HT40+"
										else
											mesh_htmode="HT40-"
										fi
									;;
								esac
							;;
						esac
					;;
					VHT80)
						mesh_htmode="80Mhz"
					;;
					VHT160)
						mesh_htmode="160Mhz"
					;;
					*) mesh_htmode="NOHT" ;;
				esac

				freq="$(get_freq "$phy" "$channel")"
				iw dev "$ifname" mesh join "$mesh_id" freq $freq $mesh_htmode \
					${mcval:+mcast-rate $mcval} \
					beacon-interval $beacon_int
			fi

			for var in $MP_CONFIG_INT $MP_CONFIG_BOOL $MP_CONFIG_STRING; do
				json_get_var mp_val "$var"
				[ -n "$mp_val" ] && iw dev "$ifname" set mesh_param "$var" "$mp_val"
			done
		;;
		adhoc)
			wireless_vif_parse_encryption
			mac80211_setup_adhoc_htmode
			if [ "$wpa" -gt 0 -o "$auto_channel" -gt 0 ]; then
				mac80211_setup_supplicant || failed=1
			else
				mac80211_setup_adhoc
			fi
		;;
		sta)
			mac80211_setup_supplicant || failed=1
		;;
	esac

	json_select ..
	[ -n "$failed" ] || wireless_add_vif "$name" "$ifname"
}

get_freq() {
	local phy="$1"
	local chan="$2"
	iw "$phy" info | grep -E -m1 "(\* ${chan:-....} MHz${chan:+|\\[$chan\\]})" | grep MHz | awk '{print $2}'
}

mac80211_interface_cleanup() {
	local phy="$1"

	for wdev in $(list_phy_interfaces "$phy"); do
		local interface_idx="${wdev:4:1}"
		local phy_idx="${phy:3:1}"
		if [ "$interface_idx" = "$phy_idx" ]; then
			ip link set dev "$wdev" down 2>/dev/null
		fi
		#TODO: enable it back when driver will be single whipy
		#iw dev "$wdev" del
	done
}

drv_mac80211_cleanup() {
	hostapd_common_cleanup
}

drv_mac80211_setup_phy() {
	local radios=""
	local iw_output=""

	iw_output=`iw dev | grep -E 'Interface|type' | awk '{print $2}'`
	radios=$(echo $iw_output | awk '{for (I=1;I<=NF;I++) if ($I == "AP") {print $(I-1)};}')

	for iface in $radios
	do
		iface_idx=`echo $iface | sed "s/[^0-9]//g"`
		phy=`cat /sys/class/net/$iface/phy80211/name`
		uci set wireless.radio$iface_idx.phy=$phy
		uci commit wireless
	done
}

setup_reconf() {
	json_select config
	json_get_vars \
		phy macaddr path \
		country chanbw distance \
		txpower \
		rxantenna txantenna \
		frag rts beacon_int:100 htmode atf_config_file

	if [ -f /lib/netifd/debug_infrastructure.sh ]; then
		json_get_vars hostapd_log_level
		debug_infrastructure_json_get_vars debug_iw_pre_up_
		debug_infrastructure_json_get_vars debug_iw_post_up_
	fi

	json_get_values basic_rate_list basic_rate
	json_select ..

	local action="$1"; shift

	find_phy || {
		echo "Could not find PHY for device '$1'"
		wireless_set_retry 0
		return 1
	}

	wireless_set_data phy="$phy"

	if [ "$action" = "setup" ]; then
		mac80211_interface_cleanup "$phy"
	fi

	# convert channel to frequency
	[ "$auto_channel" -gt 0 ] || freq="$(get_freq "$phy" "$channel")"

	[ -n "$country" ] && {
		iw reg get | grep -q "^country $country:" || {
			iw reg set "$country"
			sleep 1
		}
	}

	which flock > /dev/null 2>&1
	local which_ret=$?
	if [ $which_ret -eq 0 ]; then
		local lock_attempts_left=5
		local lock_file="/tmp/lock_file_$phy"
		exec 222>$lock_file
		flock -n 222
		local flock_ret=$?

		while [[ $flock_ret -ne 0 && $lock_attempts_left -gt 0 ]]; do
			sleep 1
			flock -n 222
			flock_ret=$?
			lock_attempts_left=$((lock_attempts_left-1))
		done

		if [ $lock_attempts_left -le 0 ]; then
			exit 1
		fi
	else
		/usr/bin/logger -t HOSTAPD_CONF -p 3 "flock isn't found..."
		/usr/bin/logger -t HOSTAPD_CONF -p 3 "...Big probability of race condition"
	fi

	set_default atf_config_file "/var/run/hostapd-$phy-atf.conf"
	hostapd_conf_file="/var/run/hostapd-$phy.conf"
	hostapd_atf_conf_file="$atf_config_file"

	no_ap=1
	macidx=0
	staidx=0

	[ -n "$chanbw" ] && {
		for file in /sys/kernel/debug/ieee80211/$phy/ath9k/chanbw /sys/kernel/debug/ieee80211/$phy/ath5k/bwmode; do
			[ -f "$file" ] && echo "$chanbw" > "$file"
		done
	}

	set_default rxantenna all
	set_default txantenna all
	set_default distance 0

	iw phy "$phy" set antenna $txantenna $rxantenna >/dev/null 2>&1
	iw phy "$phy" set distance "$distance"

	[ -n "$frag" ] && iw phy "$phy" set frag "${frag%%.*}"
	[ -n "$rts" ] && iw phy "$phy" set rts "${rts%%.*}"

	has_ap=
	hostapd_ctrl=
	for_each_interface "ap" mac80211_check_ap

	rm -f "$hostapd_conf_file"
	[ -n "$has_ap" ] && mac80211_hostapd_setup_base "$phy"

	for_each_interface "ap" mac80211_prepare_vif

	if [ "$action" = "setup" ]; then
		for_each_interface "sta adhoc mesh monitor" mac80211_prepare_vif
	fi

	case "$action" in
		reconf)
			local reconf_vap="$2"
			local reconf_radio="${reconf_vap%.*}"

			[ -n "$hostapd_ctrl" ] && {
				if [ "$reconf_vap" = "$reconf_radio" ]; then
					/usr/sbin/hostapd_cli -i${reconf_radio} reconf
				else
					/usr/sbin/hostapd_cli -i${reconf_radio} reconf ${reconf_vap}
				fi

				ret="$?"
				[ "$ret" != 0 ] && {
					echo "[MAC]: reconf failed"
					wireless_setup_failed HOSTAPD_RECONF_FAILED
					return
				}
			}
		;;
		setup)
			[ -n "$hostapd_ctrl" ] && {
				radio_index=`cat /sys/class/ieee80211/phy0/index`
				if [ -f /lib/netifd/debug_infrastructure.sh ]; then
					debug_infrastructure_execute_iw_command debug_iw_pre_up_ $radio_index
				fi

				/usr/sbin/hostapd -s"$hostapd_log_level" -P /var/run/wifi-$phy.pid -B "$hostapd_conf_file"
				ret="$?"

				[ "$ret" != 0 ] && {
					echo "[MAC]: setup failed"
					wireless_setup_failed HOSTAPD_START_FAILED
					return
				}

				retry_count=0
				hostapd_pid=
				until [ $retry_count -ge 5 ]
				do
					hostapd_pid=`cat /var/run/wifi-$phy.pid` && break
					retry_count=$[$retry_count+1]
					sleep 1
				done
				[ ! -n "$hostapd_pid" ] && {
					wireless_setup_failed HOSTAPD_START_FAILED
					return
				}

				wireless_add_process "$hostapd_pid" "/usr/sbin/hostapd" 1

				if [ -f /lib/netifd/debug_infrastructure.sh ]; then
					debug_infrastructure_execute_iw_command debug_iw_post_up_ $radio_index
				fi
			}
		;;
		*)
			echo "Unknown action: \"$action\". Doing nothng"
		;;
	esac

	if [ $which_ret -eq 0 ]; then
		flock -u 222
	fi

	for_each_interface "ap sta adhoc mesh monitor" mac80211_setup_vif

	if [ "$action" = "setup" ]; then
		wireless_set_up
	fi
}

drv_mac80211_setup() {
	setup_reconf "setup" "$@"
}

drv_mac80211_reconf() {
	setup_reconf "reconf" "$@"
}

list_phy_interfaces() {
	local phy="$1"
	if [ -d "/sys/class/ieee80211/${phy}/device/net" ]; then
		ls "/sys/class/ieee80211/${phy}/device/net" 2>/dev/null;
	else
		ls "/sys/class/ieee80211/${phy}/device" 2>/dev/null | grep net: | sed -e 's,net:,,g'
	fi
}

drv_mac80211_teardown() {
	wireless_process_kill_all

	json_select data
	json_get_vars phy
	json_select ..

	mac80211_interface_cleanup "$phy"
}

add_driver mac80211
