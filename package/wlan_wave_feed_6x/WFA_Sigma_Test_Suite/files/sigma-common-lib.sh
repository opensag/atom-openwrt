#!/bin/sh
# sigma-common-lib.sh

##############################
## User Configurable Definitions
##############################
# For 5.3.0 and lower
#CONF_DIR=/tmp/wlan_wave
# For 5.4.0 and higher
export CONF_DIR=/opt/lantiq/wave/confs
export WAVE_DIR=/opt/lantiq/wave/
export SINGLE_INTF_SUPPORT=0 # Single radio interface support. 0=dual intf (wlan0 and wlan2) - default, 1=single intf (wlan0) - for debug.
export DEBUGGING=0 # 1=enabled 0=disabled
export DEBUGGING_FILE=/opt/lantiq/wave/confs/sigma_debug_on
export SIGMA_CERTIFICATION_ON="${WAVE_DIR}/SIGMA_CERTIFICATION_ON"
export SIGMA_MBO_ON="${WAVE_DIR}/SIGMA_MBO_ON"
export TERMINATE_EVENT_LISTENER="0" # BSS transition e.g. MBO-4.2.6 workaround: Set to 1 to kill hostapd events listener. Rationale: probe request events are sent up because of VSIE, and are causing probe responses to be sent late and STA to miss them



##############################
## Constant Definitions
##############################
if [ -e $DEBUGGING_FILE ]; then
	DEBUGGING=1
fi

SMDPIPE=/tmp/sigmaManagerPipe

sigma_print()
{
	echo "$*" > /tmp/sigma-pipe
}

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

