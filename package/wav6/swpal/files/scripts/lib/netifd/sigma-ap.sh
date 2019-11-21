#!/bin/sh

# Test script for Wi-Fi Sigma Control API for APs
# Commands based on version 10.2.0
#
# On target board run:
# $ cd /tmp
# $ mknod sigma-pipe p
# $ nc -l -p 9000 < sigma-pipe | ./sigma-ap.sh > sigma-pipe &
# or as persistent
# while true; do nc -l -p 9000 < sigma-pipe | "./sigma-ap.sh" > ./sigma-pipe; done &
# On PC run:
# $ nc <board-ip> 8989
# or
# connect with telnet client in raw mode to <board-ip>:8989
# Then enter commands and send them with <ENTER>

# First digit - Major: Changes incompatible with previous version
# Second digit - Minor: Extension features
# Third digit - Fix: Fixes
# TODO: Store md5 checksum inside the script
FW_VERSION=`cat /version.txt | grep "imagename" | awk -F ": " '{print $2}'`
CA_VERSION="Sigma-CAPI-10.2.0-${FW_VERSION}"

MODEL=''
VENDOR='INTEL'
DEBUGGING=1 # 1=enabled 0=disabled
# TIMESTAMP="cat /proc/uptime"
TIMESTAMP=

WLAN_RUNNING=1 # Used to check if stopping is needed

dirname()
{
	full=$1
	file=`basename $full`
	path=${full%%$file}
	[ -z "$path" ] && path=./
	echo $path
}
thispath=`dirname $0`

# create hard coded ft_mac_address - need to be modify for each test event
if [ -e /nvram/ft_mac_address ]; then
	source /nvram/ft_mac_address
fi

info_print()
{
	echo "sigma INFO: $*" > /dev/console
}

debug_print()
{
	if [ "$DEBUGGING" = "1" ]; then
		echo "sigma DEBUG: $*" > /dev/console
	fi
}

error_print()
{
	echo "sigma ERROR: $*" > /dev/console
}



# check if tr command is available
# lower/upper case conversion is faster with external tr
TR=`command -v tr`
if [ "$TR" = "" ]; then
	debug_print "tr not available"
	alias lower=lc_int
	alias upper=uc_int
else
	debug_print "tr available at $TR"
	alias lower=lc_ext
	alias upper=uc_ext
fi

# check if WLAN is currently running
WLAN_DEV_COUNT=`iwconfig 2>/dev/null | grep -c ^[a-z]`
if [ "$WLAN_DEV_COUNT" = "0" ]; then
	debug_print "WLAN not running"
	WLAN_RUNNING=0
else
	debug_print "WLAN running"
	WLAN_RUNNING=1
fi

##### Helper Functions #####

send_running()
{
	IFS=,
	echo "status,RUNNING " `eval $TIMESTAMP`
}

# First char for these function needs to be a "," to be able to also send replies
# without parameters
send_complete()
{
	IFS=,
	echo "status,COMPLETE$*" `eval $TIMESTAMP`
}

send_error()
{
	IFS=,
	echo "status,ERROR$*" `eval $TIMESTAMP`
}

send_invalid()
{
	IFS=,
	echo "status,INVALID$*" `eval $TIMESTAMP`
}

#
# TODO: Check whether sed works faster when all the variables are changed at once
#		Maybe the file read only once in this case.
#

UPPERCHARS=ABCDEFGHIJKLMNOPQRSTUVWXYZ
LOWERCHARS=abcdefghijklmnopqrstuvwxyz