# get the ESSID of an interface.
get_essid()
{
	local interface_name field

	interface_name=$1
	[ -z "$1" ] && error_print "get_essid: ERROR: Missing interface_name" && echo -1 && return

	essid=`iwconfig $interface_name | grep $interface_name`
	field=${essid%%\"*}
	essid="${essid/$field\"/""}"
	essid=${essid%%\"*}

	echo $essid
}

set_conf_params_in_run_time()
{
	local interface_name count=0 essid

	[ -z "$radio_if_for_run_time" ] && error_print "set_conf_params_in_run_time: ERROR: Missing interface_name" && echo -1 && return
	[ -z "$num_non_tx_bss" ] && error_print "set_conf_params_in_run_time: ERROR: Missing num_non_tx_bss" && echo -1 && return
	[ -z "$non_tx_bss_index" ] && error_print "set_conf_params_in_run_time: ERROR: Missing non_tx_bss_index" && echo -1 && return

	while [ $count -lt 10 ]; do
		sleep 1

		essid=`get_essid $radio_if_for_run_time`
		if [ -n "$essid" ]; then
			#if [ $num_non_tx_bss = 0 ] || [ $non_tx_bss_index = 0 ]; then
				#hostapd_cli -i${radio_if_for_run_time} set multibss_enable 0 > /dev/null
			#else
				#hostapd_cli -i${radio_if_for_run_time} set multibss_enable 1 > /dev/null
			#fi

			#hostapd_cli -i${radio_if_for_run_time} reload > /dev/null
			break
		fi

		count=$((count+1))
	done
}

# get how many STA are connected per interface.
get_num_of_connected_sta()
{
	local num_of_sta interface_name

	interface_name=$1
	num_of_sta=`iw dev $interface_name station dump | grep -c Station`
	echo $num_of_sta
}

# get maximum ppdu transmission time limit from FAPI conf files.
get_max_ppdu_transmission_time_limit()
{
	local interface_name interface_index TMP1 max_ppdu_transmission_time_limit
	interface_name=$1
	interface_index=${interface_name/wlan/}
	TMP1=`cat $CONF_DIR/fapi_wlan_wave_radio_conf | sed -n 's/^WaveSPDlComMaximumPpduTransmissionTimeLimit_'"$interface_index"'=//p'`
	eval max_ppdu_transmission_time_limit=`printf $TMP1`
	echo "$max_ppdu_transmission_time_limit"
}

# get SP BW per interface from FAPI conf files.
get_plan_bw()
{
	local interface_name interface_index TMP1 sp_bw

	interface_name=$1
	interface_index=${interface_name/wlan/}
	TMP1=`cat $CONF_DIR/fapi_wlan_wave_radio_conf | sed -n 's/^WaveSPTxopComStartBwLimit_'"$interface_index"'=//p'`
	eval sp_bw=`printf $TMP1`
	echo "$sp_bw"
}

# get SP number of users per interface from FAPI conf files.
get_plan_num_users()
{
	local interface_name interface_index TMP1 sp_num_users

	interface_name=$1
	interface_index=${interface_name/wlan/}
	TMP1=`cat $CONF_DIR/fapi_wlan_wave_radio_conf | sed -n 's/^WaveSPDlComNumOfParticipatingStations_'"$interface_index"'=//p'`
	eval sp_num_users=`printf $TMP1`
	echo "$sp_num_users"
}

# get twt_resp per interface from FAPI conf files.
get_plan_twt_respsupport()
{
	local interface_name interface_index TMP1 sp_twt_resp

	interface_name=$1
	interface_index=${interface_name/wlan/}
	TMP1=`cat $CONF_DIR/fapi_wlan_wave_radio_conf | sed -n 's/^HeTargetWakeTime_'"$interface_index"'=//p'`
	eval sp_twt_resp=`printf $TMP1`
	[ "$sp_twt_resp" = "1" ] && sp_twt_resp="true"
	[ "$sp_twt_resp" = "0" ] && sp_twt_resp="false"
	echo "$sp_twt_resp"
}

# get SP Enable (0=disabled, 1=enabled)
get_plan_enable()
{
	#local interface_index=${ap_interface/wlan/}
	local interface_name interface_index TMP1 sp_enable
	interface_name=$1

	interface_index=${interface_name/wlan/}
	TMP1=`cat $CONF_DIR/fapi_wlan_wave_radio_conf | sed -n 's/^WaveSPEnable_'"$interface_index"'=//p'`
	eval sp_enable=`printf $TMP1`
	[ "$sp_enable" = "true" ] && sp_enable=1
	[ "$sp_enable" = "false" ] && sp_enable=0
	echo "$sp_enable"
}

# get SP DL NSS per interface, per user, from FAPI conf files
get_plan_nss_dl()
{
	local interface_name interface_index TMP1 user_index sp_nss_mcs sp_nss

	interface_name=$1
	user_index=$2
	interface_index=${interface_name/wlan/}
	TMP1=`cat $CONF_DIR/fapi_wlan_wave_radio_conf | sed -n 's/^WaveSPDlUsrPsduRatePerUsp'"$user_index"'_'"$interface_index"'=//p'`
	eval sp_nss_mcs=`printf $TMP1`
	let sp_nss="$sp_nss_mcs/16+1"
	echo "$sp_nss"
}

# get SP DL MCS per interface, per user, from FAPI conf files
get_plan_mcs_dl()
{
	local interface_name interface_index TMP1 user_index sp_nss_mcs sp_mcs

	interface_name=$1
	user_index=$2
	interface_index=${interface_name/wlan/}
	TMP1=`cat $CONF_DIR/fapi_wlan_wave_radio_conf | sed -n 's/^WaveSPDlUsrPsduRatePerUsp'"$user_index"'_'"$interface_index"'=//p'`
	eval sp_nss_mcs=`printf $TMP1`
	let sp_mcs="$sp_nss_mcs%16"
	echo "$sp_mcs"
}

# get SP UL NSS per interface, per user, from FAPI conf files
get_plan_nss_ul()
{
	local interface_name interface_index TMP1 user_index sp_nss_mcs sp_nss

	interface_name=$1
	user_index=$2
	interface_index=${interface_name/wlan/}
	TMP1=`cat $CONF_DIR/fapi_wlan_wave_radio_conf | sed -n 's/^WaveSPDlUsrUlPsduRatePerUsp'"$user_index"'_'"$interface_index"'=//p'`
	eval sp_nss_mcs=`printf $TMP1`
	let sp_nss="$sp_nss_mcs/16+1"
	echo "$sp_nss"
}

# get SP UL MCS per interface, per user, from FAPI conf files
get_plan_mcs_ul()
{
	local interface_name interface_index TMP1 user_index sp_nss_mcs sp_mcs

	interface_name=$1
	user_index=$2
	interface_index=${interface_name/wlan/}
	TMP1=`cat $CONF_DIR/fapi_wlan_wave_radio_conf | sed -n 's/^WaveSPDlUsrUlPsduRatePerUsp'"$user_index"'_'"$interface_index"'=//p'`
	eval sp_nss_mcs=`printf $TMP1`
	let sp_mcs="$sp_nss_mcs%16"
	echo "$sp_mcs"
}

# get SP TF length for an interface from FAPI conf files.
get_plan_tf_length()
{
	local interface_name interface_index TMP1 sp_tf_length

	interface_name=$1
	interface_index=${interface_name/wlan/}
	TMP1=`cat $CONF_DIR/fapi_wlan_wave_radio_conf | sed -n 's/^WaveSPRcrComTfLength_'"$interface_index"'=//p'`
	eval sp_tf_length=`printf $TMP1`
	echo "$sp_tf_length"
}

# get test-case name/number (tc_name) - retrieved from the SSID.
# returns test-case name/number, if not found, returns 0.
get_tc_name()
{
	local ssid_name tc_name

	[ -z "$1" ] && echo "get_tc_name: ERROR: Missing ssid name" && echo 0
	ssid_name=$1

	tc_name=${ssid_name##*-}  #trim leading prefix
	tc_name=${tc_name%%_*}    #trim trailing postfix

	echo "$tc_name"
}

# get test-case name/number (tc_name) - retrieved from the SSID.
# per test-case number, return '0' (if it is inside the list) or '1'.
# the reason for this is to prevent setting the plan when stations being connected; plan will be set upon different command
get_sp_enable_val()
{
	local ssid_name tc_name sp_enable_value

	[ -z "$1" ] && echo "get_tc_name: ERROR: Missing ssid name" && echo 0
	ssid_name=$1

	tc_name=`get_tc_name $ssid_name`

	case "$tc_name" in
		5.64.1) sp_enable_value=0 ;;
		*) sp_enable_value=1 ;; # not found
	esac

	echo "$sp_enable_value"
}

# get number of participating STA per HE TC (retrieved from the SSID).
# returns the no. of sta, if not found, returns 0.
get_nof_sta_per_he_test_case()
{
	local ssid_name tc_name nof_sta
	ssid_name=$1

	tc_name=`get_tc_name $ssid_name`

	case "$tc_name" in
		4.58.1) nof_sta=1 ;; # UL only.TBD DL if needed.
		4.68.1) nof_sta=1 ;;
		5.60.1) nof_sta=1 ;;
		5.61.1) nof_sta=1 ;;
		5.73.1) nof_sta=1 ;;
		5.74.1) nof_sta=1 ;;
		4.43.1) nof_sta=2 ;;
		4.44.1) nof_sta=2 ;;
		4.46.1) nof_sta=2 ;;
		4.53.1) nof_sta=2 ;;
		4.53.2) nof_sta=2 ;;
		4.53.3) nof_sta=2 ;;
		4.54.1) nof_sta=2 ;;
		4.56.1) nof_sta=2 ;;
		4.63.1) nof_sta=2 ;;
		5.47.1) nof_sta=2 ;;
		5.48.1) nof_sta=2 ;;
		5.52.1) nof_sta=2 ;;
		5.54.1) nof_sta=2 ;;
		5.55.1) nof_sta=2 ;;
		5.57.1) nof_sta=2 ;;
		5.57.2) nof_sta=2 ;;
		5.57.3) nof_sta=2 ;;
		5.58.1) nof_sta=2 ;;