lc_int()
{
	# usage: lc "SOME STRING" "destination variable name"
	i=0
	OUTPUT=""
	while ([ $i -lt ${#1} ]) do
		CUR=${1:$i:1}
		case $UPPERCHARS in
			*$CUR*)
			CUR=${UPPERCHARS%$CUR*}
			OUTPUT="${OUTPUT}${LOWERCHARS:${#CUR}:1}"
		;;
		*)
			OUTPUT="${OUTPUT}$CUR"
		;;
		esac
		i=$((i+1))
	done
	debug_print "lower-${OUTPUT}"
	export ${2}="${OUTPUT}"
}

lc_ext()
{
	export ${2}=`echo $1 | tr '[:upper:]' '[:lower:]'`
}

uc_int()
{
	# usage: uc "some string" -> "SOME STRING"
	i=0
	OUTPUT=""
	while ([ $i -lt ${#1} ]) do
		CUR=${1:$i:1}
		case $LOWERCHARS in
			*$CUR*)
				CUR=${LOWERCHARS%$CUR*}
				OUTPUT="${OUTPUT}${UPPERCHARS:${#CUR}:1}"
			;;
			*)
				OUTPUT="${OUTPUT}$CUR"
			;;
		esac
		i=$((i+1))
	done
	debug_print "upper-${OUTPUT}"
	export ${2}="${OUTPUT}"
}

uc_ext()
{
	export ${2}=`echo $1 | tr '[:lower:]' '[:upper:]'`
}

stop_wlan()
{
	if [ $WLAN_RUNNING = "1" ]; then
		#/etc/rc.d/rc.bringup_wlan stop > /dev/null
		WLAN_RUNNING=0
	fi
}

add_new_default_vap()
{
	debug_print "add_new_default_vap"
}

get_wlan_param_radio_all()
{
	debug_print "$0"
	#export ${1} = get uci param: Channel 
	#export ${2} = get uci param: AutoChannelEnable 
	#export ${3} = get uci param: OperatingStandards 
	#export ${4} = get uci param: OperatingFrequencyBand 
	#export ${5} = get uci param: radioEnable 
	#export ${6} = get uci param: ExtensionChannel 
	#export ${7} = get uci param: MCS 
	#export ${8} = get uci param: GuardInterval 
	#export ${9} = get uci param: OperatingChannelBandwidth 
	#export ${10} = get uci param: RegulatoryDomain 
}

get_wlan_param_ap_all()
{
	debug_print "$0"
	#export ${1} = get uci param: WMMEnable
	#export ${1} = get uci param: UAPSDEnable
}

get_wlan_param_ap_security_all()
{
	debug_print "$0"
	#export ${1} = get uci param: SSID
	#export ${2} = get uci param: ModeEnabled
	#export ${3} = get uci param: WEPKey
	#export ${4} = get uci param: KeyPassphrase
	#export ${5} = get uci param: wpsEnable
	# TODO: wps parameter? ap_wps_enrollee, ap_wps_int_reg, ap_wps_proxy
}

get_wlan_param_qos_all()
{
	debug_print "$0"
#	"ap_cwmin_vo" "ap_cwmax_vo" "ap_aifs_vo" "ap_txop_vo" "ap_acm_val_vo"
	#export ${1} = get uci param: ECWMin_VO
	#export ${2} = get uci param: ECWMax_VO
	#export ${3} = get uci param: AIFSN_VO
	#export ${4} = get uci param: TxOpMax_VO
	#export ${5} = get uci param: AckPolicy_VO
	#export ${6} = get uci param: ECWMin_VI
	#export ${7} = get uci param: ECWMax_VI
	#export ${8} = get uci param: AIFSN_VI
	#export ${9} = get uci param: TxOpMax_VI
	#export ${10} = get uci param: AckPolicy_VI
	#export ${11} = get uci param: ECWMin_BK
	#export ${12} = get uci param: ECWMax_BK
	#export ${13} = get uci param: AIFSN_BK
	#export ${14} = get uci param: TxOpMax_BK
	#export ${15} = get uci param: AckPolicy_BK
	#export ${16} = get uci param: ECWMin_BE
	#export ${17} = get uci param: ECWMax_BE
	#export ${18} = get uci param: AIFSN_BE
	#export ${19} = get uci param: TxOpMax_BE
	#export ${20} = get uci param: AckPolicy_BE
}

get_interface_name()
{
	debug_print "$0"
	#need to implement
	#echo wlan0.0
}

get_radio_interface_name()
{
	debug_print "$0"
	#need to implement
	#echo wlan0
}

get_uci_if()
{
	debug_print "$0"
	interface_name=$1
	uci_if=`uci show | grep -w \'$interface_name\' | sed 's/.ifname/ /g' | awk '{print $1}'`
	echo $uci_if
}

##### Command Functions #####

ap_ca_version()
{
	send_running
	send_complete ",version,$CA_VERSION"
}

ca_get_version()
{
	send_running
	send_complete ",version,$CA_VERSION"
}

ap_set_wireless()
{
	debug_print "ap_set_wireless():start"
	ap_name="INTEL"

	IFS=" "
	# checking for interface parameter (might fail if some value is also "interface")
	send_running
	ap_radio_interface=`get_radio_interface_name $@`
	ap_interface=`get_interface_name $@`
	uci_if=`get_uci_if $ap_interface`

	stop_wlan

	debug_print "ap_radio_interface:$ap_radio_interface"
	debug_print "ap_interface:$ap_interface"

	# TODO: Is reading existing info needed??? Introduces bug of always setting params that weren't changed
	#get_wlan_param_radio_all "ap_channel" "ap_auto_channel" "ap_mode_val" "ap_band_val" "ap_radio" "ap_offset_val" "ap_mcs_fixedrate" "ap_sgi20_val" "ap_width_val" "ap_region"
	# not supported by clish
	#ap_bcnint=100
	#ap_rts=2347
	#get_wlan_param "ssid" "SSID" "ap_ssid"
	#debug_print "***read from clish"
	#debug_print "ap_ssid:$ap_ssid"
	#debug_print "ap_channel:$ap_channel"
	#debug_print "ap_auto_channel:$ap_auto_channel"
	#debug_print "ap_mode_val:$ap_mode_val"
	#debug_print "ap_band_val:$ap_band_val"
	#debug_print "ap_rts:$ap_rts"
	#debug_print "ap_bcnint:$ap_bcnint"
	#debug_print "ap_radio:$ap_radio"
	#debug_print "ap_width_val:$ap_width_val"

	while [ "$1" != "" ]; do
		# for upper case only
		upper "$1" token
		debug_print "while loop $1 - token:$token"
		case "$token" in
		NAME)
			shift
			debug_print "set parameter ap_name=$1"
		;;
		INTERFACE)
			# skip since it was read in loop before
			shift
		;;
		WLAN_TAG)
			shift
			debug_print "set parameter ap_wlan_tag=$1"
		;;
		SSID)
			shift
			ap_ssid=$1
			debug_print "uci set $uci_if.ssid=$ap_ssid"
			`uci set $uci_if.ssid=$ap_ssid`
		;;
		CHANNEL)
			shift
				debug_print "set parameter ap_channel=$1"
			ap_auto_channel=0
		;;
		MODE)
			shift
			debug_print "set parameter ap_mode=$1"
		;;
		WME)
			shift
			debug_print "set parameter ap_wme=$1"
		;;
		WMMPS)
			shift
			debug_print "set parameter ap_wmmps=$1"
		;;
		RTS)
			shift
				debug_print "set parameter ap_rts=$1"
		;;
		FRGMNT)
			shift
				debug_print "set parameter ap_frgmnt=$1"
		;;
		FRGMNTSUPPORT)
			#do nothing
			shift
		;;
		PWRSAVE)
			# param not supported
			shift
				debug_print "set parameter ap_pwrsave=$1"
		;;
		BCNINT)
			shift
				debug_print "set parameter ap_bcnint=$1"
		;;
		RADIO)
			shift
			debug_print "set parameter ap_radio=$1"
		;;
		P2PMGMTBIT)
			# param not supported
			shift
				debug_print "set parameter ap_p2pbit=$1"
		;;
		CHANNELUSAGE)
			# param not supported
			shift
				debug_print "set parameter ap_channelusage=$1"
		;;
		TDLSPROHIBIT)
			# param not supported
			shift
				debug_print "set parameter ap_tdls=$1"
		;;
		TDLSCHSWITCHPROHIBIT)
			# param not supported
			shift
				debug_print "set parameter ap_tdlschannel=$1"
		;;
		WIDTH)
			shift
			debug_print "set parameter ap_width=$1"
		;;
		OFFSET)
			shift
				debug_print "set parameter ap_offset=$1"
		;;
		COUNTRY) ## NB: Extension parameter
			shift
				debug_print "set parameter ap_region=$1"
		;;
		COUNTRYCODE) ## NB: Extension parameter
			shift
				debug_print "set parameter ap_region=$1"
		;;
		REG_DOMAIN) ## NB: Extension parameter
			# param not supported
			shift
		;;
		CELLULAR_CAP_PREF)
			shift
				debug_print "set parameter ap_cellular_cap_pref=$1"
		;;
		GAS_CB_DELAY)
			shift
				debug_print "set parameter ap_gas_cb_delay=$1"
		;;
		DOMAIN)
			shift
				debug_print "set parameter ap_domain=$1"
		;;
		FT_OA)
			shift
				debug_print "set parameter ap_ft_oa=$1"
		;;
		PROGRAM)
			shift
				debug_print "set parameter ap_program=$1"
		;;
		OCESUPPORT)
			shift
				debug_print "set parameter ap_oce_support=$1"
		;;
		FILSDSCV)
			shift
				debug_print "set parameter ap_fils_dscv=$1"
		;;
		FILSDSCVINTERVAL)
			shift
				debug_print "set parameter ap_fils_dscv_interval=$1"
		;;
		BROADCASTSSID)
			shift
				debug_print "set parameter ap_broadcast_ssid=$1"
		 ;;
		FILSHLP)
			shift
				debug_print "set parameter ap_filshlp=$1"
		;;
		NAIREALM)
			shift
				debug_print "set parameter ap_nairealm=$1"
		;;
		RNR)
			shift
				debug_print "set parameter ap_rnr=$1"
		;;
		DEAUTHDISASSOCTX)
			shift
				debug_print "set parameter ap_deauth_disassoc_tx=$1"
		;;
		BLESTACOUNT)
			shift
				debug_print "set parameter ap_ble_sta_count=$1"
		;;
		BLECHANNELUTIL)
			shift
				debug_print "set parameter ap_ble_channel_util=$1"
		;;
		BLEAVAILADMINCAP)
			shift
				debug_print "set parameter ap_ble_avail_admin_cap=$1"
		;;
		AIRTIMEFRACT)
			shift
				debug_print "set parameter ap_air_time_fract=$1"
		;;
		DATAPPDUDURATION)
			shift
				debug_print "set parameter ap_data_ppdu_duration=$1"
		;;
		DHCPSERVIPADDR)
			shift
				debug_print "set parameter dhcp_serv_ip_addr=$1"
		;;
		NSS_MCS_CAP)
			shift
				debug_print "set parameter ap_nss_mcs_cap=$1"
		;;
		FILSCAP)
			# do nothing
			shift
		;;
		BAWINSIZE)
			shift
				debug_print "set parameter oce_ba_win_size=$1"
		;;
		DATAFORMAT)
			shift
				debug_print "set parameter oce_data_format=$1"
		;;
		ESP_IE)
			shift
				debug_print "set parameter oce_esp_ie=$1"
		;;
		AMSDU)
			# param not supported
			shift
			debug_print "set parameter ap_amsdu=$1"
		;;
		MCS_FIXEDRATE)
			shift
				debug_print "set parameter ap_mcs_fixedrate=$1"
		;;
		SPATIAL_RX_STREAM)
			# do nothing
			shift
		;;
		SPATIAL_TX_STREAM)
			shift
				debug_print "set parameter ap_spatial_tx_stream=$1"
		;;
		BCC)
			shift
				debug_print "set parameter ap_bcc=$1"
		;;
		LDPC)
			shift
				debug_print "set parameter ap_ldpc=$1"
		;;
		NOACK)
			# param not supported
			shift
				debug_print "set parameter ap_no_ack=$1"
		;;
		OFDMA)
			# param not supported
			shift
		;;
		PPDUTXTYPE)
			# param not supported
			shift
		;;
		SPECTRUMMGT)
			# param not supported
			shift
		;;
		*)
			error_print "while loop error $1"
			send_invalid ",errorCode,2"
			return
		;;
		esac
		shift
	done

	# Mode / Band selection
	if [ "$ap_mode" != "" ]; then
		case "$ap_mode" in
		11a)
			debug_print "set parameter ap_mode_val=a"
			debug_print "set parameter ap_band_val=5GHz"
		;;
		11b)
			debug_print "set parameter ap_mode_val=b"
			debug_print "set parameter ap_band_val=2.4GHz"
		;;
		11g)
			debug_print "set parameter ap_mode_val=g"
			debug_print "set parameter ap_band_val=2.4GHz"
		;;
		11bg)
			debug_print "set parameter ap_mode_val=b,g"
			debug_print "set parameter ap_band_val=2.4GHz"
		;;
		11bgn)
			debug_print "set parameter ap_mode_val=b,g,n"
			debug_print "set parameter ap_band_val=2.4GHz"
		;;
		11ng)
			debug_print "set parameter ap_mode_val=b,g,n"
			debug_print "set parameter ap_band_val=2.4GHz"
		;;
		11na)
			debug_print "set parameter ap_mode_val=a,n"
			debug_print "set parameter ap_band_val=5GHz"
		;;
		11an)
			debug_print "set parameter ap_mode_val=a,n"
			debug_print "set parameter ap_band_val=5GHz"
		;;
		11ac)
			debug_print "set parameter ap_mode_val=a,n,ac"
			debug_print "set parameter ap_band_val=5GHz"
		;;
		11anac)
			debug_print "set parameter ap_mode_val=a,n,ac"
			debug_print "set parameter ap_band_val=5GHz"
		;;
		11ax)
			debug_print "set parameter ap_mode_val=ax"
			# The band value can be 2.4GHz or 5GHz, and is determined below, considering the channel value.
			# The mode value combination is determined below, considering the channel value.
		;;
		*)
			error_print "Unsupported value - ap_mode:$ap_mode"
			send_error ",errorCode,3"
			return
		;;
		esac
	fi

	debug_print "ap_set_wireless():end"
}