# TODO: 5.44.2) nof_sta=2 and 4 ;;
		4.36.1) nof_sta=4 ;;
		4.37.1) nof_sta=4 ;;
		4.40.1) nof_sta=4 ;;
		4.40.2) nof_sta=4 ;;
		4.40.3) nof_sta=4 ;;
		4.40.4) nof_sta=4 ;;
		4.40.5) nof_sta=4 ;;
		4.41.1) nof_sta=4 ;;
		4.41.2) nof_sta=4 ;;
		4.49.1) nof_sta=4 ;;
		4.55.1) nof_sta=4 ;;
		4.62.1) nof_sta=4 ;;
		4.69.1) nof_sta=4 ;;
		5.44.1) nof_sta=4 ;;
		5.44.2) nof_sta=4 ;;
		5.44.3) nof_sta=4 ;;
		5.44.4) nof_sta=4 ;;
		5.44.5) nof_sta=4 ;;
		5.44.6) nof_sta=4 ;;
		5.44.7) nof_sta=4 ;;
		5.44.8) nof_sta=4 ;;
		5.44.9) nof_sta=4 ;;
		5.45.1) nof_sta=4 ;;
		5.45.2) nof_sta=4 ;;
		5.49.1) nof_sta=4 ;;
		5.50.1) nof_sta=4 ;;
		5.53.1) nof_sta=4 ;;
		*) nof_sta=0 ;; # not found
	esac

	echo "$nof_sta"
}