ap_set_11n_wireless()
{
	ap_name="INTEL"
	# default to wlan0 in case no interface parameter is given

	IFS=" "
	send_running
	# checking for interface parameter (might fail if some value is also "interface")
	ap_radio_interface=`get_radio_interface_name $@`
	ap_interface=`get_interface_name $@`
	stop_wlan
	
	#get_wlan_param_radio_all "ap_channel" "ap_auto_channel" "ap_mode_val" "ap_band_val" "ap_radio" "ap_offset_val" "ap_mcs_fixedrate" "ap_sgi20_val" "ap_width_val" "ap_region"
	# not supported by clish
	#ap_bcnint=100
	#ap_rts=2347
	#ap_frgmnt=2346
	# not supported by wave
	#ap_ampdu_val
	#ap_ampdu_exp_val
	#ap_amsdu_val
	#get_wlan_param "ssid" "SSID" "ap_ssid"
	#debug_print "***read from clish"
	#debug_print "ap_ssid:$ap_ssid"
	#debug_print "ap_channel:$ap_channel"
	#debug_print "ap_auto_channel:$ap_auto_channel"
	#debug_print "ap_mode_val:$ap_mode_val"
	#debug_print "ap_band_val:$ap_band_val"
	#debug_print "ap_rts:$ap_rts"
	#debug_print "ap_frgmnt:$ap_frgmnt"
	#debug_print "ap_bcnint:$ap_bcnint"
	#debug_print "ap_radio:$ap_radio"
	#debug_print "ap_offset_val:$ap_offset_val"
	#debug_print "ap_mcs_fixedrate:$ap_mcs_fixedrate"
	#debug_print "ap_sgi20_val:$ap_sgi20_val"
	#debug_print "ap_width_val:$ap_width_val"
	#debug_print "ap_region:$ap_region"

	while [ "$1" != "" ]; do
		# for upper case only
		upper "$1" token
		debug_print "while loop $1 - token:$token"
		case "$token" in
			NAME)
				shift
				debug_print "set parameter 	ap_name=$1"
			;;
			INTERFACE)
				# skip since it was read in loop before
				shift
			;;
			WLAN_TAG)
				# skip as it is determined in get_interface_name
				shift
			;;
			SSID)
				shift
				debug_print "set parameter 	ap_ssid=$1"
			;;
			CHANNEL)
				shift
				debug_print "set parameter 	ap_channel=$1"
				ap_auto_channel=0
			;;
			MODE)
				shift
				debug_print "set parameter ap_mode=$1"
			;;
			WME)
				shift
				debug_print "set parameter ap_wme=$1"
			;;
			WMMPS)
				shift
				debug_print "set parameter ap_wmmps=$1"
			;;
			RTS)
				shift
				debug_print "set parameter 	ap_rts=$1"
			;;
			FRGMNT)
				shift
				debug_print "set parameter 	ap_frgmnt=$1"
			;;
			PWRSAVE)
				# param 11n not supported
				shift
				debug_print "set parameter 	ap_pwrsave=$1"
			;;
			BCNINT)
				shift
				debug_print "set parameter 	ap_bcnint=$1"
			;;
			RADIO)
				shift
				debug_print "set parameter ap_radio=$1"
			;;
			40_INTOLERANT)
				# param 11n not supported
				shift
				debug_print "set parameter 	ap_40intolerant=$1"
			;;
			ADDBA_REJECT)
				# param 11n not supported
				shift
				debug_print "set parameter 	ap_addba_reject=$1"
			;;
			AMPDU)
				shift
				debug_print "set parameter ap_ampdu=$1"
			;;
			AMPDU_EXP)
				shift
				debug_print "set parameter 	ap_ampdu_exp=$1"
			;;
			AMSDU)
				shift
				debug_print "set parameter ap_amsdu=$1"
			;;
			GREENFIELD)
				# param 11n not supported
				shift
				debug_print "set parameter 	ap_greenfield=$1"
			;;
			OFFSET)
				shift
				debug_print "set parameter 	ap_offset=$1"
			;;
			MCS_32)
				# param 11n not supported
				shift
				debug_print "set parameter 	ap_mcs_32=$1"
			;;
			MCS_FIXEDRATE)
				shift
				debug_print "set parameter 	ap_mcs_fixedrate=$1"
			;;
			SPATIAL_RX_STREAM)
				# do nothing
				shift
			;;
			SPATIAL_TX_STREAM)
				# do nothing
				shift
			;;
			MPDU_MIN_START_SPACING)
				# param 11n not supported
				shift
				debug_print "set parameter 	ap_mpdu_min_start_spacing=$1"
			;;
			RIFS_TEST)
				# param 11n not supported
				shift
				debug_print "set parameter 	ap_rifs_test=$1"
			;;
			SGI20)
				shift
				debug_print "set parameter ap_sgi20=$1"
			;;
			STBC_TX)
				# param 11n not supported
				shift
				debug_print "set parameter 	ap_stbc_tx=$1"
			;;
			WIDTH)
				shift
				debug_print "set parameter ap_width=$1"
			;;
			WIDTH_SCAN)
				# param 11n not supported
				shift
				debug_print "set parameter 	ap_width_scan=$1"
			;;
			COUNTRY) ## NB: Extension parameter
				shift
				debug_print "set parameter 	ap_region=$1"
			;;
			COUNTRYCODE) ## NB: Extension parameter
				shift
				debug_print "set parameter 	ap_region=$1"
			;;
			*)
				error_print "while loop error $1"
				send_invalid ",errorCode,7"
				return
			;;
		esac
		shift
	done
}

ap_set_security()
{
	ap_name="INTEL"
	# default to wlan0 in case no interface parameter is given

	IFS=" "
	send_running
	# checking for interface parameter (might fail if some value is also "interface")
	ap_radio_interface=`get_radio_interface_name $@`
	ap_interface=`get_interface_name $@`

	stop_wlan

	# TODO: Do you really want to get all params? only write changed
	#get_wlan_param_ap_security_all "ap_ssid" "ap_ModeEnabled" "ap_wepkey" "ap_psk" "ap_wps_ena"
	#debug_print "***read from clish"
	#debug_print "ap_ssid:$ap_ssid"
	#debug_print "ap_ModeEnabled:$ap_ModeEnabled"
	#debug_print "ap_wepkey:$ap_wepkey"
	#debug_print "ap_psk:$ap_psk"
	#debug_print "ap_wps_ena:$ap_wps_ena"

	while [ "$1" != "" ]; do
		# for upper case only
		upper "$1" token
		debug_print "while loop $1 - token:$token"
		case "$token" in
			NAME)
				shift
				debug_print "set parameter 	ap_name=$1"
			;;
			INTERFACE)
				# skip since it was read in loop before
				shift
			;;
			WLAN_TAG)
				# skip as it is determined in get_interface_name
				shift
			;;
			KEYMGNT)
				shift
				debug_print "set parameter ap_keymgnt=$1"
			;;
			ENCRYPT)
				shift
				debug_print "set parameter ap_encrypt=$1"
				# ENCRYPT parameter is ignored
			;;
			PSK)
				shift
				debug_print "set parameter 	ap_psk=$1"
				configurePassphrase=1
			;;
			WEPKEY)
				shift
				debug_print "set parameter 	ap_wepkey=$1"
				configureWEP=1
				case "${#ap_wepkey}" in
					5)
						ap_wepEncrLvl=0
						ap_wepKeyType=0
					;;
					10)
						ap_wepEncrLvl=0
						ap_wepKeyType=1
					;;
					13)
						ap_wepEncrLvl=1
						ap_wepKeyType=0
					;;
					26)
						ap_wepEncrLvl=1
						ap_wepKeyType=1
					;;
					*)
						debug_print "Unsupported value - ap_wepkey:$ap_wepkey"
						send_invalid ",errorCode,17"
						return
					;;
				esac
			;;
			SSID)
				shift
				debug_print "set parameter 	ap_ssid=$1"
			;;
			PMF)
				shift
				debug_print "set parameter ap_pmf=$1"
			;;
			SHA256AD)
				# param not supported
				shift
				debug_print "set parameter ap_sha256ad=$1"
			;;
			AKMSUITETYPE)
				shift
				debug_print "set parameter 	ap_akm_suite_type=$1"
			;;
			PMKSACACHING)
				shift
				debug_print "set parameter 	ap_pmks_a_caching=$1"
			;;
			*)
				debug_print "while loop error $1"
				send_invalid ",errorCode,18"
				return
			;;
		esac
		shift
	done
}

start_wps_registration()
{
	ap_name="INTEL"
	# default to wlan0 in case no interface parameter is given

	IFS=" "
	send_running
	# checking for interface parameter (might fail if some value is also "interface")
	ap_radio_interface=`get_radio_interface_name $@`
	ap_interface=`get_interface_name $@`

	while [ "$1" != "" ]; do
		# for upper case only
		upper "$1" token
		debug_print "while loop $1 - token:$token"
		case "$token" in
			NAME)
				shift
				debug_print "set parameter 	ap_name=$1"
			;;
			INTERFACE)
				# skip since it was read in loop before
				shift
			;;
			WPSROLE)
				shift
				debug_print "set parameter 	ap_wps_role=$1"
				# no param for this in our DB.
				# we are already configured correctly by default no action here.
				# By default AP is Registrar and STA is Enrollee
			;;
			WPSCONFIGMETHOD)
				shift
				debug_print "set parameter 	ap_wps_config_method=$1"
				# ConfigMethodsEnabled
				# we are already configured correctly by default no action here.
			;;
			*)
				error_print "while loop error $1"
				send_invalid ",errorCode,20"
				return
			;;
		esac
		shift
	done
}

# Push the WPS button on AP
ap_set_wps_pbc()
{
	ap_name="INTEL"
	# default to wlan0 in case no interface parameter is given

	IFS=" "
	send_running
	# checking for interface parameter (might fail if some value is also "interface")
	ap_radio_interface=`get_radio_interface_name $@`
	ap_interface=`get_interface_name $@`

	while [ "$1" != "" ]; do
		# for upper case only
		upper "$1" token
		debug_print "while loop $1 - token:$token"
		case "$token" in
			NAME)
				shift
				debug_print "set parameter 	ap_name=$1"
			;;
			INTERFACE)
				# skip since it was read in loop before
				shift
			;;
			*)
				error_print "while loop error $1"
				send_invalid ",errorCode,21"
				return
			;;
		esac
		shift
	done
	send_complete
}