# check if test case is permitted to set channel or bw (TC retrieved from the SSID).
# returns '1' if permitted, else, returns '0'.
is_test_case_permitted_to_set_channel()
{
	local ssid_name tc_name tc_permitted
	ssid_name=$1

	tc_name=`get_tc_name $ssid_name`

	case "$tc_name" in
		5.40.1) tc_permitted="1" ;;
		*) tc_permitted="0" ;; # not found
	esac

	echo "$tc_permitted"
}

# Check for all the STAs connected if they support LDPC to update the plan accordingly.
# Info is saved to a file and the file is sourced to be used.
sp_check_ldpc_support()
{
	local interface_name HOSTAPD_CLI_ALL_STA_FILE LDPC_SUPPORT_FILE \
	current_aid line ldpc bcc_detected

	interface_name=$1
	bcc_detected="0"

	# Check for each STA if it supports LDPC or not and save to a file
	HOSTAPD_CLI_ALL_STA_FILE="/tmp/sigma-hostapd-cli-all-sta-conf"
	LDPC_SUPPORT_FILE="/tmp/sigma-ldpc-support-conf"
	rm -f $HOSTAPD_CLI_ALL_STA_FILE $LDPC_SUPPORT_FILE
	hostapd_cli -i${interface_name} all_sta $interface_name > $HOSTAPD_CLI_ALL_STA_FILE
	current_aid=0
	while read -r line || [[ -n "$line" ]]
	do
		# Find the aid of the current block
		if [ "${line##aid=}" != "$line" ]
		then
			current_aid=${line##aid=}
		# The LDPC support bit appears in he_phy line
		elif [ "${line##he_phy=}" != "$line" ]
		then
			ldpc=`echo $line | awk '{print $2}'`
			# bit#6 0x20 means STA supports LDPC
			ldpc="0x$ldpc"
			if [ "$((ldpc & 0x20))" != "0" ]
			then
					ldpc=1
			else
					ldpc=0
					bcc_detected=1
			fi
			echo "ldpc_${current_aid}=${ldpc}" >> $LDPC_SUPPORT_FILE
		fi
	done < $HOSTAPD_CLI_ALL_STA_FILE

	## WLANRTSYS-11900-TC HE-4.41.1
	[ "$bcc_detected" = "1" ] && sed -i 's/=1/=0/g' $LDPC_SUPPORT_FILE

	# Save ldpc support information to be set
	[ -e $LDPC_SUPPORT_FILE ] && source $LDPC_SUPPORT_FILE
}

# Get the maximum NSS value from all the STAs connected
get_max_nss()
{
	local interface_name MAX_NSS_FILE max_nss nss

	interface_name=$1

	MAX_NSS_FILE="/tmp/sigma-max-nss-file"
	rm -f $MAX_NSS_FILE
	max_nss=-1

	cat /proc/net/mtlk/$interface_name/Debug/sta_list | awk '{print $11}' > $MAX_NSS_FILE
	sed -i 's/|//' $MAX_NSS_FILE
	while read -r line || [[ -n "$line" ]]
	do
		[ -z "$line" ] && continue
		[ $line -gt $max_nss ] && max_nss=$line
	done < $MAX_NSS_FILE

	echo "$max_nss"
}

# Change the TF length according to maximum NSS value of the connected STAs.
# Change only if [ "$ap_ltf" = "6.4" ] && [ "$ap_gi" = "1.6" ], i.e.: length is 3094.
# If length is 2914, no need to change it.
# Currently, only NSS values of 1 and 2 are handled.
sp_set_plan_tf_length()
{
	local interface_name ap_mu_tf_len current_tf_length max_nss

	interface_name=$1
	ap_mu_tf_len=$2

	if [ -z $ap_mu_tf_len ]
	then
		ap_mu_tf_len=2914
		current_tf_length=`get_plan_tf_length $interface_name`
	else
		current_tf_length=$ap_mu_tf_len
	fi

	if [ $current_tf_length -eq 3094 ]
	then
		max_nss=`get_max_nss $interface_name`
		case $max_nss in
			1) ap_mu_tf_len=3076 ;;
			2) ap_mu_tf_len=3082 ;;
		esac
	fi
	echo "$ap_mu_tf_len"
}

# get WaveSPDlComMuType for an interface from FAPI conf files.
get_plan_dl_com_mu_type()
{
	local interface_name interface_index TMP1 dl_com_mu_type

	interface_name=$1
	interface_index=${interface_name/wlan/}
	TMP1=`cat $CONF_DIR/fapi_wlan_wave_radio_conf | sed -n 's/^WaveSPDlComMuType_'"$interface_index"'=//p'`
	eval dl_com_mu_type=`printf $TMP1`

	echo "$dl_com_mu_type"
}

aid_idx_out_of_list_get()
{
	local count=0 aid_idx

	[ -z "$1" ] && error_print "aid_idx_out_of_list_get: ERROR: Missing station sorted AID list" && echo -1 && return
	[ -z "$2" ] && error_print "aid_idx_out_of_list_get: ERROR: Missing number of parameter to get" && echo -1 && return

	sta_index_sorted_list="$1,"  # add , at the end for the parsing
	field_location=$2

	while [ $count -lt $field_location ]; do
		aid_idx=${sta_index_sorted_list%%,*}
		sta_index_sorted_list="${sta_index_sorted_list/$aid_idx,/""}"

		count=$((count+1))
	done

	echo "$aid_idx"
}

# get the radio's band
get_radio_band()
{
	local interface_name

	[ -z "$1" ] && error_print "get_sta_highest_bw: ERROR: Missing ifname" && echo -1 && return

	interface_name=$1

	tmp_channel=`cat /proc/net/mtlk/$interface_name/channel | grep primary_channel | awk '{print $2}'`
	if [ $tmp_channel -ge 36 ]; then
		echo "5GHz"
	else
		echo "2.4GHz"
	fi
}