# TODO: Can we recycle the ap_set_security API?
ap_set_pmf()
{
	ap_name="INTEL"
	# default to wlan0 in case no interface parameter is given

	IFS=" "
	send_running
	# checking for interface parameter (might fail if some value is also "interface")
	ap_radio_interface=`get_radio_interface_name $@`
	ap_interface=`get_interface_name $@`

	#get_wlan_param "ap security" "pmfEnable" "ap_pmf"

	debug_print "ap_pmf:$ap_pmf"

	while [ "$1" != "" ]; do
		# for upper case only
		upper "$1" token
		debug_print "while loop $1 - token:$token"
		case "$token" in
			NAME)
				shift
				debug_print "set parameter 	ap_name=$1"
			;;
			INTERFACE)
				# skip since it was read in loop before
				shift
			;;
			PMF)
				shift
				debug_print "set parameter ap_pmf_ena=$1"
			;;
			*)
				error_print "while loop error $1"
				send_invalid ",errorCode,22"
				return
			;;
		esac
		shift
	done

}

ap_set_apqos()
{
	ap_name="INTEL"
	# default to wlan0 in case no interface parameter is given

	IFS=" "
	send_running
	# checking for interface parameter (might fail if some value is also "interface")
	ap_radio_interface=`get_radio_interface_name $@`
	ap_interface=`get_interface_name $@`

	stop_wlan

	# ac types: vo=3 vi=2 bk=1 be=0
	#get_wlan_param_qos_all "ap_cwmin_vo" "ap_cwmax_vo" "ap_aifs_vo" "ap_txop_vo" "ap_acm_val_vo" "ap_cwmin_vi" "ap_cwmax_vi" "ap_aifs_vi" "ap_txop_vi" "ap_acm_val_vi" "ap_cwmin_bk" "ap_cwmax_bk" "ap_aifs_bk" "ap_txop_bk" "ap_acm_val_bk" "ap_cwmin_be" "ap_cwmax_be" "ap_aifs_be" "ap_txop_be" "ap_acm_val_be"
	#debug_print "***read from clish"
	#debug_print "ap_cwmin_val_vo:$ap_cwmin_vo"
	#debug_print "ap_cwmax_val_vo:$ap_cwmax_vo"
	#debug_print "ap_aifs_val_vo:$ap_aifs_vo"
	#debug_print "ap_txop_val_vo:$ap_txop_vo"
	#debug_print "ap_acm_val_vo:$ap_acm_val_vo"

	#debug_print "ap_cwmin_val_vi:$ap_cwmin_vi"
	#debug_print "ap_cwmax_val_vi:$ap_cwmax_vi"
	#debug_print "ap_aifs_val_vi:$ap_aifs_vi"
	#debug_print "ap_txop_val_vi:$ap_txop_vi"
	#debug_print "ap_acm_val_vi:$ap_acm_val_vi"

	#debug_print "ap_cwmin_val_bk:$ap_cwmin_bk"
	#debug_print "ap_cwmax_val_bk:$ap_cwmax_bk"
	#debug_print "ap_aifs_val_bk:$ap_aifs_bk"
	#debug_print "ap_txop_val_bk:$ap_txop_bk"
	#debug_print "ap_acm_val_bk:$ap_acm_val_bk"

	#debug_print "ap_cwmin_val_be:$ap_cwmin_be"
	#debug_print "ap_cwmax_val_be:$ap_cwmax_be"
	#debug_print "ap_aifs_val_be:$ap_aifs_be"
	#debug_print "ap_txop_val_be:$ap_txop_be"
	#debug_print "ap_acm_val_be:$ap_acm_val_be"

	while [ "$1" != "" ]; do
		# for upper case only
		upper "$1" token
		debug_print "while loop $1 - token:$token"
		case "$token" in
			NAME)
				shift
				debug_print "set parameter 	ap_name=$1"
			;;
			INTERFACE)
				# skip as it is determined in get_interface_name
				shift
			;;
			CWMIN*)
				lower "${1#CWMIN_*}" ap_actype
				shift
				debug_print "set parameter ap_cwmin=$1"
				lower "$ap_cwmin" ap_cwmin_${ap_actype}
				debug_print "ap_cwmin:$ap_cwmin"
				debug_print "ap_actype:$ap_actype"
				debug_print "ap_param:$ap_param"
				debug_print "ap_cwmin_vo:$ap_cwmin_vo"
				debug_print "ap_cwmin_vi:$ap_cwmin_vi"
				debug_print "ap_cwmin_bk:$ap_cwmin_bk"
				debug_print "ap_cwmin_be:$ap_cwmin_be"
			;;
			CWMAX*)
				lower "${1#CWMAX_*}" ap_actype
				shift
				debug_print "set parameter ap_cwmax=$1"
				lower "$ap_cwmax" ap_cwmax_${ap_actype}
			;;
			AIFS*)
				lower "${1#AIFS_*}" ap_actype
				shift
				debug_print "set parameter ap_aifs=$1"
				lower "$ap_aifs" ap_aifs_${ap_actype}
			;;
			TXOP*)
				lower "${1#TXOP_*}" ap_actype
				shift
				debug_print "set parameter ap_txop=$1"
				lower "$ap_txop" ap_txop_${ap_actype}
			;;
			ACM*)
				lower "${1#ACM_*}" ap_actype
				shift
				debug_print "set parameter ap_acm=$1"
				lower "$ap_acm" ap_acm_${ap_actype}
			;;
			*)
				error_print "while loop error $1"
				send_invalid ",errorCode,31"
				return
			;;
		esac
		shift
	done
}