# get the highest (last) station's BW
get_sta_highest_bw()
{
	local interface_name ap_client_mac sta_bw list_of_sta_bw

	[ -z "$1" ] && error_print "get_sta_highest_bw: ERROR: Missing ifname and sta mac address" && echo -1 && return
	[ -z "$2" ] && error_print "get_sta_highest_bw: ERROR: Missing sta mac address" && echo -1 && return

	interface_name=$1
	ap_client_mac=$2

	list_of_sta_bw=`hostapd_cli -i${interface_name} get_he_phy_channel_width_set $ap_client_mac`
	# "he_phy_chanwidth_set=20MHz,40MHz,80MHz,160MHz" or only part of these rates will the answer.
	# in case of a non-HE station, the response will be: "CTRL: GET_HE_PHY_CHANNEL_WIDTH_SET - STA doesn't have HE elements in assoc req"

	sta_bw=${list_of_sta_bw##*=}
	sta_bw=${sta_bw##*,}

	echo $sta_bw
}

# get the indication whether to lower ru-size or not - WLANRTSYS-9745
is_set_low_ru_size_get()
{
	local interface_name ru_size aid_index band ap_client_mac sta_aid sta_bw field

	[ -z "$1" ] && error_print "is_set_low_ru_size_get: ERROR: Missing ifname, ru_size and aid_index" && echo -1 && return
	[ -z "$2" ] && error_print "is_set_low_ru_size_get: ERROR: Missing ru_size and aid_index" && echo -1 && return
	[ -z "$3" ] && error_print "is_set_low_ru_size_get: ERROR: Missing aid_index" && echo -1 && return

	interface_name=$1
	ru_size=$2
	aid_index=$3

	if [ "$ru_size" != "3" ]; then
		echo "0"
		return
	fi

	band=`get_radio_band $interface_name`

	while read -r line || [[ -n "$line" ]]
	do
		# display $line or do something with $line
		field=`echo $line | awk '{print $1}' | tr  "\n"`
		ap_client_mac=`echo $field | grep ":"`
		if [ -n "$ap_client_mac" ]; then
			sta_aid=`echo $line | cut -d'|' -f 2`
			sta_aid="$(echo -e "${sta_aid}" | tr -d '[:space:]')"

			if [ "$sta_aid" != "" ] && [ $sta_aid -gt 0 ]; then
				if [ "$sta_aid" = "$aid_index" ]; then
					sta_bw=`get_sta_highest_bw $interface_name $ap_client_mac`
					if [ "$sta_bw" = "20MHz" ]; then
						# set the nss with the highest value; this way it will be the first one after sorting all stations
						echo "'$ap_client_mac' is a 20MHz station" > /dev/console
						sigma_print "'$ap_client_mac' is a 20MHz station"

						sta_he_caps=`hostapd_cli -i $interface_name get_sta_he_caps $ap_client_mac`

						if [ "$band" = "2.4GHz" ]; then
							if [ "${sta_he_caps//B4/}" = "$sta_he_caps" ]; then  # 'B4' is NOT present
								echo "1"
								return
							else
								# 'B4' is present - check if he_phy_20_mhz_in_160_slash_80_plus_80_mhz_he_ppdu != 1
								sta_he_caps=${sta_he_caps##*he_phy_20_mhz_in_40_mhz_he_ppdu_in_24_ghz_band=}
								sta_he_caps=${sta_he_caps%%he*}
								sta_he_caps=`echo $sta_he_caps`
								if [ "$sta_he_caps" != "1" ]; then
									echo "1"
									return
								fi
							fi
						elif [ "$band" = "5GHz" ]; then
							if [ "${sta_he_caps//B5/}" = "$sta_he_caps" ]; then  # 'B5' is NOT present
								echo "1"
								return
							else
								# 'B5' is present - check if he_phy_20_mhz_in_160_slash_80_plus_80_mhz_he_ppdu != 1
								sta_he_caps=${sta_he_caps##*he_phy_20_mhz_in_160_slash_80_plus_80_mhz_he_ppdu=}
								sta_he_caps=`echo $sta_he_caps`
								if [ "$sta_he_caps" != "1" ]; then
									echo "1"
									return
								fi
							fi
						fi
					fi
				fi
			fi
		fi
	done < /proc/net/mtlk/$interface_name/Debug/sta_list

	echo "0"
}

# get the sorted station AID index according to nss descending order (high to low)
get_sta_aid_idx_sorted_list()
{
	local interface_name field sta_bw
	local ap_client_mac aid_index sta_nss sta_index_sorted_list
	local SMD_AID_SS_FILE="/tmp/sigma-smd-aid-ss-conf"
	local SMD_AID_SS_FILE_SORTED="/tmp/sigma-smd-aid-ss-conf-sorted"

	interface_name=$1
	[ -z "$1" ] && error_print "get_sta_aid_idx_sorted_list: ERROR: Missing interface_name" && echo -1 && return

	[ -e $SMD_AID_SS_FILE ] && rm $SMD_AID_SS_FILE && rm $SMD_AID_SS_FILE_SORTED

	while read -r line || [[ -n "$line" ]]
	do
		# display $line or do something with $line
		field=`echo $line | awk '{print $1}' | tr  "\n"`
		ap_client_mac=`echo $field | grep ":"`
		if [ -n "$ap_client_mac" ]; then
			aid_index=`echo $line | cut -d'|' -f 2`
			aid_index="$(echo -e "${aid_index}" | tr -d '[:space:]')"
			sta_nss=`echo $line | cut -d'|' -f 6`

			# remove all blanks (before and after the digits)
			sta_nss="$(echo -e "${sta_nss}" | tr -d '[:space:]')"

			# add 100 just for the sort to work fine
			sta_nss=$((sta_nss+100))

			# here check the station's maximum band width, and mark it to be the 1st one at the sorted list 

			sta_bw=`get_sta_highest_bw $interface_name $ap_client_mac`

			if [ "$sta_bw" = "20MHz" ]; then
				# set the nss with the highest value; this way it will be the first one after sorting all stations
				sta_nss=$((sta_nss+100))
				echo "'$ap_client_mac' is a 20MHz station" > /dev/console
				sigma_print "'$ap_client_mac' is a 20MHz station"
			fi

			echo "$sta_nss,$aid_index,$ap_client_mac" >> $SMD_AID_SS_FILE
		fi
	done < /proc/net/mtlk/$interface_name/Debug/sta_list

	sort -r $SMD_AID_SS_FILE > $SMD_AID_SS_FILE_SORTED

	## update all users according to the AID_SS_FILE_SORTED higher to lower ss.
	sta_index_sorted_list=""
	while read -r line || [[ -n "$line" ]]
	do
		## 2 params in line : nss,aid_index
		aid_index=`echo $line | cut -d',' -f 2`

		if [ -z "$sta_index_sorted_list" ]; then
			sta_index_sorted_list="$aid_index"
		else
			sta_index_sorted_list="$sta_index_sorted_list,$aid_index"
		fi
	done <  $SMD_AID_SS_FILE_SORTED

	echo "$sta_index_sorted_list"
}

# get the value of ap_nss_mcs_val out of ap_nss_cap and ap_mcs_max_cap
get_nss_mcs_val()
{
	local ap_nss_mcs_val ap_nss ap_mcs

	[ -z "$1" ] && echo "get_nss_mcs_val: ERROR: Missing ap_nss" && return
	[ -z "$2" ] && echo "get_nss_mcs_val: ERROR: Missing ap_mcs" && return

	ap_nss=$1
	ap_mcs=$2

	if [ "$ap_nss" = "1" ] && [ "$ap_mcs" = "7" ]; then
		ap_nss_mcs_val=65532
	elif [ "$ap_nss" = "1" ] && [ "$ap_mcs" = "9" ]; then
		ap_nss_mcs_val=65533
	elif [ "$ap_nss" = "1" ] && [ "$ap_mcs" = "11" ]; then
		ap_nss_mcs_val=65534
	elif [ "$ap_nss" = "2" ] && [ "$ap_mcs" = "7" ]; then
		ap_nss_mcs_val=65520
	elif [ "$ap_nss" = "2" ] && [ "$ap_mcs" = "9" ]; then
		ap_nss_mcs_val=65525
	elif [ "$ap_nss" = "2" ] && [ "$ap_mcs" = "11" ]; then
		ap_nss_mcs_val=65530
	elif [ "$ap_nss" = "3" ] && [ "$ap_mcs" = "7" ]; then
		ap_nss_mcs_val=65472
	elif [ "$ap_nss" = "3" ] && [ "$ap_mcs" = "9" ]; then
		ap_nss_mcs_val=65493
	elif [ "$ap_nss" = "3" ] && [ "$ap_mcs" = "11" ]; then
		ap_nss_mcs_val=65514
	elif [ "$ap_nss" = "4" ] && [ "$ap_mcs" = "7" ]; then
		ap_nss_mcs_val=65280
	elif [ "$ap_nss" = "4" ] && [ "$ap_mcs" = "9" ]; then
		ap_nss_mcs_val=65365
	elif [ "$ap_nss" = "4" ] && [ "$ap_mcs" = "11" ]; then
		ap_nss_mcs_val=65450
	else
		error_print "Unsupported value - ap_nss_cap:$1 ap_mcs_max_cap:$2"
		return
	fi

	echo "$ap_nss_mcs_val"
}

# check clish is operational. return value: 0: success, else: empty string
ap_clish_validity()
{
	local num_of_inf clish_test_size0 clish_test_size2 ret_val
	num_of_inf=`ifconfig | grep -c wlan`
	ret_val="0"

	# check clish validity wlan0
	[ -e clish_test0 ] && rm clish_test0
	[ $num_of_inf -ge 1 ] && clish -c "show wlan SSID wlan0" > clish_test0
	[ -e clish_test0 ] && clish_test_size0=`wc -c clish_test0 | awk '{print $1}'`
	[ -n "$clish_test_size0" ] && [ $clish_test_size0 -lt 60 ] && ret_val=""

	# check clish validity wlan2 (only if exist)
	[ -e clish_test2 ] && rm clish_test2
	[ $num_of_inf -ge 2 ] && clish -c "show wlan SSID wlan2" > clish_test2
	[ -e clish_test2 ] && clish_test_size2=`wc -c clish_test2 | awk '{print $1}'`
	[ -n "$clish_test_size2" ] && [ $clish_test_size2 -lt 60 ] && ret_val=""

	echo "$ret_val"
}

SIGMA_COMMON_LIB_SOURCED="1"