ap_set_staqos()
{
	ap_name="INTEL"
	# default to wlan0 in case no interface parameter is given

	IFS=" "
	send_running
	# checking for interface parameter (might fail if some value is also "interface")
	ap_radio_interface=`get_radio_interface_name $@`
	ap_interface=`get_interface_name $@`

	stop_wlan

	# ac types: vo=3 vi=2 bk=1 be=0

	#debug_print "reading acs from rc.conf"
	#get_rc_conf wlmn_${ap_vap_index}_cpeId "ap_main_cpeId"

	#get_rc_conf wlswmm${ap_main_cpeId}_3_ECWmin "ap_cwmin_vo"
	#get_rc_conf wlswmm${ap_main_cpeId}_3_ECWmax "ap_cwmax_vo"
	#get_rc_conf wlswmm${ap_main_cpeId}_3_AIFSN "ap_aifs_vo"
	#get_rc_conf wlswmm${ap_main_cpeId}_3_TXOP "ap_txop_vo"

	#get_rc_conf wlswmm${ap_main_cpeId}_2_ECWmin "ap_cwmin_vi"
	#get_rc_conf wlswmm${ap_main_cpeId}_2_ECWmax "ap_cwmax_vi"
	#get_rc_conf wlswmm${ap_main_cpeId}_2_AIFSN "ap_aifs_vi"
	#get_rc_conf wlswmm${ap_main_cpeId}_2_TXOP "ap_txop_vi"

	#get_rc_conf wlswmm${ap_main_cpeId}_1_ECWmin "ap_cwmin_bk"
	#get_rc_conf wlswmm${ap_main_cpeId}_1_ECWmax "ap_cwmax_bk"
	#get_rc_conf wlswmm${ap_main_cpeId}_1_AIFSN "ap_aifs_bk"
	#get_rc_conf wlswmm${ap_main_cpeId}_1_TXOP "ap_txop_bk"

	#get_rc_conf wlswmm${ap_main_cpeId}_0_ECWmin "ap_cwmin_be"
	#get_rc_conf wlswmm${ap_main_cpeId}_0_ECWmax "ap_cwmax_be"
	#get_rc_conf wlswmm${ap_main_cpeId}_0_AIFSN "ap_aifs_be"
	#get_rc_conf wlswmm${ap_main_cpeId}_0_TXOP "ap_txop_be"

	#debug_print "read from rc.conf ap_cwmin_val_vo:$ap_cwmin_vo"
	#debug_print "read from rc.conf ap_cwmax_val_vo:$ap_cwmax_vo"
	#debug_print "read from rc.conf ap_aifs_val_vo:$ap_aifs_vo"
	#debug_print "read from rc.conf ap_txop_val_vo:$ap_txop_vo"

	#debug_print "read from rc.conf ap_cwmin_val_vi:$ap_cwmin_vi"
	#debug_print "read from rc.conf ap_cwmax_val_vi:$ap_cwmax_vi"
	#debug_print "read from rc.conf ap_aifs_val_vi:$ap_aifs_vi"
	#debug_print "read from rc.conf ap_txop_val_vi:$ap_txop_vi"

	#debug_print "read from rc.conf ap_cwmin_val_bk:$ap_cwmin_bk"
	#debug_print "read from rc.conf ap_cwmax_val_bk:$ap_cwmax_bk"
	#debug_print "read from rc.conf ap_aifs_val_bk:$ap_aifs_bk"
	#debug_print "read from rc.conf ap_txop_val_bk:$ap_txop_bk"

	#debug_print "read from rc.conf ap_cwmin_val_be:$ap_cwmin_be"
	#debug_print "read from rc.conf ap_cwmax_val_be:$ap_cwmax_be"
	#debug_print "read from rc.conf ap_aifs_val_be:$ap_aifs_be"
	#debug_print "read from rc.conf ap_txop_val_be:$ap_txop_be"

	while [ "$1" != "" ]; do
		# for upper case only
		upper "$1" token
		debug_print "while loop $1 - token:$token"
		case "$token" in
			NAME)
				shift
				debug_print "set parameter 	ap_name=$1"
			;;
			INTERFACE)
				# skip since it was read in loop before
				shift
			;;
			CWMIN*)
				lower "${1#CWMIN_*}" ap_actype
				shift
				debug_print "set parameter ap_cwmin=$1"
				lower "$ap_cwmin" ap_cwmin_${ap_actype}
				debug_print "ap_cwmin:$ap_cwmin"
				debug_print "ap_actype:$ap_actype"
				debug_print "ap_param:$ap_param"
				debug_print "ap_cwmin_vo:$ap_cwmin_vo"
				debug_print "ap_cwmin_vi:$ap_cwmin_vi"
				debug_print "ap_cwmin_bk:$ap_cwmin_bk"
				debug_print "ap_cwmin_be:$ap_cwmin_be"
			;;
			CWMAX*)
				lower "${1#CWMAX_*}" ap_actype
				shift
				debug_print "set parameter ap_cwmax=$1"
				lower "$ap_cwmax" ap_cwmax_${ap_actype}
			;;
			AIFS*)
				lower "${1#AIFS_*}" ap_actype
				shift
				debug_print "set parameter ap_aifs=$1"
				lower "$ap_aifs" ap_aifs_${ap_actype}
			;;
			TXOP*)
				lower "${1#TXOP_*}" ap_actype
				shift
				debug_print "set parameter ap_txop=$1"
				lower "$ap_txop" ap_txop_${ap_actype}
			;;
			ACM*)
				# not supported for stations by mapi
				lower "${1#ACM_*}" ap_actype
				shift
				debug_print "set parameter ap_acm=$1"
				lower "$ap_acm" ap_acm_${ap_actype}
			;;
			*)
				error_print "while loop error $1"
				send_invalid ",errorCode,37"
				return
			;;
		esac
		shift
	done
	# TODO: 1. Add STA QoS support 2. Use new SM to write efficiently

	send_complete
}

ap_set_radius()
{
	ap_name="INTEL"
	# default to wlan0 in case no interface parameter is given
	IFS=" "
	# checking for interface parameter (might fail if some value is also "interface")
	send_running
	ap_radio_interface=`get_radio_interface_name $@`
	ap_interface=`get_interface_name $@`

	while [ "$1" != "" ]; do
		# for upper case only
		upper "$1" token
		debug_print "while loop $1 - token:$token"
		case "$token" in
			NAME)
				shift
				debug_print "set parameter 	ap_name=$1"
			;;
			INTERFACE)
				# skip since it was read in loop before
				shift
			;;
			IPADDR)
				shift
				debug_print "set parameter 	ap_ipaddr=$1"
			;;
			PORT)
				shift
				debug_print "set parameter 	ap_port=$1"
			;;
			PASSWORD)
				shift
				debug_print "set parameter 	ap_password=$1"
			;;
			*)
				error_print "while loop error $1"
				send_invalid ",errorCode,38"
				return
			;;
		esac
		shift
	done
	send_complete
}

# TODO: 1. Add HS2 support 2. Use new SM to write efficiently, or rewrite using fapi_cli
ap_set_hs2()
{
	ap_name="INTEL"
	send_running

	while [ "$1" != "" ]; do
		# for upper case only
		upper "$1" token
		debug_print "while loop $1 - token:$token"
		shift
		case "$token" in
			NAME)
				debug_print "set parameter 	ap_name=$1"
			;;
				INTERWORKING)
				debug_print "set parameter hs20Mode=$1"
			;;
			ACCS_NET_TYPE)
				debug_print "set parameter accessNetType=$1"
			;;
			INTERNET)
				debug_print "set parameter internetConnectivity=$1"
			;;
			VENUE_GRP)
				debug_print "set parameter venueGroup=$1"
			;;
			VENUE_TYPE)
				debug_print "set parameter venueType=$1"
			;;
			HESSID)
				debug_print "set parameter hessid=$1"
			;;
			ROAMING_CONS)
				debug_print "set parameter roamConsort=$1"
			;;
			DGAF_DISABLE)
				debug_print "set parameter dgafDisabled=$1"
			;;
			ANQP)
				# param not supported
			;;
			NET_AUTH_TYPE)
				debug_print "set parameter netAuthType=$1"
				debug_print "set parameter netAuthUrl=https://tandc-server.wi-fi.org"
			;;
			NAI_REALM_LIST)
				debug_print "set parameter addNaiRealmName"
			;;
			DOMAIN_LIST)
				debug_print "set parameter addDomainName"
			;;
			OPER_NAME)
				debug_print "set parameter addOpFriendlyName"
			;;
			VENUE_NAME)
				debug_print "set parameter addVenueName"
			;;
			GAS_CB_DELAY)
				debug_print "set parameter gasComebackDelay=$1"
			;;
			MIH)
				# param not supported
			;;
			L2_TRAFFIC_INSPECT)
				# not defined in Oliver's doc
			;;
			BCST_UNCST)
				# param not supported
			;;
			PLMN_MCC)
				PLMN_MCC_VAL="$1"
			;;
			PLMN_MNC)
				debug_print "set parameter threeGpp"
			;;
			PROXY_ARP)
				debug_print "set parameter proxyArp=$1"
			;;
			WAN_METRICS)
				debug_print "set parameter wanMetric"
			;;
			CONN_CAP)
				case "$1" in
					1)
						debug_print "set parameter addConnectionCap 6:20:1"
						debug_print "set parameter addConnectionCap 6:80:1"
						debug_print "set parameter addConnectionCap 6:443:1"
						debug_print "set parameter addConnectionCap 17:244:1"
						debug_print "set parameter addConnectionCap 17:4500:1"
					;;
				esac
			;;
			IP_ADD_TYPE_AVAIL)
				
				case "$1" in
					1)
						debug_print "set parameter ipv4AddrType"
					;;
				esac
			;;
			ICMPv4_ECHO)
				wlancli -c set_wlan_hs_l2_firewall_list -P addAction "$1" addProtocol 1
			;;
			OPER_CLASS)
				case "$1" in
					1)
						debug_print "set parameter operatingClass=51"
					;;
					2)
						debug_print "set parameter operatingClass=73"
					;;
					3)
						debug_print "set parameter operatingClass=5173"
					;;
				esac
			;;
			*)
				error_print "while loop error $1"
				send_invalid ",errorCode,39"
				return
			;;
		esac
		shift
	done
	#wlancli -c set_wlan_apply - not needed because the sigma commit function doing rc.bringup stop/start
	send_complete
}

ap_set_rfeature()
{
	ap_name="INTEL"
	# default to wlan0 in case no interface parameter is given

	IFS=" "
	send_running
	# checking for interface parameter (might fail if some value is also "interface")
	ap_radio_interface=`get_radio_interface_name $@`
	ap_interface=`get_interface_name $@`

	#get_wlan_param_radio_all "ap_nss_mcs_opt"
	#debug_print "***read from clish"
	#debug_print "ifn ap_nss_mcs_opt:$ap_nss_mcs_opt"
	#debug_print "ifn ap_nss_mcs_opt:$ap_nss_mcs_opt"
	#debug_print "ifn ap_nss_mcs_opt:"$ap_nss_mcs_opt"

	while [ "$1" != "" ]; do
		# for upper case only
		upper "$1" token
		debug_print "while loop $1 - token:$token"
		case "$token" in
			NAME)
				shift
				debug_print "set parameter 	ap_name=$1"
			;;
			INTERFACE)
				# skip as it is determined in get_interface_name
				shift
			;;
			WLAN_TAG)
				# skip as it is determined in get_interface_name
				shift
			;;
			TYPE)
				shift
				debug_print "set parameter 	ap_type=$1"
			;;
			BSS_TRANSITION)
				# do nothing
				shift
			;;
			NSS_MCS_OPT)
				shift
				debug_print "set parameter 	ap_nss_mcs_opt=$1"
			;;
			OPT_MD_NOTIF_IE)
				shift
				debug_print "set parameter 	ap_opt_md_notif_ie=$1"
			;;
			CHNUM_BAND)
				shift
				debug_print "set parameter 	ap_chnum_band=$1"
			;;
			BTM_DISASSOCIMNT)
				shift
				debug_print "set parameter 	ap_btmreq_disassoc_imnt=$1"
			;;
			BTMREQ_DISASSOC_IMNT)
				shift
				debug_print "set parameter 	ap_btmreq_disassoc_imnt=$1"
			;;
			BTMREQ_TERM_BIT)
				shift
				debug_print "set parameter 	ap_btmreq_term_bit=$1"
			;;
			BTM_BSSTERM)
				shift
				debug_print "set parameter 	ap_btm_bssterm=$1"
			;;
			BSS_TERM_DURATION)
				shift
				debug_print "set parameter 	ap_btm_bssterm=$1"
			;;
			ASSOC_DISALLOW)
				shift
				debug_print "set parameter 	ap_assoc_disallow=$1"
			;;
			DISASSOC_TIMER)
				shift
				debug_print "set parameter 	ap_disassoc_timer=$1"
			;;
			ASSOC_DELAY)
				shift
				debug_print "set parameter 	ap_assoc_delay=$1"
			;;
			NEBOR_BSSID)
				shift
				debug_print "set parameter 	ap_nebor_bssid=$1"
			;;
			NEBOR_OP_CLASS)
				shift
				debug_print "set parameter 	ap_nebor_op_class=$1"
			;;
			NEBOR_OP_CH)
				shift
				debug_print "set parameter 	ap_nebor_op_ch=$1"
			;;
			NEBOR_PREF)
				shift
				debug_print "set parameter 	ap_nebor_priority=$1"
			;;
			BSS_TERM_TSF)
				shift
				debug_print "set parameter 	ap_bssTermTSF=$1"
			;;
			PROGRAM)
				shift
				debug_print "set parameter 	ap_program=$1"
			;;
			DOWNLINKAVAILCAP)
				shift
				debug_print "set parameter 	ap_down_link_avail_cap=$1"
			;;
			UPLINKAVAILCAP)
				shift
				debug_print "set parameter 	ap_up_link_avail_cap=$1"
			;;
			RSSITHRESHOLD)
				shift
				debug_print "set parameter 	ap_rssi_threshold=$1"
			;;
			RETRYDELAY)
				shift
				debug_print "set parameter 	ap_retry_delay=$1"
			;;
			TXPOWER)
				shift
				debug_print "set parameter 	ap_tx_power=$1"
			;;
			TXBANDWIDTH)
				shift
				debug_print "set parameter 	ap_txbandwidth=$1"
			;;
			LTF)
				# param not supported
				shift
				debug_print "set parameter 	ap_ltf=$1"
			;;
			GI)
				# param not supported
				shift
				debug_print "set parameter 	ap_gi=$1"
			;;
			RUAllocTones)
				# param not supported
				shift
				debug_print "set parameter 	ap_rualloctones=$1"
			;;
			*)
				error_print "while loop error $1"
				send_invalid ",errorCode,40"
				return
			;;
		esac
		shift
	done

	send_complete
}

dev_send_frame()
{
	ap_name="INTEL"
	# default to wlan0 in case no interface parameter is given

	IFS=" "
	send_running
	# checking for interface parameter (might fail if some value is also "interface")
	ap_radio_interface=`get_radio_interface_name $@`
	ap_interface=`get_interface_name $@`

	#get_wlan_param_radio_all "ap_nss_mcs_opt"
	#debug_print "ifn ap_nss_mcs_opt:$ap_nss_mcs_opt"

	while [ "$1" != "" ]; do
		# for upper case only
		upper "$1" token
		debug_print "while loop $1 - token:$token"
		case "$token" in
			NAME)
				shift
				debug_print "set parameter 	ap_name=$1"
			;;
			INTERFACE)
				# skip as it is determined in get_interface_name
				shift
			;;
			WLAN_TAG)
				# skip as it is determined in get_interface_name
				shift
			;;
			DEST_MAC)
				shift
				debug_print "set parameter 	ap_dest_mac=$1"
			;;
			PROGRAM)
				# do nothing
				shift
			;;
			FRAMENAME)
				shift
				debug_print "set parameter 	ap_frame_name=$1"
			;;
			CAND_LIST)
				shift
				debug_print "set parameter 	ap_cand_list=$1"
			;;
			BTMQUERY_REASON_CODE)
				shift
				debug_print "set parameter 	ap_btmquery_reason_code=$1"
			;;
			DISASSOC_TIMER)
				shift
				debug_print "set parameter 	ap_disassoc_timer=$1"
			;;
			MEAMODE)
				shift
				debug_print "set parameter 	ap_meamode=$1"
			;;
			REGCLASS)
				shift
				debug_print "set parameter 	ap_regclass=$1"
			;;
			CHANNEL)
				shift
				debug_print "set parameter 	ap_channel=$1"
			;;
			RANDINT)
				shift
				debug_print "set parameter 	ap_randint=$1"
			;;
			MEADUR)
				shift
				debug_print "set parameter 	ap_meadur=$1"
			;;
			SSID)
				shift
				debug_print "set parameter 	ap_ssid=$1"
			;;
			RPTCOND)
				shift
				debug_print "set parameter 	ap_rptcond=$1"
			;;
			RPTDET)
				shift
				debug_print "set parameter 	ap_rpt_det=$1"
			;;
			MEADURMAND)
				shift
				debug_print "set parameter 	ap_meadurmand=$1"
			;;
			APCHANRPT)
				shift
				debug_print "set parameter 	ap_apchanrpt=$1"
			;;
			REQINFO)
				shift
				debug_print "set parameter 	ap_reqinfo=$1"
			;;
			REQUEST_MODE)
				# do nothing
				shift
			;;
			BSSID)
				shift
				debug_print "set parameter 	ap_bssid=$1"
			;;
			*)
				error_print "while loop error $1"
				send_invalid ",errorCode,42"
				return
			;;
		esac
		shift
	done
	send_complete
}

ap_reboot()
{
	# ap_config_commit,NAME,name_of_AP
	# NAME parameter is ignored here

	send_running
	reboot
	send_complete
}

ap_config_commit()
{
	# uci commit ?
	send_complete
}

ap_reset_default()
{
	#echo -e "ap_reset_default:$ap_program:start\n" > /dev/console ^^Gal
	info_print "ap_reset_default():start"
}

ap_get_info()
{
	# ap_get_info,NAME,name_of_AP
	# NAME parameter is ignored here

	send_running
	#wd_list=`iwconfig 2>/dev/null | grep ^[a-z] | cut -f0 -d" "| xargs echo `
	
	#answer=need to see what info need to be returned
	
	send_complete "$answer"
}

ap_deauth_sta()
{
	ap_name="INTEL"
	# default to wlan0 in case no interface parameter is given

	IFS=" "
	send_running
	# checking for interface parameter (might fail if some value is also "interface")
	ap_radio_interface=`get_radio_interface_name $@`
	ap_interface=`get_interface_name $@`

	while [ "$1" != "" ]; do
		# for upper case only
		upper "$1" token
		debug_print "while loop $1 - token:$token"
		case "$token" in
			NAME)
				shift
				debug_print "deauthenticate 	ap_name=$1"
			;;
			INTERFACE)
				# skip since it was read in loop before
				shift
			;;
			STA_MAC_ADDRESS)
				shift
				debug_print "deauthenticate ap_sta_mac=$1"
			;;
			MINORCODE)
				shift
				debug_print "deauthenticate ap_minor_code=$1"
			;;
			*)
				error_print "while loop error $1"
				send_invalid ",errorCode,44"
				return
			;;
		esac
		shift
	done

	send_complete
}

ap_get_mac_address()
{
	ap_name="INTEL"

	IFS=" "
	send_running
	# checking for interface parameter (might fail if some value is also "interface")
	ap_radio_interface=`get_radio_interface_name $@`
	ap_interface=`get_interface_name $@`
	
	wdmac=ifconfig $ap_interface | grep HWaddr | awk '{print $5;}'

	send_complete ",mac,$wdmac"
}

device_get_info()
{
	send_running
	# configure following values: vendor, model name, FW version
	send_complete ",vendor,$VENDOR,model,$MODEL,version,$FW_VERSION"
}

##### Parser #####

parse_command()
{
	#echo parsing,`eval $TIMESTAMP`
	lower "$1" cmd
	shift

	debug_print "running command: >>>$cmd<<<"
	$cmd $*
	local res=$?
	debug_print "result: $res"
	if [ $res != "0" ]; then
		send_invalid ",errorCode,46"
		error_print "Unknown command: >>>$cmd<<<"
		error_print "Supported commands:"
		error_print "ap_ca_version, ca_get_version, ap_set_wireless, ap_set_11n_wireless, ap_set_security"
		error_print "ap_set_pmf, ap_set_statqos, ap_set_radius, ap_set_hs2, ap_reboot, ap_config_commit,"
		error_print "ap_reset_default, ap_get_info, ap_deauth_sta, ap_get_mac_address, ap_set_rfeature"
		error_print "dev_send_frame, device_get_info"
	fi
	cmd=""
	return
}

if [ -e "sigma-ext.sh" ]; then
	source sigma-ext.sh
fi

#echo -e "Sigma-AP Agent version $CA_VERSION is running...\n" > /dev/console ^^Gal
info_print "Sigma-AP Agent version $CA_VERSION is running ..."

#important, set field separator properly
IFS=" "

let ap_line_count=0

while read line; do
	debug_print "read: >>>$line<<<"
	let ap_line_count+=1
	# remove special characters except comma, underscore, exclamation mark
	#tline=`echo $line | tr -dc '[:alnum:],_!\n'`
	#debug_print "tline: >>>$tline<<<"
	# For Windows hosts we need to remove carriage returns from the line
	tline=`echo $line | tr -d '\r'`
	# Delete trailing spaces
	tline=`echo $tline | tr -d ' '`
	debug_print "tline: >>>$tline<<<"
	IFS=,
	parse_command $tline
	IFS=" "

	debug_print "lines parsed: $ap_line_count"
	debug_print "clearing all temp ap_ variables"
	variables=`set | grep "^ap_" | cut -d= -f1 | xargs echo `
	for var in $variables; do
		#debug_print "clearing $var"
		unset ${var}
	done
	unset variables
	unset token
	unset line
	unset tline
done