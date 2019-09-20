#!/bin/sh
# sigma-ap.sh

# Test script for Wi-Fi Sigma Control API for APs
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
FW_VERSION=`version.sh | grep "Wave wlan version" | awk -F ": " '{print $2}'`
CA_VERSION="Sigma-CAPI-10.2.119-${FW_VERSION}"

MODEL='AxePoint'
VENDOR='INTEL'
# TIMESTAMP="cat /proc/uptime"
TIMESTAMP=

# source for common and debug tools
[ ! "$SIGMA_COMMON_LIB_SOURCED" ] && . /opt/lantiq/wave/scripts/sigma-common-lib.sh

WLAN_RUNNING=1 # Used to check if stopping is needed

# default interface
AP_IF_ACTIVE='wlan0'

DEFAULT_IFS=$IFS

# default values for normal (dut) mode
nss_def_val_dl="2"	# Range: 1-4
mcs_def_val_dl="11"	# Range: 0-11
nss_def_val_ul="2"	# Range: 1-4
mcs_def_val_ul="11"	# Range: 0-11

# default values for testbed mode
nss_def_val_dl_testbed="2"	# Range: 1-4
mcs_def_val_dl_testbed="7"	# Range: 0-11
nss_def_val_ul_testbed="2"	# Range: 1-4
mcs_def_val_ul_testbed="7"	# Range: 0-11

## dual band tags support
wlan_tag_1_interface=""
wlan_tag_2_interface=""

# command files for mbssid
CMD_MBSS_WIRELESS_FILE="/tmp/sigma_mbss_wireless_cmds"
CMD_MBSS_SECURITY_FILE="/tmp/sigma_mbss_security_cmds"
CMD_WPA3_SECURITY_FILE="/tmp/sigma_wpa3_security_cmds"

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
if [ ! -e /opt/lantiq/wave/scripts/ft_mac_address ]; then
	echo AP_MAC1_wlan0=00:09:86:05:02:80 >> /opt/lantiq/wave/scripts/ft_mac_address
	echo AP_MAC1_wlan2=00:09:86:05:02:90 >> /opt/lantiq/wave/scripts/ft_mac_address
	echo AP_MAC2_wlan0=00:E0:92:00:01:50 >> /opt/lantiq/wave/scripts/ft_mac_address
	echo AP_MAC2_wlan2=00:E0:92:00:01:60 >> /opt/lantiq/wave/scripts/ft_mac_address
	#echo AP_MAC3_wlan0=00:E0:92:01:00:20 >> /opt/lantiq/wave/scripts/ft_mac_address
	#echo AP_MAC3_wlan2=00:E0:92:01:00:30 >> /opt/lantiq/wave/scripts/ft_mac_address
fi

#AP_MAC1=00:09:05:09:04:50
#AP_MAC2=00:09:09:09:86:70
source /opt/lantiq/wave/scripts/ft_mac_address

#########################
# Configuration state machine:
# We don't want to bring the interface down and up for each Sigma command, only after receiving the commit command
# so we will implement a simple state machine for saving all configurations to be handled during commit
# TODO: Will we be able to return config errors to Sigma in this flow? These weren't sent before, so this is probably not a major problem.
#
# STATES: not-started, started
# Initial state: not-started.
#
# OPERATIONS:
# If not-started and a config command is received, write the start command, and the config command, and change the state to started
# If started and a config command is received, write the config command
# If started and a commit command is received, write the commit command (bring interface up) and change state to not started
#
# In case of clish, the commands are:
### start: "clish -c \"configure wlan\" -c \"start\"
### configure: -c \"set radio $ap_radio_interface $radio_params\" -c \"set ssid $ap_interface SSID $ap_ssid\" -c \"set ap $ap_interface $ap_params\" -c \"set ap security $ap_interface $sec_params\"
### commit: -c \"commit\"
### All commands will be written to a tmp script until the command is committed. On commit the script is executed and deleted
CONFIG_SM="not-started"
CONFIG_TMP="/tmp/sigma-config.sh"
CONFIG_TMP_INTERFACES="/tmp/sigma-config-interfaces.conf"
CONFIG_ADD_WLAN0_VAP="/tmp/sigma-config-add-wlan0-vap"
CONFIG_ADD_WLAN2_VAP="/tmp/sigma-config-add-wlan2-vap"
SIGMA_SMD_OFDMA_REPLAN="/tmp/sigma-config-SMD-act"

perform_power_change()
{
	[ -z "$1" ] && echo "perform_power_change: ERROR: Missing interface name" && return
	local ifname=$1

	if [ -n "$global_mcs_fixedrate" ]; then
		iw $ifname iwlwav sFixedPower 0 0 40 1
	fi
}

perform_mbssid_commands()
{
	local is_mbssid=0

	[ -z "$1" ] && echo "perform_mbssid_commands: ERROR: Missing interface name" && return

	if [ "$global_ap_mbssid" != "enable" ]; then
		return
	fi

	if [ -s "$CMD_MBSS_WIRELESS_FILE" ]; then
		echo "commit" >> $CMD_MBSS_WIRELESS_FILE
		echo "exit" >> $CMD_MBSS_WIRELESS_FILE
		clish $CMD_MBSS_WIRELESS_FILE > /dev/console
		rm -f $CMD_MBSS_WIRELESS_FILE
	fi

	if [ -s "$CMD_MBSS_SECURITY_FILE" ]; then
		echo "commit" >> $CMD_MBSS_SECURITY_FILE
		echo "exit" >> $CMD_MBSS_SECURITY_FILE
		clish $CMD_MBSS_SECURITY_FILE > /dev/console
		rm -f $CMD_MBSS_SECURITY_FILE
	fi

	clish -c "configure wlan" -c "start" -c "set radio $1 WaveMultiBssEnable 1" -c "set radio $1 WaveHtOperationCohostedBss 0" -c "commit" 1>&2
}

clish_config_start()
{
	echo -n "clish " > $CONFIG_TMP
	rm -f $CONFIG_TMP_INTERFACES
	##[ "$DEBUGGING" = "1" ] && echo -n "-c \"set debug on\" " >> $CONFIG_TMP
	echo -n "-c \"configure wlan\" -c \"start\" " >> $CONFIG_TMP
	## in case clish debug is needed (adding set debug on to all clish commands)
	## echo -n "-c \"set debug on\" -c \"configure wlan\" -c \"start\" " >> $CONFIG_TMP
}

clish_dual_band_support()
{
	local input_params command_type input_params_and_vals input_param_name index is_dual_input_params \
			input_param_val interface param_value

	input_params="$1"
	command_type="$2"
	input_interfaces="$3"

	## trimmed leading and trailing spaces
	input_params="${input_params## }"
	input_params="${input_params%% }"
	## replace all space with comma
	input_params=${input_params//" "/,}
	for input_params_and_vals in $input_params
	do
		let index=index+1
		if [ $(( $index % 2 )) -eq 1 ]; then
			input_param_name="$input_params_and_vals"
			continue
		else
			input_param_val="$input_params_and_vals"
		fi
		is_dual_input_params=${input_param_val/";"/}
		# we are dual band but params are not dual make them dual, same for both interface.
		[ "$is_dual_input_params" = "$input_param_val" ] && input_param_val="$input_param_val;$input_param_val"
		input_param_val=${input_param_val//;/,}
		## assuming 2 param for 2 interfaces.
		index=0
		interface="$input_interfaces"
		interface1=${interface%%,*}
		interface2=${interface##*,}
		for param_value in $input_param_val
		do
			let index=index+1
			interface="interface$index"
			eval interface=\$$interface
			[ "$input_param_name" = "OperatingStandards" ] && param_value=${param_value//./,}
			echo -n "-c \"set $command_type $interface $input_param_name $param_value\" " >> $CONFIG_TMP
		done
	done
}

clish_config_set()
{
	if [ "$CONFIG_SM" = "not-started" ]; then
		clish_config_start
		CONFIG_SM="started"
	fi

	echo $ap_radio_interface >> $CONFIG_TMP_INTERFACES

	if [ "$is_dual_band" = "1" ]; then
		if [ "$radio_params" != "" ]; then
			clish_dual_band_support $radio_params "radio" $ap_radio_interface
			radio_params=""
		fi
		if [ "$ap_params" != "" ]; then
			clish_dual_band_support $ap_params "ap" $ap_interface
			ap_params=""
		fi
		if [ "$sec_params" != "" ]; then
			clish_dual_band_support $sec_params "ap security" $ap_interface
			sec_params=""
		fi
		if [ "$ap_ssid" != "" ]; then
			ap_ssid="SSID $ap_ssid"
			clish_dual_band_support $ap_ssid "ssid" $ap_interface
			ap_ssid=""
		fi
	## dual band tags support
	## PAY ATTENTION!!! - currently according to TC 4.7.1 only ap_set_security is done without interface or tag.
	## another way( all other params get explicit interface so treated as single band else here).
	elif ([ "$ap_interface" = "" ] || [ "$ap_radio_interface" = "" ]) && ([ "$wlan_tag_1_interface" != "" ] && [ "$wlan_tag_2_interface" != "" ]); then
		interfaces_wlan_tags="$wlan_tag_1_interface,$wlan_tag_2_interface"
		[ "$sec_params" != "" ] && clish_dual_band_support $sec_params "ap security" "$interfaces_wlan_tags"
		sec_params=""
	else
		# TODO: Debug: Should we always clean up the parameters after writing? Make sure this is ok also with ap_ssid (radio and ap params most likely safe to clean up)
		[ "$radio_params" != "" ] && echo -n "-c \"set radio $ap_radio_interface $radio_params\" " >> $CONFIG_TMP && radio_params=""
		[ "$ap_params" != "" ] && echo -n "-c \"set ap $ap_interface $ap_params\" " >> $CONFIG_TMP && ap_params=""
		[ "$sec_params" != "" ] && echo -n "-c \"set ap security $ap_interface $sec_params\" " >> $CONFIG_TMP && sec_params=""
		[ "$ap_ssid" != "" ] && echo -n "-c \"set ssid $ap_interface SSID $ap_ssid\" " >> $CONFIG_TMP && ap_ssid=""
	fi

	# TODO: Max command length in clish is 512 - need to make sure that we don't exceed, or break to multiple commands.
	# (might be an issue in MBSSID tests) TBD: It might be that every -c has its own memory allocation so currently this seems to work even in long lines.
}

clish_config_commit()
{
	if [ "$CONFIG_SM" = "started" ]; then
		if [ "$radio_off" != "true" ] && [ "$radio_on" != "true" ]
		then
			if [ "$is_dual_band" = "1" ]; then
				radio_params=$radio_params" radioEnable 1"
				clish_dual_band_support $radio_params "radio"
			else
				CURRENT_IFS="$IFS"
				IFS=$'\n'
				for saved_interface in `sort -u $CONFIG_TMP_INTERFACES`
				do
					# by WFA: always turn on radio except in case where "radio off" was explicitly specified
					echo -n "-c \"set radio $saved_interface radioEnable 1\" " >> $CONFIG_TMP
				done
				IFS="$CURRENT_IFS"
			fi
		elif [ "$radio_off" = "true" ]; then
			# when doing "radio off" always turn off both bands
			echo -n "-c \"set radio wlan0 radioEnable 0\" " >> $CONFIG_TMP
			[ "$SINGLE_INTF_SUPPORT" = "0" ] && echo -n "-c \"set radio wlan2 radioEnable 0\" " >> $CONFIG_TMP

			# radio off command was sent. wait 10 sec. let it run and beacons will stop.
			sleep 10
		fi
		radio_off=""
		radio_on=""
		echo "-c \"commit\" 1>&2" >> $CONFIG_TMP

		while [ -n "$(pgrep -f clish)" ]; do
			info_print "clish_config_commit(): clish command is on-going. pending..."
			sleep 1
		done
		debug_print "clish_config_commit(): clish command finished. continue ..."

		set -x
		source $CONFIG_TMP
		set +x

		if [ "$TERMINATE_EVENT_LISTENER" = "1" ]; then
			# MBO-4.2.6 workaround: Kill hostapd events listener - probe request events are causing probe responses to be sent late and STA to miss them
			ps | awk '/hostapd_cli/ {print $1}' | xargs kill
		fi

		# example of hoe to use set_wireless like rfeature:
		#if [ -n "$num_non_tx_bss" ] && [ -n "$non_tx_bss_index" ]; then
		#	[ -n "$radio_if_for_run_time" ] && set_conf_params_in_run_time
		#fi

		# add touch $SIGMA_SMD_OFDMA_REPLAN to any command that needs Sigma Manager Daemon (SMD) for OFDMA.
		# currently not it use. please use ap_rualloctones as an example.
		if [ -e $SIGMA_SMD_OFDMA_REPLAN ]; then
			[ -e ${SMDPIPE} ] && echo "SIGMA_SMD_OFDMA_REPLAN,WLAN,${saved_interface}" > ${SMDPIPE}
			rm $SIGMA_SMD_OFDMA_REPLAN
		fi

		# WA: Handling OCE tags and neighbor reports
		# need to add ssid param to the neighbor report AML command to ap_bss_neighbor_set_params.
		# wlan2.0 for UCC: APUT-4.5.1
		if [ -e $CONFIG_ADD_WLAN2_VAP ]; then
			clish -c "configure wlan" -c "add vap wlan2 OCE-APUT-Guest" 1>&2 && rm $CONFIG_ADD_WLAN2_VAP
			ap_bss_neighbor_set_params=""
			ap_to_report_neighbors="wlan0"
			ap_neighbor_interface="wlan2"
			vap_neighbor_interface="wlan2.0"
			ap_mac=`cat /sys/class/net/$ap_neighbor_interface/address`
			vap_mac=`cat /sys/class/net/$vap_neighbor_interface/address`
			ap_bss_neighbor_set_params=$ap_bss_neighbor_set_params" opClass=115"
			ap_bss_neighbor_set_params=$ap_bss_neighbor_set_params" channelNumber=36"
			ap_bss_neighbor_set_params=$ap_bss_neighbor_set_params" priority=100"

			bss_neighbor_set_mac="$ap_mac"
			sed -i "/ssid_0=/d" /usr/lib/BeeRock.General.db_wlan0
			sed -i '2i\ssid_0="OCE-APUT"' /usr/lib/BeeRock.General.db_wlan0
			debug_print "/usr/sbin/fapi_wlan_debug_cli COMMAND \"AML_BSS_NEIGHBOR_SET $ap_to_report_neighbors $bss_neighbor_set_mac $ap_bss_neighbor_set_params\""
			/usr/sbin/fapi_wlan_debug_cli COMMAND "AML_BSS_NEIGHBOR_SET $ap_to_report_neighbors $bss_neighbor_set_mac $ap_bss_neighbor_set_params" 1 >> $CONF_DIR/BeeRock_CMD.log

			bss_neighbor_set_mac="$vap_mac"
			sed -i "/ssid_0=/d" /usr/lib/BeeRock.General.db_wlan0
			sed -i '2i\ssid_0="OCE-APUT-Guest"' /usr/lib/BeeRock.General.db_wlan0
			debug_print "/usr/sbin/fapi_wlan_debug_cli COMMAND \"AML_BSS_NEIGHBOR_SET $ap_to_report_neighbors $bss_neighbor_set_mac $ap_bss_neighbor_set_params\""
			/usr/sbin/fapi_wlan_debug_cli COMMAND "AML_BSS_NEIGHBOR_SET $ap_to_report_neighbors $bss_neighbor_set_mac $ap_bss_neighbor_set_params" 1 >> $CONF_DIR/BeeRock_CMD.log

		fi
		# wlan0.0 for UCC: APUT-4.5.1
		if [ -e $CONFIG_ADD_WLAN0_VAP ]; then
			clish -c "configure wlan" -c "add vap wlan0 OCE-APUT-Guest" 1>&2 && rm $CONFIG_ADD_WLAN0_VAP
			ap_bss_neighbor_set_params=""
			ap_to_report_neighbors="wlan2"
			ap_neighbor_interface="wlan0"
			vap_neighbor_interface="wlan0.0"
			ap_mac=`cat /sys/class/net/$ap_neighbor_interface/address`
			vap_mac=`cat /sys/class/net/$vap_neighbor_interface/address`
			ap_bss_neighbor_set_params=$ap_bss_neighbor_set_params" opClass=81"
			ap_bss_neighbor_set_params=$ap_bss_neighbor_set_params" channelNumber=6"
			ap_bss_neighbor_set_params=$ap_bss_neighbor_set_params" priority=100"

			bss_neighbor_set_mac="$ap_mac"
			sed -i "/ssid_0=/d" /usr/lib/BeeRock.General.db_wlan2
			sed -i '2i\ssid_0="OCE-APUT"' /usr/lib/BeeRock.General.db_wlan2
			debug_print "/usr/sbin/fapi_wlan_debug_cli COMMAND \"AML_BSS_NEIGHBOR_SET $ap_to_report_neighbors $bss_neighbor_set_mac $ap_bss_neighbor_set_params\""
			/usr/sbin/fapi_wlan_debug_cli COMMAND "AML_BSS_NEIGHBOR_SET $ap_to_report_neighbors $bss_neighbor_set_mac $ap_bss_neighbor_set_params" 1 >> $CONF_DIR/BeeRock_CMD.log

			bss_neighbor_set_mac="$vap_mac"
			sed -i "/ssid_0=/d" /usr/lib/BeeRock.General.db_wlan2
			sed -i '2i\ssid_0="OCE-APUT-Guest"' /usr/lib/BeeRock.General.db_wlan2
			debug_print "/usr/sbin/fapi_wlan_debug_cli COMMAND \"AML_BSS_NEIGHBOR_SET $ap_to_report_neighbors $bss_neighbor_set_mac $ap_bss_neighbor_set_params\""
			/usr/sbin/fapi_wlan_debug_cli COMMAND "AML_BSS_NEIGHBOR_SET $ap_to_report_neighbors $bss_neighbor_set_mac $ap_bss_neighbor_set_params" 1 >> $CONF_DIR/BeeRock_CMD.log
		fi

		## dual band tags support - init params after commit.
		wlan_tag_1_interface=""
		wlan_tag_2_interface=""
		interfaces_wlan_tags=""

		CONFIG_SM="not-started"
		#[ "$DEBUGGING" = "0" ] && rm $CONFIG_TMP
	fi
}

clish_add_vap()
{
	debug_print "clish_add_vap - Detected MBSSID test: Creating two new VAPs"
	if [ -z "$AP_IF_ACTIVE" ]; then
		error_print "Failed to add VAPs. No active interface set."
		return
	fi
	# Create two new VAPs
	clish -c "configure wlan" -c "start" -c "add vap $AP_IF_ACTIVE VAP1" -c "add vap $AP_IF_ACTIVE VAP2" -c "commit" 1>&2
}

clish_del_existing_vaps()
{
	local num_vaps=`iwconfig 2>/dev/null | grep '\(wlan0\.\|wlan2\.\|wlan4\.\)' -c`

	debug_print "clish_del_existing_vaps - Clearing VAPs after MBSSID test ; num_vaps = $num_vaps"

	if [ "$num_vaps" -gt "0" ]; then
		# Delete all VAPs
		vapnames=`iwconfig 2>/dev/null | grep '\(wlan0\.\|wlan2\.\|wlan4\.\)' | awk '{print $1}'`
		PREV_IFS=$IFS
		IFS=$DEFAULT_IFS

		local CMD_FILE="/tmp/sigma-cmds"
		echo "configure wlan" > $CMD_FILE
		echo "start" >> $CMD_FILE

		for vap in $vapnames; do
			debug_print "Deleting VAP: $vap"
			echo "del vap $vap" >> $CMD_FILE
		done

		echo "commit" >> $CMD_FILE
		echo "exit" >> $CMD_FILE
		clish $CMD_FILE > /dev/console
		rm -f $CMD_FILE

		IFS=$PREV_IFS
	fi
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

get_wlan_param()
{
	# command to read one parameter using the clish show wlan functionality
	# first parameter defines the object for the clish command: radio/ssid/ap
	# second parameter defines the TR-181 parameter to be returned in third parameter
	# example get_wlan_param "ssid" "BSSID" "bssid_param_val"
	clish -c "show wlan ${1} $ap_interface" > /tmp/clish_show_wlan_${1}
	local tmp
	if [ "${1}" = "ssid" ]; then
		tmp=$(cat /tmp/clish_show_wlan_${1} | grep -m2 "${2} : " | tail -n1)
	else
		tmp=$(cat /tmp/clish_show_wlan_${1} | grep -m1 "${2} : ")
	fi

	param_val="$(echo $tmp | cut -d ' ' -f 3)"

	#param_val=`awk -v var="${2}" '$0 ~ var {print $NF;}' /tmp/clish_show_wlan_${1}`
	#debug_print "${2}=$param_val"
	export ${3}="$param_val"
}

get_wlan_param_radio_all()
{
	debug_print "clish -c \"show wlan radio $ap_interface\" > /tmp/clish_show_wlan_radio_all"
	clish -c "show wlan radio $ap_interface" > /tmp/clish_show_wlan_radio_all
	local tmp=$(cat /tmp/clish_show_wlan_radio_all)
	debug_print "$tmp"

	tmp=$(cat /tmp/clish_show_wlan_radio_all | grep -m1 "Channel : ")
	param_val="$(echo $tmp | cut -d ' ' -f 3)"
	export ${1}="$param_val"

	tmp=$(cat /tmp/clish_show_wlan_radio_all | grep -m1 "AutoChannelEnable : ")
	param_val="$(echo $tmp | cut -d ' ' -f 3)"
	export ${2}="$param_val"

	param_val=`awk '$1=="OperatingStandards" {print $NF;}' /tmp/clish_show_wlan_radio_all`
	debug_print "Operating Standards=$param_val"
	export ${3}="$param_val"

	tmp=$(cat /tmp/clish_show_wlan_radio_all | grep -m1 "OperatingFrequencyBand : ")
	param_val="$(echo $tmp | cut -d ' ' -f 3)"
	export ${4}="$param_val"

	tmp=$(cat /tmp/clish_show_wlan_radio_all | grep -m1 "radioEnable : ")
	param_val="$(echo $tmp | cut -d ' ' -f 3)"
	export ${5}="$param_val"

	tmp=$(cat /tmp/clish_show_wlan_radio_all | grep -m1 "ExtensionChannel : ")
	param_val="$(echo $tmp | cut -d ' ' -f 3)"
	export ${6}="$param_val"

	tmp=$(cat /tmp/clish_show_wlan_radio_all | grep -m1 "MCS : ")
	param_val="$(echo $tmp | cut -d ' ' -f 3)"
	export ${7}="$param_val"

	tmp=$(cat /tmp/clish_show_wlan_radio_all | grep -m1 "GuardInterval : ")
	param_val="$(echo $tmp | cut -d ' ' -f 3)"
	export ${8}="$param_val"

	tmp=$(cat /tmp/clish_show_wlan_radio_all | grep -m1 "OperatingChannelBandwidth : ")
	param_val="$(echo $tmp | cut -d ' ' -f 3)"
	export ${9}="$param_val"

	tmp=$(cat /tmp/clish_show_wlan_radio_all | grep -m1 "RegulatoryDomain : ")
	param_val="$(echo $tmp | cut -d ' ' -f 3)"
	export ${10}="$param_val"
}

get_wlan_param_ap_all()
{
	debug_print "clish -c \"show wlan ap $ap_interface\" > /tmp/clish_show_wlan_ap_all"
	clish -c "show wlan ap $ap_interface" > /tmp/clish_show_wlan_ap_all
	local tmp=$(cat /tmp/clish_show_wlan_ap_all)
	debug_print "$tmp"

	tmp=$(cat /tmp/clish_show_wlan_ap_all | grep -m1 "WMMEnable : ")
	param_val="$(echo $tmp | cut -d ' ' -f 3)"
	export ${1}="$param_val"

	tmp=$(cat /tmp/clish_show_wlan_ap_all | grep -m1 "UAPSDEnable : ")
	param_val="$(echo $tmp | cut -d ' ' -f 3)"
	export ${2}="$param_val"
}

get_wlan_param_ap_security_all()
{
	debug_print "clish -c \"show wlan ap security $ap_interface\" > /tmp/clish_show_wlan_ap_sec_all"

	local tmp_ap_interface
	tmp_ap_interface=${ap_interface/","/}
	if [ "$tmp_ap_interface" != "$ap_interface" ]; then
		clish -c "show wlan ap security wlan0" > /tmp/clish_show_wlan_ap_sec_all
		clish -c "show wlan ap security wlan2" > /tmp/clish_show_wlan_ap_sec_all
	else
		clish -c "show wlan ap security $ap_interface" > /tmp/clish_show_wlan_ap_sec_all
	fi

	local tmp=$(cat /tmp/clish_show_wlan_ap_sec_all)
	debug_print "$tmp"

	tmp=$(cat /tmp/clish_show_wlan_ap_sec_all | grep -m1 "SSID : ")
	param_val="$(echo $tmp | cut -d ' ' -f 3)"
	export ${1}="$param_val"

	tmp=$(cat /tmp/clish_show_wlan_ap_sec_all | grep -m1 "ModeEnabled : ")
	param_val="$(echo $tmp | cut -d ' ' -f 3)"
	export ${2}="$param_val"

	tmp=$(cat /tmp/clish_show_wlan_ap_sec_all | grep -m1 "WEPKey : ")
	param_val="$(echo $tmp | cut -d ' ' -f 3)"
	export ${3}="$param_val"

	tmp=$(cat /tmp/clish_show_wlan_ap_sec_all | grep -m1 "KeyPassphrase : ")
	param_val="$(echo $tmp | cut -d ' ' -f 3)"
	export ${4}="$param_val"

	tmp=$(cat /tmp/clish_show_wlan_ap_sec_all | grep -m1 "wpsEnable : ")
	param_val="$(echo $tmp | cut -d ' ' -f 3)"
	export ${5}="$param_val"

	# TODO: wps parameter? ap_wps_enrollee, ap_wps_int_reg, ap_wps_proxy
}

get_wlan_param_qos_all()
{
	debug_print "clish -c \"show wlan ap ac $ap_interface\" > /tmp/clish_show_wlan_ap_ac_all"
	clish -c "show wlan ap ac $ap_interface" > /tmp/clish_show_wlan_ap_ac_all
	local tmp=$(cat /tmp/clish_show_wlan_ap_ac_all)

	# TODO: FIX BUG HERE:
	debug_print "$tmp"

	"ap_cwmin_vo" "ap_cwmax_vo" "ap_aifs_vo" "ap_txop_vo" "ap_acm_val_vo"
	tmp=$(cat /tmp/clish_show_wlan_ap_ac_all | grep -m1 "ECWMin_VO : ")
	param_val="$(echo $tmp | cut -d ' ' -f 3)"
	export ${1}="$param_val"

	tmp=$(cat /tmp/clish_show_wlan_ap_ac_all | grep -m1 "ECWMax_VO : ")
	param_val="$(echo $tmp | cut -d ' ' -f 3)"
	export ${2}="$param_val"

	tmp=$(cat /tmp/clish_show_wlan_ap_ac_all | grep -m1 "AIFSN_VO : ")
	param_val="$(echo $tmp | cut -d ' ' -f 3)"
	export ${3}="$param_val"

	tmp=$(cat /tmp/clish_show_wlan_ap_ac_all | grep -m1 "TxOpMax_VO : ")
	param_val="$(echo $tmp | cut -d ' ' -f 3)"
	export ${4}="$param_val"

	tmp=$(cat /tmp/clish_show_wlan_ap_ac_all | grep -m1 "AckPolicy_VO : ")
	param_val="$(echo $tmp | cut -d ' ' -f 3)"
	export ${5}="$param_val"

	tmp=$(cat /tmp/clish_show_wlan_ap_ac_all | grep -m1 "ECWMin_VI : ")
	param_val="$(echo $tmp | cut -d ' ' -f 3)"
	export ${6}="$param_val"

	tmp=$(cat /tmp/clish_show_wlan_ap_ac_all | grep -m1 "ECWMax_VI : ")
	param_val="$(echo $tmp | cut -d ' ' -f 3)"
	export ${7}="$param_val"

	tmp=$(cat /tmp/clish_show_wlan_ap_ac_all | grep -m1 "AIFSN_VI : ")
	param_val="$(echo $tmp | cut -d ' ' -f 3)"
	export ${8}="$param_val"

	tmp=$(cat /tmp/clish_show_wlan_ap_ac_all | grep -m1 "TxOpMax_VI : ")
	param_val="$(echo $tmp | cut -d ' ' -f 3)"
	export ${9}="$param_val"

	tmp=$(cat /tmp/clish_show_wlan_ap_ac_all | grep -m1 "AckPolicy_VI : ")
	param_val="$(echo $tmp | cut -d ' ' -f 3)"
	export ${10}="$param_val"

	tmp=$(cat /tmp/clish_show_wlan_ap_ac_all | grep -m1 "ECWMin_BK : ")
	param_val="$(echo $tmp | cut -d ' ' -f 3)"
	export ${11}="$param_val"

	tmp=$(cat /tmp/clish_show_wlan_ap_ac_all | grep -m1 "ECWMax_BK : ")
	param_val="$(echo $tmp | cut -d ' ' -f 3)"
	export ${12}="$param_val"

	tmp=$(cat /tmp/clish_show_wlan_ap_ac_all | grep -m1 "AIFSN_BK : ")
	param_val="$(echo $tmp | cut -d ' ' -f 3)"
	export ${13}="$param_val"

	tmp=$(cat /tmp/clish_show_wlan_ap_ac_all | grep -m1 "TxOpMax_BK : ")
	param_val="$(echo $tmp | cut -d ' ' -f 3)"
	export ${14}="$param_val"

	tmp=$(cat /tmp/clish_show_wlan_ap_ac_all | grep -m1 "AckPolicy_BK : ")
	param_val="$(echo $tmp | cut -d ' ' -f 3)"
	export ${15}="$param_val"

	tmp=$(cat /tmp/clish_show_wlan_ap_ac_all | grep -m1 "ECWMin_BE : ")
	param_val="$(echo $tmp | cut -d ' ' -f 3)"
	export ${16}="$param_val"

	tmp=$(cat /tmp/clish_show_wlan_ap_ac_all | grep -m1 "ECWMax_BE : ")
	param_val="$(echo $tmp | cut -d ' ' -f 3)"
	export ${17}="$param_val"

	tmp=$(cat /tmp/clish_show_wlan_ap_ac_all | grep -m1 "AIFSN_BE : ")
	param_val="$(echo $tmp | cut -d ' ' -f 3)"
	export ${18}="$param_val"

	tmp=$(cat /tmp/clish_show_wlan_ap_ac_all | grep -m1 "TxOpMax_BE : ")
	param_val="$(echo $tmp | cut -d ' ' -f 3)"
	export ${19}="$param_val"

	tmp=$(cat /tmp/clish_show_wlan_ap_ac_all | grep -m1 "AckPolicy_BE : ")
	param_val="$(echo $tmp | cut -d ' ' -f 3)"
	export ${20}="$param_val"
}

get_interface_name()
{
	debug_print "get_interface_name():start"
	ap_param_interface_found=""
	ap_param_channel_found=""
	ap_param_wlan_tag_dc_found=""
	is_dual_band=""
	ap_tmp_param=""

	for ap_param in $*; do
		upper "$ap_param" ap_param
		debug_print "ap_param:$ap_param"
		if [ "$ap_param_interface_found" = "1" ]; then
			# TODO: Fix hard-coded interface name based on band - won't work on all platforms
			if [ "$ap_param" = "24G" ]; then
				ap_interface="wlan0"
			elif [ "$ap_param" = "5G" ]; then
				ap_interface="wlan2"
				[ "$SINGLE_INTF_SUPPORT" = "1" ] && ap_interface="wlan0"
			elif [ "$ap_param" = "wlan0" ]; then
				ap_interface="wlan0"
			elif [ "$ap_param" = "wlan2" ]; then
				ap_interface="wlan2"
			else
				error_print "Unsupported value - ap_param:$ap_param"
				send_invalid ",errorCode,1"
				return
			fi
			AP_IF_ACTIVE="$ap_interface"
			break
		elif [ "$ap_param_channel_found" = "1" ]; then
			ap_tmp_param=${ap_param/";"/}
			[ "$ap_tmp_param" != "$ap_param" ] && is_dual_band="1"
			if [ "$is_dual_band" = "1" ]; then
				ap_param=${ap_param//;/,}
				for tmp_param in $ap_param
				do
					if [ "$tmp_param" -ge "36" ]; then
						[ "$ap_interface" = "" ] && ap_interface="wlan2" || ap_interface="$ap_interface,wlan2"
					elif [ "$tmp_param" -eq "6" ] || [ "$tmp_param" -eq "1" ] || [ "$tmp_param" -eq "11" ]; then
						[ "$ap_interface" = "" ] && ap_interface="wlan0" || ap_interface="$ap_interface,wlan0"
					fi
				done
			else
				if [ "$ap_param" -ge "36" ]; then
					ap_interface="wlan2"
					[ "$SINGLE_INTF_SUPPORT" = "1" ] && ap_interface="wlan0"
				elif [ "$ap_param" -eq "6" ] || [ "$ap_param" -eq "1" ] || [ "$ap_param" -eq "11" ]; then
					ap_interface="wlan0";
				fi
			fi
			AP_IF_ACTIVE="$ap_interface"
			break
		## dual band tags support - we are not breaking , if channel or interface recieved used them for set interface.
		elif [ "$ap_param_wlan_tag_dc_found" = "1" ]; then
				info_print "get_radio_interface_name:WLAN_TAG received $ap_param"
				[ "$ap_param" = "1" ] && ap_interface="wlan2" && ap_param_wlan_tag_dc_found="1"
				[ "$ap_param" = "2" ] && ap_interface="wlan0" && ap_param_wlan_tag_dc_found="2"
		fi
		case "$ap_param" in
			INTERFACE)
				debug_print "ap_param:$ap_param"
				ap_param_interface_found="1"
			;;
			CHANNEL)
				debug_print "ap_param:$ap_param"
				ap_param_channel_found="1"
			;;
			WLAN_TAG)
				debug_print "ap_param:$ap_param"
				ap_param_wlan_tag_dc_found="1"
			;;
			NAME)
				shift
				upper "$1" ap_name
				debug_print "ap_name:$ap_name"
			;;
			*)
				debug_print "default case: ap_param:$ap_param"
			;;
		esac
	done

	for ap_param in $*; do
		upper "$ap_param" ap_param
		if [ "$ap_param_wlan_tag_found" = "1" ]; then
			#debug_print "ap_param:$ap_param"

			# Add VAP workaround:
			# Check number of VAPs, if less than number needed add a new VAP.
			# Super-workaround: for optimization, use iwconfig instead of API to find number of VAP, and always add two new VAPs, as needed in MBO testplan
			# TODO: Possible bug: requires VAPs to be up, or the number of VAPs will be wrong and additional VAPs will be created.
			# TODO: Is this the best place to add VAPs? before config starts? probably yes
			num_vaps=`iwconfig 2>/dev/null | grep $AP_IF_ACTIVE -c`
			if [ "$num_vaps" -lt "$ap_param" ]; then
				clish_add_vap
			fi

			if [ "$ap_param" -ge "2" ]; then
				let vap_index=$ap_param-2
				if [ "$ap_interface" != "" ]; then
					ap_interface="$ap_interface.$vap_index"
				else
					ap_interface="$AP_IF_ACTIVE.$vap_index"
				fi
				debug_print "vap_index:$vap_index"
			else
				if [ "$ap_interface" = "" ]; then
					ap_interface="$AP_IF_ACTIVE"
				fi
			fi
#			if [ "$ap_param" = "2" ]; then
#				if [ "$ap_interface" != "" ]; then
#					ap_interface="$ap_interface.0";
#				else
#					ap_interface="$AP_IF_ACTIVE.0";
#				fi
#			elif [ "$ap_param" = "3" ]; then
#				if [ "$ap_interface" != "" ]; then
#					ap_interface="$ap_interface.1";
#				else
#					ap_interface="$AP_IF_ACTIVE.1";
#				fi
#			fi
			break
		fi
		case "$ap_param" in
			WLAN_TAG)
				debug_print "ap_param:$ap_param"
			;;
		esac
	done

	if [ "$ap_interface" = "" ]; then
		ap_interface="$AP_IF_ACTIVE"
		local tmp_ap_interface
		tmp_ap_interface=${ap_interface/","/}
		[ "$tmp_ap_interface" != "$ap_interface" ] && is_dual_band="1"
	fi

	## dual band tags support - update the wlan_tag 1 or 2 according to the final interface.
	[ "$ap_param_wlan_tag_dc_found" != "" ] && eval "wlan_tag_${ap_param_wlan_tag_dc_found}_interface=$ap_radio_interface"

	info_print "ap_interface:$ap_interface"
	debug_print "get_interface_name():end"
}

get_radio_interface_name()
{
	debug_print "get_radio_interface_name():start"
	ap_param_interface_found=""
	ap_param_channel_found=""
	ap_param_wlan_tag_dc_found=""
	is_dual_band=""
	ap_tmp_param=""

	# preset ap_radio_interface with active interface from previous call in case function is called without required parameter
	for ap_param in $*; do
		upper "$ap_param" ap_param
		debug_print "ap_param:$ap_param"
		if [ "$ap_param_interface_found" = "1" ]; then
			if [ "$ap_param" = "24G" ]; then
				ap_radio_interface="wlan0"
			elif [ "$ap_param" = "5G" ]; then
				ap_radio_interface="wlan2"
				[ "$SINGLE_INTF_SUPPORT" = "1" ] && ap_radio_interface="wlan0"
			fi
		break
		elif [ "$ap_param_channel_found" = "1" ]; then
			ap_tmp_param=${ap_param/";"/}
			[ "$ap_tmp_param" != "$ap_param" ] && is_dual_band="1"
			if [ "$is_dual_band" = "1" ]; then
				ap_param=${ap_param//;/,}
				for tmp_param in $ap_param
				do
					if [ "$tmp_param" -ge "36" ]; then
						[ "$ap_radio_interface" = "" ] && ap_radio_interface="wlan2" || ap_radio_interface="$ap_radio_interface,wlan2"
					elif [ "$tmp_param" -eq "6" ] || [ "$tmp_param" -eq "1" ] || [ "$tmp_param" -eq "11" ]; then
						[ "$ap_radio_interface" = "" ] && ap_radio_interface="wlan0" || ap_radio_interface="$ap_radio_interface,wlan0"
					fi
				done
			else
				if [ "$ap_param" -ge "36" ]; then
					ap_radio_interface="wlan2"
					[ "$SINGLE_INTF_SUPPORT" = "1" ] && ap_radio_interface="wlan0"
				elif [ "$ap_param" -eq "6" ] || [ "$ap_param" -eq "1" ] || [ "$ap_param" -eq "11" ]; then
					ap_radio_interface="wlan0"
				fi
			fi
		break
		## dual band tags support - we are not breaking , if channel or interface recieved used them for set interface.
		elif [ "$ap_param_wlan_tag_dc_found" = "1" ]; then
				 info_print "get_radio_interface_name:WLAN_TAG received $ap_param"
				 [ "$ap_param" = "1" ] && ap_radio_interface="wlan2" && ap_param_wlan_tag_dc_found="1"
				 [ "$ap_param" = "2" ] && ap_radio_interface="wlan0" && ap_param_wlan_tag_dc_found="2"
		fi
		case "$ap_param" in
			INTERFACE)
				debug_print "ap_param:$ap_param"
				ap_param_interface_found="1"
			;;
			CHANNEL)
				debug_print "ap_param:$ap_param"
				ap_param_channel_found="1"
			;;
			WLAN_TAG)
				debug_print "ap_param:$ap_param"
				ap_param_wlan_tag_dc_found="1"
			;;
			NAME)
				shift
				ap_name="$1"
				upper "$ap_name" ap_name
				debug_print "ap_name:$ap_name"
			;;
			*)
				debug_print "default case: ap_param:$ap_param"
			;;
		esac
	done

	if [ "$ap_radio_interface" = "" ]; then
		ap_radio_interface="$AP_IF_ACTIVE"
		local tmp_ap_interface
		tmp_ap_interface=${ap_radio_interface/","/}
		[ "$tmp_ap_interface" != "$ap_radio_interface" ] && is_dual_band="1"
	fi
	## dual band tags support - update the wlan_tag 1 or 2 according to the final interface.
	[ "$ap_param_wlan_tag_dc_found" != "" ] && eval "wlan_tag_${ap_param_wlan_tag_dc_found}_interface=$ap_radio_interface"

	info_print "ap_radio_interface:$ap_radio_interface"
	debug_print "get_radio_interface_name():end"
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
	local tc_name count=0 vap_index local_mcs spatial_rx_stream_number spatial_tx_stream_number

	debug_print "ap_set_wireless():start"
	ap_name="INTEL"
	# default to wlan0 in case no interface parameter is given
	ap_interface=""
	ap_cellular_cap_pref=""
	ap_commit_now="0"
	IFS=" "
	# checking for interface parameter (might fail if some value is also "interface")
	send_running
	get_radio_interface_name $@
	get_interface_name $@

	stop_wlan

	# TODO: Is reading existing info needed??? Introduces bug of always setting params that weren't changed
	#get_wlan_param_radio_all "ap_channel" "ap_auto_channel" "ap_mode_val" "ap_band_val" "ap_radio" "ap_offset_val" "global_mcs_fixedrate" "ap_sgi20_val" "ap_width_val" "ap_region"
	# not supported by clish
	###ap_bcnint=100
	###ap_rts=2347
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
			ap_name=$1
		;;
		INTERFACE)
			# skip since it was read in loop before
			shift
		;;
		WLAN_TAG)
			shift
			ap_wlan_tag=$1
			[ "$ap_wlan_tag" = "3" ] && touch $CONFIG_ADD_WLAN2_VAP
			[ "$ap_wlan_tag" = "4" ] && touch $CONFIG_ADD_WLAN0_VAP
			# skip as it is determined in get_interface_name
			# TODO: Add handling of VAPs here?
		;;
		SSID)
			shift
			debug_print "ap_ssid:$ap_ssid"
			if [ -z $global_num_non_tx_bss ]; then
				ap_ssid=$1
				export glob_ssid="$1"  # use export in order to be allow usage from the SMD (sigmaManagerDaemon)
				if [ -e $CONFIG_ADD_WLAN2_VAP ] || [ -e $CONFIG_ADD_WLAN0_VAP ]; then
					ap_ssid=""
				fi

				# in case of the below tests (4.68.1 & 5.73.1), setting of OFDMA to DL is missing - set it here!
				tc_name=`get_tc_name $glob_ssid`
				if [ "$tc_name" = "4.68.1" ] || [ "$tc_name" = "5.73.1" ]; then
					info_print "tc_name = $tc_name ==> set ap_ofdma='dl'"
					ap_ofdma="dl"
				fi
			else
				ap_ssid_non_tx_bss_index="$1"
				debug_print "ap_ssid_non_tx_bss_index:$ap_ssid_non_tx_bss_index"
			fi
		;;
		CHANNEL)
			shift
			ap_channel=$1
			ap_auto_channel=0
		;;
		MODE)
			shift
			lower "$1" ap_mode
		;;
		WME)
			shift
			lower "$1" ap_wme
		;;
		WMMPS)
			shift
			lower "$1" ap_wmmps
		;;
		RTS)
			shift
			ap_rts=$1
		;;
		FRGMNT)
			shift
			ap_frgmnt=$1
		;;
		FRGMNTSUPPORT)
			# do nothing
			shift
		;;
		PWRSAVE)
			# do nothing
			shift
			ap_pwrsave=$1
		;;
		BCNINT)
			shift
			ap_bcnint=$1
		;;
		RADIO)
			shift
			lower "$1" ap_radio
		;;
		P2PMGMTBIT)
			# do nothing
			shift
			ap_p2pbit=$1
		;;
		CHANNELUSAGE)
			# do nothing
			shift
			ap_channelusage=$1
		;;
		TDLSPROHIBIT)
			# do nothing
			shift
			ap_tdls=$1
		;;
		TDLSCHSWITCHPROHIBIT)
			# do nothing
			shift
			ap_tdlschannel=$1
		;;
		WIDTH)
			shift
			lower "$1" ap_width
		;;
		OFFSET)
			shift
			ap_offset=$1
		;;
		COUNTRY) ## NB: Extension parameter
			shift
			ap_region=$1
		;;
		COUNTRYCODE) ## NB: Extension parameter
			shift
			ap_region=$1
		;;
		REG_DOMAIN) ## NB: Extension parameter
			# do nothing
			shift
		;;
		CELLULAR_CAP_PREF)
			shift
			ap_cellular_cap_pref=$1
		;;
		GAS_CB_DELAY)
			shift
			ap_gas_cb_delay=$1
		;;
		DOMAIN)
			shift
			ap_domain=$1
		;;
		FT_OA)
			shift
			ap_ft_oa=$1
		;;
		PROGRAM)
			shift
			ap_program=$1
		;;
		OCESUPPORT)
			shift
			ap_oce_support=$1
		;;
		FILSDSCV)
			shift
			ap_fils_dscv=$1
		;;
		FILSDSCVINTERVAL)
			shift
			ap_fils_dscv_interval=$1
		;;
		BROADCASTSSID)
			shift
			ap_broadcast_ssid=$1
		 ;;
		FILSHLP)
			shift
			ap_filshlp=$1
		;;
		NAIREALM)
			shift
			ap_nairealm=$1
		;;
		RNR)
			shift
			ap_rnr=$1
		;;
		DEAUTHDISASSOCTX)
			shift
			ap_deauth_disassoc_tx=$1
		;;
		BLESTACOUNT)
			shift
			ap_ble_sta_count=$1
		;;
		BLECHANNELUTIL)
			shift
			ap_ble_channel_util=$1
		;;
		BLEAVAILADMINCAP)
			shift
			ap_ble_avail_admin_cap=$1
		;;
		AIRTIMEFRACT)
			shift
			ap_air_time_fract=$1
		;;
		DATAPPDUDURATION)
			shift
			ap_data_ppdu_duration=$1
		;;
		DHCPSERVIPADDR)
			shift
			dhcp_serv_ip_addr=$1
		;;
		NSS_MCS_CAP)
			shift
			ap_nss_mcs_cap=$1
		;;
		FILSCAP)
			# do nothing
			shift
		;;
		BAWINSIZE)
			shift
			oce_ba_win_size=$1
		;;
		DATAFORMAT)
			shift
			oce_data_format=$1
		;;
		ESP_IE)
			shift
			oce_esp_ie=$1
		;;
		AMPDU)
			shift
			lower "$1" ap_ampdu
		;;
		AMSDU)
			shift
			lower "$1" ap_amsdu
		;;
		ADDBA_REJECT)
			shift
			lower "$1" ap_addba_reject
		;;
		MCS_FIXEDRATE)
			shift
			global_mcs_fixedrate=$1
		;;
		SPATIAL_RX_STREAM)
			shift
			ap_spatial_rx_stream=$1
		;;
		SPATIAL_TX_STREAM)
			shift
			ap_spatial_tx_stream=$1
		;;
		BCC)
			shift
			lower $1 ap_bcc
		;;
		LDPC)
			shift
			lower $1 ap_ldpc
		;;
		NOACK)
			# param not supported
			shift
			ap_no_ack=$1
		;;
		OFDMA)
			shift
			lower "$1" ap_ofdma
			if [ "$ap_ofdma" = "dl-20and80" ]; then
				ap_ofdma="dl"
			fi
		;;
		PPDUTXTYPE)
			# do nothing
			shift
		;;
		SPECTRUMMGT)
			# do nothing
			shift
		;;
		NUMUSERSOFDMA)
			shift
			lower "$1" ap_num_users_ofdma
		;;
		TXBF)
			shift
			lower "$1" ap_tx_bf
		;;
		NUMSOUNDDIM)
			shift
			lower "$1" ap_num_sound_dim
		;;
		VHT_CAP_160_CW)
			# do nothing
			shift
			# TODO: currently this command is passive.
			#lower "$1" ap_vht_cap_160_cw
		;;
		VHT_EXTNSS)
			shift
			lower "$1" ap_vht_extnss
		;;
		MU_EDCA)
			shift
			lower "$1" ap_mu_edca
		;;
		ACKTYPE)
			# do nothing
			shift
		;;
		MU_TXBF)
			shift
			lower "$1" global_mu_txbf
		;;
		TRIG_MAC_PADDING_DUR)
			# do nothing default is 16usec
			shift
		;;
		BA_PARAM_AMSDU_SUPPORT)
			# same implementation as AMSDU - need to review for R2(AH).
			shift
			lower "$1" ap_ba_param_amsdu_support
			ap_amsdu="$ap_ba_param_amsdu_support"
		;;
		ADDBAREQ_BUFSIZE)
			shift
			lower "$1" ap_addbareq_bufsize
		;;
		ADDBARESP_BUFSIZE)
			shift
			lower "$1" ap_addbaresp_bufsize
		;;
		BA_RECV_STATUS)
			# do nothing
			shift
		;;
		OMCONTROL)
			shift
			lower "$1" ap_omcontrol
		;;
		MIMO)
			shift
			lower "$1" ap_mimo
		;;
		TWT_RESPSUPPORT)
			shift
			lower "$1" ap_twt_respsupport
		;;
		SRCTRL_SRVALUE15ALLOWED)
			shift
			lower "$1" ap_srctrl_srvalue15allowed
		;;
		MINMPDUSTARTSPACING)
			shift
			ap_min_mpdu_start_spacing=$1
		;;
		FT_BSS_LIST)
			# do nothing
			shift
		;;
		MBSSID)
			shift
			lower "$1" global_ap_mbssid
		;;
		NUMNONTXBSS)
			shift
			([ $1 -lt 1 ] || [ $1 -gt 7 ]) && error_print "NumNonTxBSS invalid value '$1'" && send_invalid ",errorCode,95" && return
			global_num_non_tx_bss=$1
			debug_print "global_num_non_tx_bss:$global_num_non_tx_bss"
		;;
		NONTXBSSINDEX)
			shift
			([ $1 -lt 1 ] || [ $1 -gt 8 ]) && error_print "NonTxBSSIndex invalid value '$1'" && send_invalid ",errorCode,96" && return
			ap_non_tx_bss_index=$1
		;;
		HE_TXOPDURRTSTHR)
			shift
			lower "$1" ap_he_txop_dur_rts_thr_conf
			if [ "$ap_he_txop_dur_rts_thr_conf" = "enable" ]; then
				ap_he_txop_dur_rts_thr_conf=10
			elif [ "$ap_he_txop_dur_rts_thr_conf" = "disable" ]; then
				ap_he_txop_dur_rts_thr_conf=1023
			else
				ap_he_txop_dur_rts_thr_conf=""
			fi
			# We support here "Enable" & "Disable" only (tests # 4.66.1, 5.71.1)
			# 1. "Enable" - any value between 0..1022
			# 2. "Disable" - 1023
		;;
		*)
			error_print "while loop error $1"
			send_invalid ",errorCode,100"
			return
		;;
		esac
		shift
	done

	# Mode / Band selection
	if [ "$ap_mode" != "" ]; then
		case "$ap_mode" in
		11a)
			ap_mode_val="a"
			ap_band_val="5GHz"
		;;
		11b)
			ap_mode_val="b"
			ap_band_val="2.4GHz"
		;;
		11g)
			ap_mode_val="g"
			ap_band_val="2.4GHz"
		;;
		11bg)
			ap_mode_val="b,g"
			ap_band_val="2.4GHz"
		;;
		11bgn)
			ap_mode_val="b,g,n"
			ap_band_val="2.4GHz"
		;;
		11ng)
			ap_mode_val="b,g,n"
			ap_band_val="2.4GHz"
		;;
		11na)
			ap_mode_val="a,n"
			ap_band_val="5GHz"
		;;
		11an)
			ap_mode_val="a,n"
			ap_band_val="5GHz"
		;;
		11ac)
			ap_mode_val="a,n,ac"
			ap_band_val="5GHz"
		;;
		11anac)
			ap_mode_val="a,n,ac"
			ap_band_val="5GHz"
		;;
		11ax)
			ap_mode_val="ax"
			# The band value can be 2.4GHz or 5GHz, and is determined below, considering the channel value.
			# The mode value combination is determined below, considering the channel value.
		;;
		"11ac;11ng")
			ap_mode_val="a.n.ac;b.g.n"
			ap_band_val="5GHz;2.4GHz"
		;;
		*)
			error_print "Unsupported value - ap_mode:$ap_mode"
			send_error ",errorCode,105"
			return
		;;
		esac
	fi

	if [ "$ap_wme" != "" ]; then
		if [ "$ap_wme" = "on" ]; then
			ap_wme_val="1"
		elif [ "$ap_wme" = "off" ]; then
			ap_wme_val="0"
		else
			error_print "Unsupported value - ap_wme:$ap_wme"
			send_error ",errorCode,110"
			return
		fi
	fi

	if [ "$ap_wmmps" != "" ]; then
		if [ "$ap_wmmps" = "on" ]; then
			ap_wmmps_val="1"
		elif [ "$ap_wmmps" = "off" ]; then
			ap_wmmps_val="0"
		else
			error_print "Unsupported value - ap_wmmps:$ap_wmmps"
			send_error ",errorCode,115"
			return
		fi
	fi

	if [ "$ap_region" != "" ]; then
		# NOTE: Do not remove the space below at the end of the string!!! TR-181 specifies 2-character country code + (space/O/I)
		# Meaning: "US " (all environment) "USO" (outside) "USI" (inside)
		ap_region="${ap_region} "
	fi

	# JIRA WLANRTSYS-9943
	if [ "$ap_vht_extnss" != "" ]; then
		if [ "$ap_vht_extnss" = "eq0" ]; then
			ap_width="40"  # "40MHz", will set he_op_vht_channel_width to '0'
		elif [ "$ap_vht_extnss" = "eq1" ]; then
			ap_width="80"  # "80MHz", will set he_op_vht_channel_width to '1'
		else
			error_print "Unsupported value - ap_vht_extnss:$ap_vht_extnss"
			send_error ",errorCode,116"
			return
		fi
	fi

	if [ "$ap_width" != "" ]; then
		if [ "$ap_width" = "20" ]; then
			ap_width_val="20MHz"
		elif [ "$ap_width" = "40" ]; then
			ap_width_val="40MHz"
		elif [ "$ap_width" = "80" ]; then
			ap_width_val="80MHz"
		elif [ "$ap_width" = "160" ]; then
			ap_width_val="160MHz"
		else
			debug_print "ap_width:$ap_width"
			ap_width_val="Auto"
		fi
	fi

	if [ "$ap_no_ack" != "" ]; then
		error_print "Unsupported value - ap_no_ack:$ap_no_ack"
		send_error ",errorCode,120"
	fi

	# start a new clish command
	radio_params=""
	if [ "$ap_channel" != "" ]; then
		radio_params=$radio_params" Channel $ap_channel"
		if [ "$ap_mode_val" = "ax" ]; then
			if [ "$ap_channel" -ge "36" ]; then
				ap_mode_val="a,n,ac,ax"
				ap_band_val="5GHz"
			else
				ap_mode_val="b,g,n,ax"
				ap_band_val="2.4GHz"
			fi
		fi
	fi

	if [ "$ap_mode_val" != "" ]; then
		radio_params=$radio_params" OperatingStandards $ap_mode_val"
	fi

	if [ "$ap_deauth_disassoc_tx" != "" ]; then
		[ "$ap_deauth_disassoc_tx" = "enable" ] && ap_radio="on"
		[ "$ap_deauth_disassoc_tx" = "disable" ] && ap_radio="off"
	fi

	if [ "$ap_radio" != "" ]; then
		ap_commit_now="1"
		if [ "$ap_radio" = "on" ]; then
			radio_params=$radio_params" radioEnable 1"
			radio_on=true
		else
			radio_params=$radio_params" radioEnable 0"
			# DO commit without commit NO_radio_on
			radio_off=true
		fi
	fi

	if [ "$ap_auto_channel" != "" ]; then
		radio_params=$radio_params" AutoChannelEnable $ap_auto_channel"
	fi

	if [ "$ap_band_val" != "" ]; then
		radio_params=$radio_params" OperatingFrequencyBand $ap_band_val"
	fi

	if [ "$ap_width_val" != "" ]; then
		radio_params=$radio_params" OperatingChannelBandwidth $ap_width_val"
		radio_params=$radio_params" WaveFRBandwidth $ap_width_val"

		# Only in 160 MHz / 5GHz, change channel check time to 1 sec to save configuration time.
		[ "$ap_width_val" = "160MHz" ] && [ "$ap_interface" = "wlan2" ] && iwpriv $ap_interface s11hChCheckTime 1

		# if static plan is on, turn it off, since we are going to change band (SMD will turn it back on).
		ap_spenable=`get_plan_enable $ap_interface`
		[ "$ap_spenable" = "1" ] && radio_params=$radio_params" WaveSPEnable 0"

		[ "$ap_width_val" = "20MHz" ] && ap_txop_com_start_bw_limit=0
		[ "$ap_width_val" = "40MHz" ] && ap_txop_com_start_bw_limit=1
		[ "$ap_width_val" = "80MHz" ] && ap_txop_com_start_bw_limit=2
		[ "$ap_width_val" = "160MHz" ] && ap_txop_com_start_bw_limit=3
		radio_params=$radio_params" WaveSPTxopComStartBwLimit ${ap_txop_com_start_bw_limit}"
		touch $SIGMA_SMD_OFDMA_REPLAN
	fi

	if [ "$ap_bcnint" != "" ]; then
		radio_params=$radio_params" BeaconPeriod $ap_bcnint"
	fi

	if [ "$ap_ofdma" != "" ]; then
		# JIRA WLANRTSYS-9736: in case of the below tests (4.68.1 & 5.73.1), setting of OFDMA to DL, even if the test was setting it as UL
		tc_name=`get_tc_name $glob_ssid`
		if [ "$ap_ofdma" != "dl" ] && ([ "$tc_name" = "4.68.1" ] || [ "$tc_name" = "5.73.1" ]); then
			info_print "tc_name = $tc_name (ap_ofdma = $ap_ofdma) ==> overwrite ap_ofdma, set it to 'dl'"
			ap_ofdma="dl"
		fi

		if [ "$ap_ofdma" = "dl" ] || [ "$ap_ofdma" = "ul" ]; then
			# TODO: Consider to move to clish_config_commit func and set a flag here.
			[ -z "$(pgrep -f sigmaManagerDaemon.sh)" ] && /opt/lantiq/wave/scripts/sigmaManagerDaemon.sh &

			if [ "$ap_ofdma" = "dl" ]; then
				glob_ofdma_phase_format=0
			elif [ "$ap_ofdma" = "ul" ]; then
				glob_ofdma_phase_format=1 
				radio_params=$radio_params" WaveSPSequenceType 5" # HE_MU_UL

				# in case of the below tests (4.56.1 & 5.60.1), set WaveSPDlComNumberOfPhaseRepetitions to 2
				if [ "$tc_name" = "4.56.1" ]; then
					radio_params=$radio_params" WaveSPDlComNumberOfPhaseRepetitions 3"
				elif [ "$tc_name" = "5.60.1" ]; then
					radio_params=$radio_params" WaveSPDlComNumberOfPhaseRepetitions 2"
				else
					# regular case
					radio_params=$radio_params" WaveSPDlComNumberOfPhaseRepetitions 1"
				fi
			fi
			radio_params=$radio_params" WaveSPDlComPhasesFormat ${glob_ofdma_phase_format}"
			if [ "$ap_num_users_ofdma" != "" ]; then
				radio_params=$radio_params" WaveSPDlComNumOfParticipatingStations ${ap_num_users_ofdma}"
			else
				if [ "$glob_ssid" != "" ]; then
					# check if num_of_users can be obtained from the predefined list (by test plan)
					# TODO: this code should be removed in the future, after ucc scripts will configure the num_of_users in every TC
					ap_num_users_ofdma=`get_nof_sta_per_he_test_case $glob_ssid`
					[ "$ap_num_users_ofdma" != "0" ] && radio_params=$radio_params" WaveSPDlComNumOfParticipatingStations ${ap_num_users_ofdma}"
				fi
			fi
		else
			error_print "Unsupported value - ap_ofdma:$ap_ofdma"
			send_error ",errorCode,150"
			return
		fi

		# set fixed rate in OFDMA MU only
		# TODO: replace this temporary command with clish configuration (through DB)
		info_print "iwpriv $ap_interface sDoSimpleCLI 70 1"
		iwpriv $ap_interface sDoSimpleCLI 70 1

		# set AGER=OFF for OFDMA MU only
		# TODO: replace this temporary command with clish configuration (through DB)
		#info_print "iwpriv $ap_interface sPdThresh 0 0 0"
		#iwpriv $ap_interface sPdThresh 0 0 0
	fi

	# Set coding (LDPC/BCC) for DL
	if [ "$ap_ldpc" != "" ] || [ "$ap_bcc" != "" ]; then
		debug_print "ap_ldpc:$ap_ldpc ap_bcc:$ap_bcc"
		if [ "$ap_ldpc" = "enable" ] && [ "$ap_bcc" = "enable" ]; then
			error_print "Unsupported value - ap_ldpc:$ap_ldpc ap_bcc:$ap_bcc"
			send_error ",errorCode,125"
			return
		elif [ "$ap_ldpc" = "disable" ] && [ "$ap_bcc" = "disable" ]; then
			error_print "Unsupported value - ap_ldpc:$ap_ldpc ap_bcc:$ap_bcc"
			send_error ",errorCode,130"
			return
		fi

		if [ "$ap_ldpc" = "disable" ]; then ap_bcc="enable"; fi
		if [ "$ap_bcc" = "disable" ]; then ap_ldpc="enable"; fi

		if [ "$ap_ldpc" = "enable" ]; then
			# set for SU, only if not OFDMA MU TC
			[ "$glob_ofdma_phase_format" = "" ] && radio_params=$radio_params" HtLDPCenabled 1 VhtLDPCenabled 1 HeLdpcCodingInPayload 1"
			# set for MU
			for usr_index in 1 2 3 4
			do
				radio_params=$radio_params" WaveSPRcrTfUsrCodingTypeBccOrLpdc${usr_index} 1"
			done
		elif [ "$ap_bcc" = "enable" ]; then
			# set for SU, only if not OFDMA MU TC
			[ "$glob_ofdma_phase_format" = "" ] && radio_params=$radio_params" HtLDPCenabled 0 VhtLDPCenabled 0 HeLdpcCodingInPayload 0"
			# set for MU
			for usr_index in 1 2 3 4
			do
				radio_params=$radio_params" WaveSPRcrTfUsrCodingTypeBccOrLpdc${usr_index} 0"
			done
		else
			error_print "Unsupported value - ap_ldpc:$ap_ldpc ap_bcc:$ap_bcc"
			send_error ",errorCode,135"
			return
		fi
	fi

	local CMD_FILE="/tmp/sigma-cmds"

	if [ "$global_mcs_fixedrate" != "" ]; then
		debug_print "global_mcs_fixedrate:$global_mcs_fixedrate"

#		echo "configure wlan" > $CMD_FILE
#		echo "start" >> $CMD_FILE
#		echo "set radio $ap_interface WaveFRMcs ${global_mcs_fixedrate}" >> $CMD_FILE
#		echo "set radio $ap_interface WaveFRAutoRate 0" >> $CMD_FILE
#		debug_print "ap_program:$ap_program"
#		if [ "$ap_program" = "HE" ]; then
#			echo "set radio $ap_interface WaveFRPhyMode ax" >> $CMD_FILE
#			echo "set radio $ap_interface WaveFRCpMode 5" >> $CMD_FILE
#		fi
#		echo "commit" >> $CMD_FILE
#		echo "exit" >> $CMD_FILE
#		clish $CMD_FILE > /dev/console
#		rm -f $CMD_FILE

		# set for SU, only if not OFDMA MU TC
		if [ "$glob_ofdma_phase_format" = "" ]; then
			radio_params=$radio_params" WaveFRMcs ${global_mcs_fixedrate}"
			radio_params=$radio_params" WaveFRAutoRate 0"
			if [ "$ap_program" = "HE" ]; then
				radio_params=$radio_params" WaveFRPhyMode ax"
				radio_params=$radio_params" WaveFRCpMode 5"
			fi
		fi

		# set for MU
		for ap_usr_index in 1 2 3 4
		do
			# get MU DL NSS value from FAPI
			ap_nss=`get_plan_nss_dl $ap_interface $ap_usr_index`

			# calculate the OFDMA MU NSS-MCS value (NSS: bits 5-4, MCS: bits 3-0)
			let ap_ofdma_mu_nss_mcs_val="($ap_nss-1)*16+$global_mcs_fixedrate"

			# set MU DL NSS MCS value
			radio_params=$radio_params" WaveSPDlUsrPsduRatePerUsp${ap_usr_index} ${ap_ofdma_mu_nss_mcs_val}"

			# get MU UL NSS value from FAPI
			ap_nss=`get_plan_nss_ul $ap_interface $ap_usr_index`

			# calculate the OFDMA MU NSS-MCS value (NSS: bits 5-4, MCS: bits 3-0)
			let ap_ofdma_mu_nss_mcs_val="($ap_nss-1)*16+$global_mcs_fixedrate"

			# set MU UL NSS MCS value
			radio_params=$radio_params" WaveSPDlUsrUlPsduRatePerUsp${ap_usr_index} ${ap_ofdma_mu_nss_mcs_val}"

			# TBD: do we need to set the UL value here?
		done
	fi

	if [ "$ap_nss_mcs_cap" != "" ]; then
		debug_print "ap_nss_mcs_cap:$ap_nss_mcs_cap"
		ap_nss_cap=${ap_nss_mcs_cap%%;*}
		ap_mcs_cap=${ap_nss_mcs_cap##*;}

		ap_mcs_min_cap=${ap_mcs_cap%%-*}
		ap_mcs_max_cap=${ap_mcs_cap##*-}

		# JIRA WLANRTSYS-9372: When SPATIAL_RX_STREAM or SPATIAL_TX_STREAM are set, use ap_mcs_max_cap; otherwise, use the default value
		global_rx_mcs_max_cap=$ap_mcs_max_cap
		global_tx_mcs_max_cap=$ap_mcs_max_cap

		# JIRA WLANRTSYS-9372: no need to set FR when capab is being set (due to Tests 5.35.1 & 5.36.1)
		# Here was a code that was set: WaveFRNss, WaveFRMcs, WaveFRAutoRate, WaveFRPhyMode, WaveFRCpMode

		# calculate the OFDMA MU NSS-MCS value (NSS: bits 5-4, MCS: bits 3-0)
		let ap_ofdma_mu_nss_mcs_val="($ap_nss_cap-1)*16+$ap_mcs_max_cap"

		# set for MU
		for ap_usr_index in 1 2 3 4
		do
			radio_params=$radio_params" WaveSPDlUsrPsduRatePerUsp${ap_usr_index} ${ap_ofdma_mu_nss_mcs_val}"
			radio_params=$radio_params" WaveSPDlUsrUlPsduRatePerUsp${ap_usr_index} ${ap_ofdma_mu_nss_mcs_val}"
		done

		# set the nss mcs capabilities
		ap_nss_mcs_val=`get_nss_mcs_val $ap_nss_cap $ap_mcs_max_cap`
		if [ "$ap_nss_mcs_val" = "" ]
		then
			error_print "Unsupported value - ap_nss_cap:$ap_nss_cap ap_mcs_max_cap:$ap_mcs_max_cap"
			send_error ",errorCode,137"
			return
		fi

		radio_params=$radio_params" HeheMcsNssRxHeMcsMapLessThanOrEqual80Mhz ${ap_nss_mcs_val}"
		## JIRA WLANRTSYS-11028: part0-Rx part1-Tx
		radio_params=$radio_params" WaveVhtMcsSetPart0 ${ap_nss_mcs_val}"
		global_nss_opt_ul=${ap_nss_cap}

		radio_params=$radio_params" HeheMcsNssTxHeMcsMapLessThanOrEqual80Mhz ${ap_nss_mcs_val}"
		# JIRA WLANRTSYS-11028: part0-Rx part1-Tx
		radio_params=$radio_params" WaveVhtMcsSetPart1 ${ap_nss_mcs_val}"
		global_nss_opt_dl=${ap_nss_cap}

		radio_params=$radio_params" HeheMcsNssRxHeMcsMap160Mhz ${ap_nss_mcs_val}"
		radio_params=$radio_params" HeheMcsNssTxHeMcsMap160Mhz ${ap_nss_mcs_val}"
	fi

	if [ "$ap_spatial_rx_stream" != "" ]; then
		debug_print "ap_spatial_rx_stream:$ap_spatial_rx_stream"
		debug_print "ap_program:$ap_program"

		if [ "$ap_program" = "HE" ]; then

			ap_num_users_ofdma=`get_nof_sta_per_he_test_case $glob_ssid`
			mu_type=`get_plan_dl_com_mu_type $ap_interface`

			if [ "$glob_ap_mimo" != "" ] || [ "$mu_type" = "1" ]; then
				if [ "$ap_num_users_ofdma" = "2" ]; then
					[ "$ap_spatial_rx_stream" = "2SS" ] && ap_spatial_rx_stream="1SS"
					[ "$ap_spatial_rx_stream" = "4SS" ] && ap_spatial_rx_stream="2SS"
				fi
				if [ "$ap_num_users_ofdma" = "4" ]; then
					[ "$ap_spatial_rx_stream" = "4SS" ] && ap_spatial_rx_stream="1SS"
				fi
			fi

			spatial_rx_stream_number=${ap_spatial_rx_stream%%S*}

			# check that the NSS # is 1, 2, 3 or 4
			case $spatial_rx_stream_number in
			1|2|3|4)
			;;
			*)
				error_print "Unsupported value - ap_spatial_rx_stream:$ap_spatial_rx_stream"
				send_error ",errorCode,140"
				return
			;;
			esac

			if [ -z "$ucc_type" ] || [ "$ucc_type" = "testbed" ]; then
				if [ -n "$global_rx_mcs_max_cap" ]; then
					# JIRA WLANRTSYS-9372: When SPATIAL_RX_STREAM or SPATIAL_TX_STREAM are set, use ap_mcs_max_cap; otherwise, use the default value
					local_mcs=$global_rx_mcs_max_cap
					global_rx_mcs_max_cap=""
				else
					local_mcs=$mcs_def_val_ul_testbed
				fi
			elif [ "$ucc_type" = "dut" ]; then
				local_mcs=$mcs_def_val_ul
			fi

			ap_spatial_rx_stream_val=`get_nss_mcs_val $spatial_rx_stream_number $local_mcs`
			radio_params=$radio_params" HeheMcsNssRxHeMcsMapLessThanOrEqual80Mhz ${ap_spatial_rx_stream_val}"
			# JIRA WLANRTSYS-11028: part0-Rx part1-Tx
			radio_params=$radio_params" WaveVhtMcsSetPart0 ${ap_spatial_rx_stream_val}"
			global_nss_opt_ul=${spatial_rx_stream_number}
		fi
	fi

	if [ "$ap_spatial_tx_stream" != "" ]; then
		debug_print "ap_spatial_tx_stream:$ap_spatial_tx_stream"
		debug_print "ap_program:$ap_program"

		if [ "$ap_program" = "HE" ]; then

			ap_num_users_ofdma=`get_nof_sta_per_he_test_case $glob_ssid`
			mu_type=`get_plan_dl_com_mu_type $ap_interface`

			if [ "$glob_ap_mimo" != "" ] || [ "$mu_type" = "1" ]; then
				if [ "$ap_num_users_ofdma" = "2" ]; then
					[ "$ap_spatial_tx_stream" = "2SS" ] && ap_spatial_tx_stream="1SS"
					[ "$ap_spatial_tx_stream" = "4SS" ] && ap_spatial_tx_stream="2SS"
				fi
				if [ "$ap_num_users_ofdma" = "4" ]; then
					[ "$ap_spatial_tx_stream" = "4SS" ] && ap_spatial_tx_stream="1SS"
				fi
			fi

			spatial_tx_stream_number=${ap_spatial_tx_stream%%S*}

			# check that the NSS # is 1, 2, 3 or 4
			case $spatial_tx_stream_number in
			1|2|3|4)
			;;
			*)
				error_print "Unsupported value - ap_spatial_tx_stream:$ap_spatial_tx_stream"
				send_error ",errorCode,145"
				return
			;;
			esac

			if [ -z "$ucc_type" ] || [ "$ucc_type" = "testbed" ]; then
				if [ -n "$global_tx_mcs_max_cap" ]; then
					# JIRA WLANRTSYS-9372: When SPATIAL_RX_STREAM or SPATIAL_TX_STREAM are set, use ap_mcs_max_cap; otherwise, use the default value
					local_mcs=$global_tx_mcs_max_cap
					global_tx_mcs_max_cap=""
				else
					local_mcs=$mcs_def_val_dl_testbed
				fi
			elif [ "$ucc_type" = "dut" ]; then
				local_mcs=$mcs_def_val_dl
			fi

			ap_spatial_tx_stream_val=`get_nss_mcs_val $spatial_tx_stream_number $local_mcs`
			radio_params=$radio_params" HeheMcsNssTxHeMcsMapLessThanOrEqual80Mhz ${ap_spatial_tx_stream_val}"
			# JIRA WLANRTSYS-11028: part0-Rx part1-Tx
			radio_params=$radio_params" WaveVhtMcsSetPart1 ${ap_spatial_tx_stream_val}"

			global_nss_opt_dl=${spatial_tx_stream_number}

			# set for MU
			for ap_usr_index in 1 2 3 4
			do
				# get MU DL MCS value from FAPI
				ap_mcs=`get_plan_mcs_dl $ap_interface $ap_usr_index`

				# calculate the OFDMA MU NSS-MCS value (NSS: bits 5-4, MCS: bits 3-0)
				let ap_ofdma_mu_nss_mcs_val="($spatial_tx_stream_number-1)*16+$ap_mcs"

				# set MU DL NSS MCS value
				radio_params=$radio_params" WaveSPDlUsrPsduRatePerUsp${ap_usr_index} ${ap_ofdma_mu_nss_mcs_val}"
				# TBD: do we need to set the UL value here?
				#radio_params=$radio_params" WaveSPDlUsrUlPsduRatePerUsp${ap_usr_index} ${ap_ofdma_mu_nss_mcs_val}"
			done
		else
			echo "configure wlan" > $CMD_FILE
			echo "start" >> $CMD_FILE
			echo "set radio $ap_interface WaveFRNss ${ap_spatial_tx_stream}" >> $CMD_FILE
			echo "set radio $ap_interface WaveFRAutoRate 0" >> $CMD_FILE
			echo "commit" >> $CMD_FILE
			echo "exit" >> $CMD_FILE
			clish $CMD_FILE > /dev/console
			rm -f $CMD_FILE
		fi
	fi

	if [ "$ap_ltf_gi" != "" ]; then
		debug_print "ap_ltf_gi:$ap_ltf_gi"
		ap_ltf=${ap_ltf_gi%%:*}
		ap_gi=${ap_ltf_gi##*:}

		# LTF is in units (1X, 2X, 4X), GI is in mSec
		if [ "$ap_ltf" = "2" ] && [ "$ap_gi" = "0.8" ]; then
			ap_su_ltf_gi="He0p8usCP2xLTF"
			ap_mu_dl_com_he_cp=0
			ap_mu_dl_com_he_ltf=1
			if [ "$glob_ofdma_phase_format" = "1" ]; then
				# this LTF and GI combination is not supported in UL
				error_print "Unsupported value - glob_ofdma_phase_format:$glob_ofdma_phase_format ap_ltf:$ap_ltf ap_gi:$ap_gi"
				send_invalid ",errorCode,155"
				return
			fi
		elif [ "$ap_ltf" = "2" ] && [ "$ap_gi" = "1.6" ]; then
			ap_su_ltf_gi="He1p6usCP2xLTF"
			ap_mu_dl_com_he_cp=1
			ap_mu_dl_com_he_ltf=1
			ap_mu_ul_com_he_cp=1
			ap_mu_ul_com_he_ltf=1
			ap_mu_ul_com_he_tf_cp_and_ltf=1
		elif [ "$ap_ltf" = "4" ] && [ "$ap_gi" = "3.2" ]; then
			ap_su_ltf_gi="He3p2usCP4xLTF"
			ap_mu_dl_com_he_cp=2
			ap_mu_dl_com_he_ltf=2
			ap_mu_ul_com_he_cp=2
			ap_mu_ul_com_he_ltf=2
			ap_mu_ul_com_he_tf_cp_and_ltf=2
		else
			# all other LTF and GI combinations are not supported
			error_print "Unsupported value - ap_ltf:$ap_ltf ap_gi:$ap_gi"
			send_invalid ",errorCode,160"
			return
		fi

		# TODO: the below is not supported yet. requirements will be changed.
		# set for SU, only if not OFDMA MU TC
		#[ "$glob_ofdma_phase_format" = "" ] && radio_params=$radio_params" WaveltfAndGiFixedRate Fixed WaveltfAndGiValue ${ap_su_ltf_gi}"

		# set for MU
		#radio_params=$radio_params" WaveSPDlComHeCp ${ap_mu_dl_com_he_cp}"
		#radio_params=$radio_params" WaveSPDlComHeLtf ${ap_mu_dl_com_he_ltf}"
		#radio_params=$radio_params" WaveSPUlComHeCp ${ap_mu_ul_com_he_cp}"
		#radio_params=$radio_params" WaveSPUlComHeLtf ${ap_mu_ul_com_he_ltf}"
		#radio_params=$radio_params" WaveSPRcrComTfHegiAndLtf ${ap_mu_ul_com_he_tf_cp_and_ltf}"
	fi

#	if [ "$ap_wlan_tag" != "" ]; then
#		debug_print "add VAP WLAN_TAG"
#	fi

#	if [ "$ap_region" != "" ]; then
#		TODO: Why is this code commented out??
#		OLIVER: no specific reason, never see any other value than US, so no need to configure, however can be commented in
#		radio_params=$radio_params" RegulatoryDomain \"$ap_region\""
#	fi

	debug_print "radio_params:$radio_params"

	ap_params=""
	if [ "$ap_wme_val" != "" ]; then
		ap_params=$ap_params" WMMEnable $ap_wme_val"
	fi
	if [ "$ap_wmmps_val" != "" ]; then
		ap_params=$ap_params" UAPSDEnable $ap_wmmps_val"
	fi
	if [ "$ap_gas_cb_delay" != "" ]; then
		ap_params=$ap_params" GasComebackDelay $ap_gas_cb_delay"
	fi
	if [ "$ap_oce_support" != "" ]; then
		[ "$ap_oce_support" = "disable" ] && ap_oce_support="false"
		[ "$ap_oce_support" = "enable" ] && ap_oce_support="true"
		ap_params=$ap_params" OceEnabled $ap_oce_support"
	fi
	if [ "$ap_fils_dscv" != "" ]; then
		[ "$ap_fils_dscv" = "disable" ] && ap_fils_dscv="false"
		[ "$ap_fils_dscv" = "enable" ] && ap_fils_dscv="true"
		ap_params=$ap_params" OceFilsDscvEnabled $ap_fils_dscv"
	fi
	if [ "$ap_fils_dscv_interval" != "" ]; then
		ap_params=$ap_params" OceFilsInterval $ap_fils_dscv_interval"
	fi

	if [ "$ap_broadcast_ssid" != "" ]; then
		[ "$ap_broadcast_ssid" = "disable" ] && ap_broadcast_ssid="false"
		[ "$ap_broadcast_ssid" = "enable" ]	&& ap_broadcast_ssid="true"
		ap_params=$ap_params" SSIDAdvertisementEnabled $ap_broadcast_ssid"
	fi

	if [ "$ap_ble_sta_count" != "" ]; then
		ap_params=$ap_params" OceBleStaCount $ap_ble_sta_count"
	fi

	if [ "$ap_ble_channel_util" != "" ]; then
		ap_params=$ap_params" OceBleChannelUtil $ap_ble_channel_util"
	fi

	if [ "$ap_ble_avail_admin_cap" != "" ]; then
		ap_params=$ap_params" OceBleAvailAdminCap $ap_ble_avail_admin_cap"
	fi

	if [ "$ap_air_time_fract" != "" ]; then
		ap_params=$ap_params" OceAirTimeFract $ap_air_time_fract"
	fi

	if [ "$ap_data_ppdu_duration" != "" ]; then
		ap_params=$ap_params" OceDataPpduDuration $ap_data_ppdu_duration"
	fi

	if [ "$dhcp_serv_ip_addr" != "" ]; then
		ap_params=$ap_params" OceDhcpServIpAddr $dhcp_serv_ip_addr"
	fi

	if [ "$oce_ba_win_size" != "" ]; then
		ap_params=$ap_params" OceBAWInSize $oce_ba_win_size"
	fi

	if [ "$oce_data_format" != "" ]; then
		ap_params=$ap_params" OceDataFormat $oce_data_format"
	fi

	if [ "$oce_esp_ie" != "" ]; then
		[ "$oce_esp_ie" = "disable" ] && oce_esp_ie="false"
		[ "$oce_esp_ie" = "enable" ] && oce_esp_ie="true"
		ap_params=$ap_params" OceESPIE $oce_esp_ie"
	fi

	if [ "$ap_rnr" != "" ]; then
		if [ "$ap_rnr" = "enable" ]; then
			ap_params=$ap_params" OceIncludeRnrInBeaconProbe 1"
			ap_params=$ap_params" OceIncludeRnrInFils 1"
		elif [ "$ap_rnr" = "disable" ]; then
			ap_params=$ap_params" OceIncludeRnrInBeaconProbe 0"
			ap_params=$ap_params" OceIncludeRnrInFils 0"
		fi
	fi

	if [ "$ap_filshlp" != "" ]; then
		[ "$ap_filshlp" = "disable" ] && ap_filshlp="false"
		[ "$ap_filshlp" = "enable" ] && ap_filshlp="true"
		ap_params=$ap_params" OceFilsHlp $ap_filshlp"
	fi

	if [ "$ap_nairealm" != "" ]; then
		ap_params=$ap_params" OceNAIRealm $ap_nairealm"
	fi

	if [ "$ap_ampdu" != "" ]; then
		debug_print "ap_ampdu:$ap_ampdu"
		if [ "$ap_ampdu" = "enable" ]; then
			ap_ampdu_val="1"
			ap_ampdu_exp_val="2"
		elif [ "$ap_ampdu" = "disable" ]; then
			ap_ampdu_val="0"
			ap_ampdu_exp_val="0"
		else
			error_print "Unsupported value - ap_ampdu:$ap_ampdu"
			send_error ",errorCode,165"
			return
		fi
		radio_params=$radio_params" HeAMsduInAMpduSupport $ap_ampdu_val"
		radio_params=$radio_params" HeMacMaximumAMpduLengthExponent $ap_ampdu_exp_val"
		ap_params=$ap_params" WaveBaAgreementEnabled $ap_ampdu_val"  # JIRA WLANRTSYS-9583
	fi

	if [ "$ap_amsdu" != "" ]; then
		debug_print "ap_amsdu:$ap_amsdu"
		if [ "$ap_amsdu" = "enable" ] || [ "$ap_amsdu" = "1" ]; then
			ap_amsdu_val="1"
		elif [ "$ap_amsdu" = "disable" ] || [ "$ap_amsdu" = "0" ]; then
			ap_amsdu_val="0"
		else
			error_print "Unsupported value - ap_amsdu:$ap_amsdu"
			send_error ",errorCode,170"
			return
		fi
		ap_params=$ap_params" WaveAmsduEnabled $ap_amsdu_val"
		## WLANRTSYS-11027 upon AMSDU enable Set WaveMaxMpduLen to 11000
		[ "$ap_amsdu_val" = "1" ] && radio_params=$radio_params" WaveMaxMpduLen 11000"
		## adding he_capab:A_MSDU_IN_A_MPDU_SUPPORT handling - need to review for R2(AH).
		radio_params=$radio_params" HeAMsduInAMpduSupport $ap_amsdu_val"
	fi

	if [ "$ap_addba_reject" != "" ]; then
		debug_print "ap_addba_reject:$ap_addba_reject"
		if [ "$ap_addba_reject" = "enable" ]; then
			ap_addba_reject_val="0" # reject=enable means 0 (disable BA agreement) in DB
		elif [ "$ap_addba_reject" = "disable" ]; then
			ap_addba_reject_val="1" # reject=disable means 1 (enable BA agreement) in DB
		else
			error_print "Unsupported value - ap_addba_reject:$ap_addba_reject"
			send_error ",errorCode,175"
			return
		fi
		ap_params=$ap_params" WaveBaAgreementEnabled $ap_addba_reject_val"
	fi

	if [ "$ap_addbareq_bufsize" != "" ]; then
		if [ "$ap_addbareq_bufsize" = "gt64" ]; then
			ap_addbareq_bufsize="256"
		elif [ "$ap_addbareq_bufsize" = "le64" ]; then
			ap_addbareq_bufsize="64"
		else
			error_print "Unsupported value - ap_addba_reject:$ap_addba_reject"
			send_error ",errorCode,176"
			return
		fi

		[ "$ap_addbareq_bufsize" != "0" ] && ap_params=$ap_params" WaveBaWindowSize $ap_addbareq_bufsize"
	fi
	## JIRA WLANRTSYS-10849
	if [ "$ap_addbaresp_bufsize" != "" ]; then
		if [ "$ap_addbaresp_bufsize" = "gt64" ]; then
			ap_addbaresp_bufsize="256"
		elif [ "$ap_addbaresp_bufsize" = "le64" ]; then
			ap_addbaresp_bufsize="64"
		else
			error_print "Unsupported value - ap_addbaresp_bufsize:$ap_addbaresp_bufsize"
			send_error ",errorCode,178"
			return
		fi

		[ "$ap_addbaresp_bufsize" != "0" ] && ap_params=$ap_params" WaveBaWindowSize $ap_addbaresp_bufsize"
	fi

	if [ "$ap_tx_bf" != "" ]; then
		debug_print "ap_tx_bf:$ap_tx_bf"
		if [ "$ap_tx_bf" = "enable" ]; then
			ap_tx_bf_val="Auto"
			if [ "$ap_program" = "HE" ]; then
				# WLANRTSYS-10947
				radio_params=$radio_params" HeSuBeamformerCapable 1"
				radio_params=$radio_params" HeMuBeamformerCapable 1"
				radio_params=$radio_params" HeSuBeamformeeCapableEnable 1"
				# set the maximum but will not be set more the hw antennas.
				radio_params=$radio_params" WaveHeNumOfAntennas 4"
				radio_params=$radio_params" HeTriggeredSuBeamformingFeedback 1"
			fi
		elif [ "$ap_tx_bf" = "disable" ]; then
			ap_tx_bf_val="Disabled"
		else
			error_print "Unsupported value - ap_tx_bf:$ap_tx_bf"
			send_error ",errorCode,180"
			return
		fi
		radio_params=$radio_params" WaveBfMode $ap_tx_bf_val"
	fi

	if [ "$ap_num_sound_dim" != "" ]; then
		debug_print "ap_num_sound_dim:$ap_num_sound_dim"
		if [ "$ap_num_sound_dim" -gt 4 ]; then
			error_print "Unsupported value - ap_num_sound_dim:$ap_num_sound_dim"
			send_error ",errorCode,185"
			return
		fi
	fi

	# TODO: the following implementation should be changed.
	if [ "$ap_vht_cap_160_cw" != "" ]; then
		debug_print "ap_vht_cap_160_cw:$ap_vht_cap_160_cw"
		if [ "$ap_vht_cap_160_cw" = "1" ]; then
			ap_width_val="160MHz"
		elif [ "$ap_vht_cap_160_cw" = "0" ]; then
			# assumption: this test is running on 5GHz, return the BW to 80 MHz.
			ap_width_val="80MHz"
		else
			error_print "Unsupported value - ap_vht_cap_160_cw:$ap_vht_cap_160_cw"
			send_error ",errorCode,190"
			return
		fi
		radio_params=$radio_params" OperatingChannelBandwidth $ap_width_val"
		radio_params=$radio_params" WaveFRBandwidth $ap_width_val"

		# Only in 160 MHz / 5GHz, change channel check time to 1 sec to save configuration time.
		if [ "$ap_width_val" = "160MHz" ]&& [ "$ap_interface" = "wlan2" ]; then
			info_print "iwpriv $ap_interface s11hChCheckTime 1"
			iwpriv $ap_interface s11hChCheckTime 1
		fi
	fi

	if [ "$ap_omcontrol" != "" ]; then
		debug_print "ap_omcontrol:$ap_omcontrol"
		if [ "$ap_omcontrol" = "enable" ]; then
			ap_omcontrol="true"
		elif [ "$ap_omcontrol" = "disable" ]; then
			ap_omcontrol="false"
		else
			error_print "Unsupported value - ap_omcontrol:$ap_omcontrol"
			send_error ",errorCode,195"
			return
		fi
		radio_params=$radio_params" HeOmControlSupport $ap_omcontrol"
	fi

	if [ "$ap_mimo" != "" ]; then
		if [ "$ap_mimo" = "dl" ] || [ "$ap_mimo" = "ul" ]; then
			glob_ap_mimo="$ap_mimo"
			# TODO: Consider to move to clish_config_commit func and set a flag here.
			[ -z "$(pgrep -f sigmaManagerDaemon.sh)" ] && /opt/lantiq/wave/scripts/sigmaManagerDaemon.sh &

			if [ "$ap_mimo" = "dl" ]; then
				glob_ofdma_phase_format=0
				## Common PART
				radio_params=$radio_params" WaveSPSequenceType 1"
				radio_params=$radio_params" WaveSPDlComMuType 1"
				radio_params=$radio_params" WaveSPRcrComTfLength 406"
				# WLANRTSYS-9638: 'WaveSPDlUsrPsduRatePerUsp' will NOT be set at all
				## Set by the SMD when STAs connected.

			elif [ "$ap_mimo" = "ul" ]; then
				glob_ofdma_phase_format=1
				## TBD currenly not supported
			fi
			radio_params=$radio_params" WaveSPDlComPhasesFormat ${glob_ofdma_phase_format}"
			## ap_num_users_ofdma in case UCC will send num of users if not user will be set according to the test
			if [ "$ap_num_users_ofdma" != "" ]; then
				radio_params=$radio_params" WaveSPDlComNumOfParticipatingStations ${ap_num_users_ofdma}"
			else
				if [ "$glob_ssid" != "" ]; then
					# check if num_of_users can be obtained from the predefined list (by test plan)
					# TODO: this code should be removed in the future, after ucc scripts will configure the num_of_users in every TC
					ap_num_users_ofdma=`get_nof_sta_per_he_test_case $glob_ssid`
					[ "$ap_num_users_ofdma" != "0" ] && radio_params=$radio_params" WaveSPDlComNumOfParticipatingStations ${ap_num_users_ofdma}"
				fi
			fi
		else
			error_print "Unsupported value - ap_mimo:$ap_mimo"
			send_error ",errorCode,197"
			return
		fi
		# set fixed rate in OFDMA MU only
		# TODO: replace this temporary command with clish configuration (through DB)
		info_print "iwpriv $ap_interface sDoSimpleCLI 70 1"
		iwpriv $ap_interface sDoSimpleCLI 70 1

		# set AGER=OFF for OFDMA MU only
		# TODO: replace this temporary command with clish configuration (through DB)
		#info_print "iwpriv $ap_interface sPdThresh 0 0 0"
		#iwpriv $ap_interface sPdThresh 0 0 0
	fi

	# for test # 5.57
	if [ "$global_mu_txbf" != "" ] && [ "$global_mu_txbf" = "enable" ]
	then
		# WLANRTSYS-10947
		radio_params=$radio_params" HeSuBeamformerCapable 1"
		radio_params=$radio_params" HeMuBeamformerCapable 1"
		radio_params=$radio_params" HeSuBeamformeeCapableEnable 1"
		# set the maximum but will not be set more the hw antennas.
		radio_params=$radio_params" WaveHeNumOfAntennas 4"
		radio_params=$radio_params" HeTriggeredSuBeamformingFeedback 1"

		radio_params=$radio_params" WaveSPDlComMuType 1"
		[ "$ap_program" = "HE" ] && [ -z "$(pgrep -f sigmaManagerDaemon.sh)" ] && /opt/lantiq/wave/scripts/sigmaManagerDaemon.sh &
	fi

	if [ "$ap_mu_edca" != "" ]; then
		## JIRA WLANRTSYS-10947: WaveHeMuEdcaIePresent
		[ "$ap_mu_edca" = "override" ] && radio_params=$radio_params" WaveHeMuEdcaIePresent 1"
	fi

	if [ "$ap_twt_respsupport" != "" ]; then
		debug_print "ap_twt_respsupport:$ap_twt_respsupport"
		ap_twt_current_db_val=`get_plan_twt_respsupport $ap_interface`

		if [ "$ap_twt_respsupport" = "enable" ]; then
			ap_twt_respsupport="true"
		elif [ "$ap_twt_respsupport" = "disable" ]; then
			ap_twt_respsupport="false"
		else
			error_print "Unsupported value - ap_twt_respsupport:$ap_twt_respsupport"
			send_error ",errorCode,200"
			return
		fi
		if [ "$ap_twt_current_db_val" != "$ap_twt_respsupport" ]; then
			radio_params=$radio_params" HeTargetWakeTime $ap_twt_respsupport"
		else
			debug_print "No change: ap_twt_current_db_val= $ap_twt_current_db_val ap_twt_respsupport= $ap_twt_respsupport"
		fi
	fi

	if [ -n "$ap_min_mpdu_start_spacing" ]; then
		debug_print "ap_min_mpdu_start_spacing:$ap_min_mpdu_start_spacing"
		radio_params=$radio_params" WaveHtMinMpduStartSpacing $ap_min_mpdu_start_spacing"
	fi

	# JIRA WLANRTSYS-9943
	if [ "$ap_vht_extnss" != "" ]; then
		if [ "$ap_vht_extnss" = "eq0" ]; then
			radio_params=$radio_params" VhtExtNssBw3 0"
		elif [ "$ap_vht_extnss" = "eq1" ]; then
			radio_params=$radio_params" VhtExtNssBw3 1"
		else
			error_print "Unsupported value - ap_vht_extnss:$ap_vht_extnss"
			send_error ",errorCode,201"
			return
		fi
	fi

	# Handle MBSSID feature - start
	if [ -n "$global_ap_mbssid" ]; then
		debug_print "global_ap_mbssid:$global_ap_mbssid"

		if [ "$global_ap_mbssid" = "enable" ]; then
			# WaveMultiBssEnable will be set to '1' after all configurations, and will cause hostapd reset, as needed

			if [ -n "$global_num_non_tx_bss" ]; then
				if [ $global_num_non_tx_bss -le 3 ]; then
					ap_mbssid_num_non_tx_bss=3  # numNonTxBss <= 3 [1,2,3] creating additional 3 VAPs (in addition to the main AP).
				else
					ap_mbssid_num_non_tx_bss=7  # numNonTxBss > 3 [4,5,6,7] creating additional 7 VAPs (in addition to the main AP).
				fi
			fi

			if [ -z "$global_is_vaps_created" ] && [ -n "$ap_non_tx_bss_index" ] && [ -n "$ap_mbssid_num_non_tx_bss" ]; then
				debug_print "ap_non_tx_bss_index:$ap_non_tx_bss_index"

				if [ ! -f "$CMD_MBSS_WIRELESS_FILE" ]; then
					echo "configure wlan" > $CMD_MBSS_WIRELESS_FILE
					echo "start" >> $CMD_MBSS_WIRELESS_FILE
				fi

				count=0
				while [ $count -lt $ap_mbssid_num_non_tx_bss ]; do
					count=$((count+1))
					if [ $count -eq $ap_non_tx_bss_index ]; then
						[ -n "$ap_ssid_non_tx_bss_index" ] && echo "add vap $ap_interface $ap_ssid_non_tx_bss_index" >> $CMD_MBSS_WIRELESS_FILE
					else
						echo "add vap $ap_interface MBSSID_VAP_${count}" >> $CMD_MBSS_WIRELESS_FILE
					fi
				done

				# must commit after VAP creation
				echo "commit" >> $CMD_MBSS_WIRELESS_FILE

				# continue with the set of commands
				echo "start" >> $CMD_MBSS_WIRELESS_FILE

				count=0
				while [ $count -lt $ap_mbssid_num_non_tx_bss ]; do
					echo "set ap $ap_interface.${count} WaveMBOEnabled 0" >> $CMD_MBSS_WIRELESS_FILE
					count=$((count+1))
				done

				# JIRA WLANRTSYS-9713: rais a flag; otherwise, when getting 'NONTXBSSINDEX' again, a new set of vaps will be created
				global_is_vaps_created=1
			fi

			if [ "$global_is_vaps_created" = "1" ] && [ -n "$ap_ssid_non_tx_bss_index" ] && [ -n "$ap_non_tx_bss_index" ] && [ "$ap_non_tx_bss_index" -gt "1" ]; then
				# handle indexes '2' and above; index '1' SSID was already set
				local ap_vap_idx=$((ap_non_tx_bss_index-1))
				echo "set SSID $ap_interface.$ap_vap_idx SSID $ap_ssid_non_tx_bss_index" >> $CMD_MBSS_WIRELESS_FILE
			fi
		else
			radio_params=$radio_params" WaveMultiBssEnable 0"
			radio_params=$radio_params" WaveHtOperationCohostedBss 1"
		fi
	fi
	# Handle MBSSID feature - end

	if [ "$ap_srctrl_srvalue15allowed" != "" ]; then
		debug_print "ap_srctrl_srvalue15allowed:$ap_srctrl_srvalue15allowed"

		if [ "$ap_srctrl_srvalue15allowed" = "enable" ] || [ "$ap_srctrl_srvalue15allowed" = "1" ]; then
			ap_srctrl_srvalue15allowed="true"
		elif [ "$ap_srctrl_srvalue15allowed" = "disable" ] || [ "$ap_srctrl_srvalue15allowed" = "0" ]; then
			ap_srctrl_srvalue15allowed="false"
		else
			error_print "Unsupported value - ap_srctrl_srvalue15allowed:$ap_srctrl_srvalue15allowed"
			send_error ",errorCode,203"
			return
		fi
			radio_params=$radio_params" WaveHeSrControlHesigaSpatialReuse15Ena $ap_srctrl_srvalue15allowed"
	fi

	if [ "$ap_he_txop_dur_rts_thr_conf" != "" ]; then
		debug_print "ap_he_txop_dur_rts_thr_conf:$ap_he_txop_dur_rts_thr_conf"
		radio_params=$radio_params" HeOperationTxOpDurationRtsThreshold $ap_he_txop_dur_rts_thr_conf"
	fi

	if [ "$ap_ft_oa" != "" ]; then
		if [ "$ap_ft_oa" = "Enable" ]; then
			ap_ft_oa_clish=true
		else
			ap_ft_oa_clish=false
		fi

		if [ "$ap_domain" != "" ]; then
			dot11FTMobilityDomainID_SET="dot11FTMobilityDomainID $ap_domain "
		else
			dot11FTMobilityDomainID_SET="dot11FTMobilityDomainID 0101"
		fi

		eval AP_MAC1=\$AP_MAC1_$ap_interface
		eval AP_MAC2=\$AP_MAC2_$ap_interface

		MAC1_FOUND=`iwconfig $ap_radio_interface | grep -c $AP_MAC1`
		MAC2_FOUND=`iwconfig $ap_radio_interface | grep -c $AP_MAC2`
		ft_clish_path="${thispath}/ft_clish.sh"

		if [ "$MAC2_FOUND" -gt 0 ]; then
			MAC_NO_DELIMITER=`echo "$AP_MAC2" | tr -d :`
			echo clish -c \"configure wlan\" -c \"start\" \\ > ${ft_clish_path}
			echo -c \"set ap security $ap_interface \\ >> ${ft_clish_path}
			echo $dot11FTMobilityDomainID_SET \\ >> ${ft_clish_path}
			echo NASIdentifierAp grx550-ap1.intel.com \\ >> ${ft_clish_path}
			echo InterAccessPointProtocol br-lan \\ >> ${ft_clish_path}
			#echo FTKeyManagment FT-PSK \\ >> ${ft_clish_path}
			echo FTOverDS false \\ >> ${ft_clish_path}
			echo FastTransionSupport $ap_ft_oa_clish \\ >> ${ft_clish_path}
			echo NASIdentifier1 grx550-ap2.intel.com \\ >> ${ft_clish_path}
			echo dot11FTR1KeyHolderID $MAC_NO_DELIMITER \\ >> ${ft_clish_path}
			echo R0KH1MACAddress $AP_MAC1 \\ >> ${ft_clish_path}
			echo R0KH1key 0f0e0d0c0b0a09080706050403020100 \\ >> ${ft_clish_path}
			echo R1KH1MACAddress $AP_MAC1 \\ >> ${ft_clish_path}
			echo R1KH1Id $AP_MAC1 \\ >> ${ft_clish_path}
			echo R1KH1key 000102030405060708090a0b0c0d0e0f \" \\ >> ${ft_clish_path}
			echo '-c "commit" 1>&2' >> ${ft_clish_path}
		elif [ "$MAC1_FOUND" -gt 0 ]; then
			MAC_NO_DELIMITER=`echo "$AP_MAC1" | tr -d :`
			echo clish -c \"configure wlan\" -c \"start\" \\ > ${ft_clish_path}
			echo -c \"set ap security $ap_interface \\ >> ${ft_clish_path}
			echo $dot11FTMobilityDomainID_SET \\ >> ${ft_clish_path}
			echo NASIdentifierAp grx550-ap2.intel.com \\ >> ${ft_clish_path}
			echo InterAccessPointProtocol br-lan \\ >> ${ft_clish_path}
			#echo FTKeyManagment FT-PSK \\ >> ${ft_clish_path}
			echo FTOverDS false \\ >> ${ft_clish_path}
			echo FastTransionSupport $ap_ft_oa_clish \\ >> ${ft_clish_path}
			echo NASIdentifier1 grx550-ap1.intel.com \\ >> ${ft_clish_path}
			echo dot11FTR1KeyHolderID $MAC_NO_DELIMITER \\ >> ${ft_clish_path}
			echo R0KH1MACAddress $AP_MAC2 \\ >> ${ft_clish_path}
			echo R0KH1key 000102030405060708090a0b0c0d0e0f \\ >> ${ft_clish_path}
			echo R1KH1MACAddress $AP_MAC2 \\ >> ${ft_clish_path}
			echo R1KH1Id $AP_MAC2 \\ >> ${ft_clish_path}
			echo R1KH1key 0f0e0d0c0b0a09080706050403020100 \" \\ >> ${ft_clish_path}
			echo '-c "commit" 1>&2' >> ${ft_clish_path}
		else
			mac=`ifconfig $ap_interface | grep HWaddr`
			mac=${mac##*HWaddr}
			AP_MAC1=`echo $mac`
			eval AP_MAC2=\$AP_MAC2_$ap_radio_interface

			MAC_NO_DELIMITER=`echo "$AP_MAC1" | tr -d :`
			echo clish -c \"configure wlan\" -c \"start\" \\ > ${ft_clish_path}
			echo -c \"set ap security $ap_interface \\ >> ${ft_clish_path}
			echo $dot11FTMobilityDomainID_SET \\ >> ${ft_clish_path}
			echo NASIdentifierAp grx550-ap2.intel.com \\ >> ${ft_clish_path}
			echo InterAccessPointProtocol br-lan \\ >> ${ft_clish_path}
			#echo FTKeyManagment FT-PSK \\ >> ${ft_clish_path}
			echo FTOverDS false \\ >> ${ft_clish_path}
			echo FastTransionSupport true \\ >> ${ft_clish_path}
			echo NASIdentifier1 grx550-ap1.intel.com \\ >> ${ft_clish_path}
			echo dot11FTR1KeyHolderID $MAC_NO_DELIMITER \\ >> ${ft_clish_path}
			echo R0KH1MACAddress $AP_MAC2 \\ >> ${ft_clish_path}
			echo R0KH1key 000102030405060708090a0b0c0d0e0f \\ >> ${ft_clish_path}
			echo R1KH1MACAddress $AP_MAC2 \\ >> ${ft_clish_path}
			echo R1KH1Id $AP_MAC2 \\ >> ${ft_clish_path}
			echo R1KH1key 0f0e0d0c0b0a09080706050403020100 \" \\ >> ${ft_clish_path}
			echo '-c "commit" 1>&2' >> ${ft_clish_path}
		fi

		chmod 777 ${ft_clish_path}
		eval ${ft_clish_path}
	fi

	debug_print "ap_params:$ap_params"
	clish_config_set

	debug_print "ap_commit_now:$ap_commit_now"
	if [ "$ap_commit_now" = "1" ]; then
		clish_config_commit
	fi

	if [ "$ap_cellular_cap_pref" != "" ]; then
		# TODO: Execute this API immediately, or also on commit??
		# OLIVER: Who can answer? Isar? I would assume it is ok to send it now.
		debug_print "/usr/sbin/fapi_wlan_debug_cli COMMAND \"AML_CELL_CONFIG_SET $ap_radio_interface 1 $ap_cellular_cap_pref\""
		/usr/sbin/fapi_wlan_debug_cli COMMAND "AML_CELL_CONFIG_SET $ap_radio_interface 1 $ap_cellular_cap_pref" >> $CONF_DIR/BeeRock_CMD.log
		#sleep 3
		#killall fapi_wlan_debug_cli 1>&2
		#/usr/sbin/fapi_wlan_debug_cli AML_START 1>&2 &
	fi

	send_complete
	debug_print "ap_set_wireless():end"
}

ap_set_11n_wireless()
{
	ap_name="INTEL"
	# default to wlan0 in case no interface parameter is given
	ap_interface=""

	IFS=" "
	send_running
	# checking for interface parameter (might fail if some value is also "interface")
	get_interface_name $@
	stop_wlan

	#get_wlan_param_radio_all "ap_channel" "ap_auto_channel" "ap_mode_val" "ap_band_val" "ap_radio" "ap_offset_val" "global_mcs_fixedrate" "ap_sgi20_val" "ap_width_val" "ap_region"
	# not supported by clish
	#ap_bcnint=100
	ap_rts=2347
	ap_frgmnt=2346
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
	#debug_print "global_mcs_fixedrate:$global_mcs_fixedrate"
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
				ap_name=$1
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
				ap_ssid=$1
			;;
			CHANNEL)
				shift
				ap_channel=$1
				ap_auto_channel=0
			;;
			MODE)
				shift
				lower "$1" ap_mode
			;;
			WME)
				shift
				lower "$1" ap_wme
			;;
			WMMPS)
				shift
				lower "$1" ap_wmmps
			;;
			RTS)
				shift
				ap_rts=$1
			;;
			FRGMNT)
				shift
				ap_frgmnt=$1
			;;
			PWRSAVE)
				# param 11n not supported
				shift
				ap_pwrsave=$1
			;;
			BCNINT)
				shift
				ap_bcnint=$1
			;;
			RADIO)
				shift
				lower "$1" ap_radio
			;;
			40_INTOLERANT)
				# param 11n not supported
				shift
			;;
			ADDBA_REJECT)
				# param 11n not supported
				shift
			;;
			AMPDU)
				shift
				lower "$1" ap_ampdu
			;;
			AMPDU_EXP)
				shift
				ap_ampdu_exp=$1
			;;
			AMSDU)
				shift
				lower "$1" ap_amsdu
			;;
			GREENFIELD)
				# param 11n not supported
				shift
			;;
			OFFSET)
				shift
				ap_offset=$1
			;;
			MCS_32)
				# param 11n not supported
				shift
			;;
			MCS_FIXEDRATE)
				shift
				global_mcs_fixedrate=$1
			;;
			SPATIAL_RX_STREAM)
				# 11n do nothing
				shift
			;;
			SPATIAL_TX_STREAM)
				# 11n do nothing
				shift
			;;
			MPDU_MIN_START_SPACING)
				# param 11n not supported
				shift
			;;
			RIFS_TEST)
				# param 11n not supported
				shift
			;;
			SGI20)
				shift
				lower "$1" ap_sgi20
			;;
			STBC_TX)
				# param 11n not supported
				shift
			;;
			WIDTH)
				shift
				lower "$1" ap_width
			;;
			WIDTH_SCAN)
				# param 11n not supported
				shift
			;;
			COUNTRY) ## NB: Extension parameter
				shift
				ap_region=$1
			;;
			COUNTRYCODE) ## NB: Extension parameter
				shift
				ap_region=$1
			;;
			*)
				error_print "while loop error $1"
				send_invalid ",errorCode,204"
				return
			;;
		esac
		shift
	done

	# Mode / Band selection
	if [ "$ap_mode" != "" ]; then
		case "$ap_mode" in
		11a)
			ap_mode_val="a"
			ap_band_val="5GHz"
		;;
		11b)
			ap_mode_val="b"
			ap_band_val="2.4GHz"
		;;
		11g)
			ap_mode_val="g"
			ap_band_val="2.4GHz"
		;;
		11bg)
			ap_mode_val="b,g"
			ap_band_val="2.4GHz"
		;;
		11bgn)
			ap_mode_val="b,g,n"
			ap_band_val="2.4GHz"
		;;
		11ng)
			ap_mode_val="b,g,n"
			ap_band_val="2.4GHz"
		;;
		11na)
			ap_mode_val="a,n"
			ap_band_val="5GHz"
		;;
		11an)
			ap_mode_val="a,n"
			ap_band_val="5GHz"
		;;
		11ac)
			ap_mode_val="a,n,ac"
			ap_band_val="5GHz"
		;;
		11anac)
			ap_mode_val="a,n,ac"
			ap_band_val="5GHz"
		;;
		*)
			error_print "Unsupported value - ap_mode:$ap_mode"
			send_error ",errorCode,205"
			return
		;;
		esac
	fi

	if [ "$ap_wme" != "" ]; then
		if [ "$ap_wme" = "on" ]; then
			ap_wme_val="1"
		elif [ "$ap_wme" = "off" ]; then
			ap_wme_val="0"
		else
			error_print "Unsupported value - ap_wme:$ap_wme"
			send_error ",errorCode,210"
			return
		fi
	fi

	if [ "$ap_wmmps" != "" ]; then
		if [ "$ap_wmmps" = "on" ]; then
			ap_wmmps_val="1"
		elif [ "$ap_wmmps" = "off" ]; then
			ap_wmmps_val="0"
		else
			error_print "Unsupported value - ap_wmmps:$ap_wmmps"
			send_error ",errorCode,215"
			return
		fi
	fi

#####
#	if [ "$ap_ampdu" != "" ]; then
#		if [ "$ap_ampdu" = "enable" ]; then
#			ap_ampdu_val="1"
#		elif [ "$ap_ampdu" = "disable" ]; then
#			ap_ampdu_val="0"
#		else
#			error_print "Unsupported value - ap_ampdu:$ap_ampdu"
#			send_error ",errorCode,220"
#			return
#		fi
#	fi

#	if [ "$ap_ampdu_exp" != "" ]; then
#		if [ "$ap_ampdu_exp" = "16" ]; then
#			ap_ampdu_exp_val="65535"
#		elif [ "$ap_ampdu_exp" = "15" ]; then
#			ap_ampdu_exp_val="32767"
#		elif [ "$ap_ampdu_exp" = "14" ]; then
#			ap_ampdu_exp_val="16383"
#		elif [ "$ap_ampdu_exp" = "13" ]; then
#			ap_ampdu_exp_val="8191"
#		else
#			error_print "Unsupported value - ap_ampdu_exp:$ap_ampdu_exp"
#			send_error ",errorCode,225"
#			return
#		fi
#	fi

#	if [ "$ap_amsdu" != "" ]; then
#		if [ "$ap_amsdu" = "enable" ] || [ "$ap_amsdu" = "1" ]; then
#			ap_amsdu_val="1"
#		elif [ "$ap_amsdu" = "disable" ] || [ "$ap_amsdu" = "0" ]; then
#			ap_amsdu_val="0"
#		else
#			error_print "Unsupported value - ap_amsdu:$ap_amsdu"
#			send_error ",errorCode,230"
#			return
#		fi
#	fi
#####

	if [ "$ap_offset" != "" ]; then
		if [ "$ap_offset" = "above" ]; then
			ap_offset_val="AboveControlChannel"
		elif [ "$ap_offset" = "below" ]; then
			ap_offset_val="BelowControlChannel"
		else
			error_print "Unsupported value - ap_offset:$ap_offset"
			send_error ",errorCode,235"
			return
		fi
	fi

	if [ "$ap_sgi20" != "" ]; then
		if [ "$ap_sgi20" = "enable" ]; then
			ap_sgi20_val="400nsec"
		elif [ "$ap_sgi20" = "disable" ]; then
			ap_sgi20_val="800nsec"
		else
			error_print "Unsupported value - ap_sgi20:$ap_sgi20"
			send_error ",errorCode,240"
			return
		fi
	fi

	if [ "$ap_width" != "" ]; then
		if [ "$ap_width" = "auto" ]; then
			ap_width_val="Auto"
		elif [ "$ap_width" = "40" ]; then
			ap_width_val="40MHz"
		elif [ "$ap_width" = "20" ]; then
			ap_width_val="20MHz"
		else
			error_print "Unsupported value - ap_width:$ap_width"
			send_error ",errorCode,245"
			return
		fi
	fi

	if [ "$ap_region" != "" ]; then
		# NOTE: Do not remove the space below at the end of the string!!! TR-181 specifies 2-character country code + (space/O/I)
		# Meaning: "US " (all environment) "USO" (outside) "USI" (inside)
		ap_region="${ap_region} "
	fi

	# new clish command
	radio_params=""
	if [ "$ap_channel" != "" ]; then
		radio_params=$radio_params" Channel $ap_channel"
	fi
	if [ "$ap_mode_val" != "" ]; then
		radio_params=$radio_params" OperatingStandards $ap_mode_val"
	fi
	if [ "$ap_radio" != "" ]; then
		[ "$ap_radio" = "off" ] && ap_radio=0
		[ "$ap_radio" = "disable" ] && ap_radio=0
		[ "$ap_radio" = "disabled" ] && ap_radio=0
		[ "$ap_radio" = "on" ] && ap_radio=1
		[ "$ap_radio" = "enable" ] && ap_radio=1
		[ "$ap_radio" = "enabled" ] && ap_radio=1
		radio_params=$radio_params" radioEnable $ap_radio"
	fi
	if [ "$ap_auto_channel" != "" ]; then
		radio_params=$radio_params" AutoChannelEnable $ap_auto_channel"
	fi
	if [ "$ap_band_val" != "" ]; then
		radio_params=$radio_params" OperatingFrequencyBand $ap_band_val"
	fi
	if [ "$ap_offset_val" != "" ]; then
		radio_params=$radio_params" ExtensionChannel $ap_offset_val"
	fi
	if [ "$ap_sgi20_val" != "" ]; then
		radio_params=$radio_params" GuardInterval $ap_sgi20_val"
	fi
	if [ "$ap_width_val" != "" ]; then
		radio_params=$radio_params" OperatingChannelBandwidth $ap_width_val"
	fi
#	if [ "$ap_region" != "" ]; then
#		TODO: Why is this code commented out??
#		radio_params=$radio_params" RegulatoryDomain \"$ap_region\""
#	fi
	debug_print "radio_params:$radio_params"

	ap_params=""
	if [ "$ap_wme_val" != "" ]; then
		ap_params=$ap_params" WMMEnable $ap_wme_val"
	fi
	if [ "$ap_wmmps_val" != "" ]; then
		ap_params=$ap_params" UAPSDEnable $ap_wmmps_val"
	fi

	debug_print "ap_params:$ap_params"
	clish_config_set

	send_complete
}

ap_set_security()
{
	local vap_index

	ap_name="INTEL"
	# default to wlan0 in case no interface parameter is given
	ap_interface=""

	IFS=" "
	send_running
	# checking for interface parameter (might fail if some value is also "interface")
	get_interface_name $@

	ap_FTKeyManagment=""

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
				ap_name=$1
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
				lower "$1" ap_keymgnt
			;;
			ENCRYPT)
				shift
				lower "$1" ap_encrypt
			;;
			PSK)
				shift
				ap_psk=$1
			;;
			WEPKEY)
				shift
				ap_wepkey=$1
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
						send_invalid ",errorCode,300"
						return
					;;
				esac
			;;
			SSID)
				shift
				ap_ssid=$1
			;;
			PMF)
				shift
				lower "$1" ap_pmf
			;;
			SHA256AD)
				# ap_set_security do nothing
				shift
			;;
			AKMSUITETYPE)
				shift
				ap_akm_suite_type=$1
			;;
			PMKSACACHING)
				shift
				ap_pmks_a_caching=$1
			;;
			NONTXBSSINDEX)
				shift
				ap_non_tx_bss_index=$1
			;;
			*)
				debug_print "while loop error $1"
				send_invalid ",errorCode,305"
				return
			;;
		esac
		shift
	done

	## MBSSID security W/A
	if [ -n "$ap_non_tx_bss_index" ]; then
		([ $ap_non_tx_bss_index -lt 1 ] || [ $ap_non_tx_bss_index -gt 8 ]) && error_print "NonTxBSSIndex invalid value '$1'" && send_invalid ",errorCode,96" && return
		vap_index=$((ap_non_tx_bss_index-1))
		if [ -n "$ap_keymgnt" ] && [ -n "$ap_psk" ]; then
			[ "$ap_keymgnt" = "wpa2-psk" ] && ap_keymgnt="WPA2-Personal"
			if [ ! -f "$CMD_MBSS_SECURITY_FILE" ]; then
				echo "configure wlan" > $CMD_MBSS_SECURITY_FILE
				echo "start" >> $CMD_MBSS_SECURITY_FILE
			fi

			echo "set ap security $ap_interface.${vap_index} ModeEnabled $ap_keymgnt" >> $CMD_MBSS_SECURITY_FILE
			echo "set ap security $ap_interface.${vap_index} KeyPassphrase $ap_psk" >> $CMD_MBSS_SECURITY_FILE

			ap_non_tx_bss_index=""
			ap_keymgnt=""
			ap_psk=""
		fi
	fi

	## WLANRTSYS-12533 WPA3 setting WFA issue HE-744 TC 4.2.1
	if [ -n "$ap_keymgnt" ] && [ "$ap_keymgnt" = "sae" ]; then
		if [ ! -f "$CMD_WPA3_SECURITY_FILE" ]; then
			echo "configure wlan" > $CMD_WPA3_SECURITY_FILE
			echo "start" >> $CMD_WPA3_SECURITY_FILE
		fi

		ap_keymgnt="WPA3-Personal"
		echo "set ap security $ap_interface ModeEnabled $ap_keymgnt" >> $CMD_WPA3_SECURITY_FILE
		echo "set ap security $ap_interface SaePassword $ap_psk" >> $CMD_WPA3_SECURITY_FILE

		ap_keymgnt=""
		ap_psk=""
	fi

	local security_supported_in_11ax_dut=""
	if [ "$ap_encrypt" = "" ]; then
		ap_encrypt="none"
	elif [ "$ap_encrypt" = "wep" ]; then
		security_supported_in_11ax_dut=0
	fi

	if [ "$ap_keymgnt" != "" ]; then
		configureSecurity=0
		configureRadius=0
		case "$ap_keymgnt-$ap_encrypt" in
			none-none)
				ap_ModeEnabled="None"
				configureSecurity=0
			;;
			none-wep)
				if [ "$ap_wepEncrLvl" = "0" ]; then
					ap_ModeEnabled="WEP-64"
				else
					ap_ModeEnabled="WEP-128"
				fi
				configureSecurity=1
				ap_wps_ena=0
			;;
			wpa-psk-tkip|wpa-psk-none)
				configureSecurity=1
				ap_ModeEnabled="WPA-Personal"
				ap_wps_ena=0
				security_supported_in_11ax_dut=0
			;;
			wpa-psk-aes)
				ap_ModeEnabled="WPA-Personal"
			;;
			wpa2-psk-tkip)
				ap_ModeEnabled="WPA2-Personal"
				ap_wps_ena=0
				security_supported_in_11ax_dut=0
			;;
			wpa2-psk-aes)
				ap_ModeEnabled="WPA2-Personal"
				ap_FTKeyManagment="FT-PSK"
			;;
			wpa2-psk-none)
				# key management is sent without encryption in MBO testing. handle as if sent.
				ap_ModeEnabled="WPA2-Personal"
				ap_FTKeyManagment="FT-PSK"
				# TODO: Why is WPS disabled in this case (and only in this case?)
				ap_wps_ena=0
			;;
			wpa2-psk-mixed-none)
				ap_ModeEnabled="WPA-WPA2-Personal"
				security_supported_in_11ax_dut=0
			;;
			wpa2-psk-sae-none)
				ap_ModeEnabled="WPA2-WPA3-Personal"
			;;
			wpa2-mixed-none)
				ap_ModeEnabled="WPA-WPA2-Personal"
			;;
			wpa-ent-tkip)
				ap_ModeEnabled="WPA-Enterprise"
				ap_wps_ena=0
				security_supported_in_11ax_dut=0
			;;
			wpa-ent-aes)
				ap_ModeEnabled="WPA-Enterprise"
			;;
			wpa2-ent-tkip)
				ap_ModeEnabled="WPA2-Enterprise"
				ap_wps_ena=0
				security_supported_in_11ax_dut=0
			;;
			wpa2-ent-aes)
				ap_ModeEnabled="WPA2-Enterprise"
				ap_FTKeyManagment="FT-EAP"
			;;
			wpa2-ent-none)
				# key management is sent without encryption in MBO testing. handle as if sent.
				ap_ModeEnabled="WPA2-Enterprise"
				ap_FTKeyManagment="FT-EAP"
			;;
			*)
				error_print "Unsupported value - ap_keymgnt:$ap_keymgnt ap_encrypt:$ap_encrypt"
				send_error ",errorCode,310"
				return
			;;
		esac
	fi

	if [ "$security_supported_in_11ax_dut" = "0" ] && [ "$ucc_type" = "dut" ]; then
		local interface_index=${ap_interface/wlan/}
		# check if 11ax mode configured in DB
		local TMP1=`cat $CONF_DIR/fapi_wlan_wave_radio_conf | sed -n 's/^OperatingStandards_'"$interface_index"'=//p'`
		eval ap_radio_OperatingStandards=`printf "$TMP1"`
		if [ $(echo "$ap_radio_OperatingStandards" | grep "ax") ]; then
			error_print "Invalid configuration - ap_encrypt:$ap_encrypt"
			send_invalid ",errorCode,315,Requested configuration is not supported."
			return
		fi

		# check if 11ax mode is to be committed
		local TMP2=$(echo $CONFIG_TMP | grep "set radio" | grep "ax")
		if [ "$TMP2" != "" ]; then
			error_print "Invalid configuration - ap_encrypt:$ap_encrypt"
			send_invalid ",errorCode,320,Requested configuration is not supported."
			return
		fi
	fi

	# Required/Optional/Disabled
	if [ "$ap_pmf" = "required" ]; then
		ap_pmf="1"
		ap_pmf_required="1"
	elif [ "$ap_pmf" = "optional" ]; then
		ap_pmf="1"
		ap_pmf_required="0"
	elif [ "$ap_pmf" = "disable" ]; then
		ap_pmf="0"
		ap_pmf_required="0"
	elif [ "$ap_pmf" = "disabled" ]; then
		ap_pmf="0"
		ap_pmf_required="0"
	else
		# enable by default for MBO
		if [ "$ucc_program" = "mbo" ]; then
			ap_pmf="1"
			ap_pmf_required="0"
		fi
		# disable by default for HE
		if [ "$ucc_program" = "he" ]; then
			ap_pmf="0"
			ap_pmf_required="0"
		fi
	fi

	# new clish command
	sec_params=""
	[ "$ap_ModeEnabled" != "" ] && sec_params=$sec_params" ModeEnabled $ap_ModeEnabled"
	# TODO: Can you use "ap_wepkey" instead of a new parameter to verify write? e.g. any bad side effects if you write both wep key and passphrase when in one of these modes?
	if [ "$ap_wepkey" != "" ] && [ "$ap_pmf" != "" ]; then
		sec_params=$sec_params" WEPKey $ap_wepkey ManagementFrameProtection $ap_pmf"
	fi

	if [ "$ap_psk" != "" ]; then
		sec_params=$sec_params" KeyPassphrase $ap_psk"
	fi

	if [ "$ap_pmf" != "" ] && [ "$ap_pmf_required" != "" ]; then
		sec_params=$sec_params" ManagementFrameProtection $ap_pmf ManagementFrameProtectionRequired $ap_pmf_required"
	fi

	if [ "$ap_FTKeyManagment" != "" ]; then
		sec_params=$sec_params" FTKeyManagment $ap_FTKeyManagment"
	fi

	if [ "$ap_akm_suite_type" != "" ]; then
		sec_params=$sec_params" AKMSuiteType $ap_akm_suite_type"
	fi

	if [ "$ap_pmks_a_caching" != "" ]; then
		[ "$ap_pmks_a_caching" = "disabled" ] && ap_pmks_a_caching="true"
		[ "$ap_pmks_a_caching" = "enabled" ] && ap_pmks_a_caching="false"
		sec_params=$sec_params" PMKSACaching $ap_pmks_a_caching"
	fi
	debug_print "sec_params:$sec_params"

	clish_config_set
	send_complete

	if [ "$DEBUGGING" = "1" ]; then
		get_wlan_param_ap_security_all "ap_ssid" "ap_ModeEnabled" "ap_wepkey" "ap_psk" "ap_wps_ena"
		debug_print "***read from clish"
		debug_print "ap_ssid:$ap_ssid"
		debug_print "ap_ModeEnabled:$ap_ModeEnabled"
		debug_print "ap_wepkey:$ap_wepkey"
		debug_print "ap_psk:$ap_psk"
		debug_print "ap_wps_ena:$ap_wps_ena"
		debug_print "ap_akm_suite_type:$ap_akm_suite_type"
		debug_print "ap_pmks_a_caching:$ap_pmks_a_caching"
	fi
}

start_wps_registration()
{
	ap_name="INTEL"
	# default to wlan0 in case no interface parameter is given
	ap_interface=""

	IFS=" "
	send_running
	# checking for interface parameter (might fail if some value is also "interface")
	get_interface_name $@
	get_radio_interface_name $@
	get_interface_name $@

	while [ "$1" != "" ]; do
		# for upper case only
		upper "$1" token
		debug_print "while loop $1 - token:$token"
		case "$token" in
			NAME)
				shift
				ap_name=$1
			;;
			INTERFACE)
				# skip since it was read in loop before
				shift
			;;
			WPSROLE)
				shift
				ap_wps_role=$1
				# no param for this in our DB.
				# we are already configured correctly by default no action here.
				# By default AP is Registrar and STA is Enrollee
			;;
			WPSCONFIGMETHOD)
				shift
				ap_wps_config_method=$1
				# ConfigMethodsEnabled
				# we are already configured correctly by default no action here.
			;;
			*)
				error_print "while loop error $1"
				send_invalid ",errorCode,330"
				return
			;;
		esac
		shift
	done

	# enable WPS and commit to ensure enable before WPS push button, we are already configure to Registrar.
	if [ "$ap_wps_role" != "" ]; then
		ap_wps_ena="true"
		sec_params=""
		sec_params=$sec_params" wpsEnable $ap_wps_ena"
		clish_config_set
		clish_config_commit
	fi

	if [ "$ap_wps_config_method" != "" ]; then
		ap_push_button="PBC"
		radio_params=""
		radio_params=$radio_params" WPSAction $ap_push_button"
		clish_config_set
		clish_config_commit
	fi

	send_complete

	if [ "$DEBUGGING" = "1" ]; then
		get_wlan_param "ap security" "wpsEnable" "ap_wps_ena"
		debug_print "read from clish ap security params"
		debug_print "ap_wps_ena:$ap_wps_ena"
		get_wlan_param "radio" "WPSAction" "ap_push_button"
		debug_print "read from clish radio params"
		debug_print "ap_push_button:$ap_push_button"
	fi
}

# push the WPS button on AP
ap_set_wps_pbc()
{
	ap_name="INTEL"
	# default to wlan0 in case no interface parameter is given
	ap_interface=""

	IFS=" "
	send_running
	# checking for interface parameter (might fail if some value is also "interface")
	get_interface_name $@

	while [ "$1" != "" ]; do
		# for upper case only
		upper "$1" token
		debug_print "while loop $1 - token:$token"
		case "$token" in
			NAME)
				shift
				ap_name=$1
			;;
			INTERFACE)
				# skip since it was read in loop before
				shift
			;;
			*)
				error_print "while loop error $1"
				send_invalid ",errorCode,335"
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
	ap_interface=""

	IFS=" "
	send_running
	# checking for interface parameter (might fail if some value is also "interface")
	get_interface_name $@

	get_wlan_param "ap security" "pmfEnable" "ap_pmf"
	debug_print "***read from clish"
	debug_print "ap_pmf:$ap_pmf"

	while [ "$1" != "" ]; do
		# for upper case only
		upper "$1" token
		debug_print "while loop $1 - token:$token"
		case "$token" in
			NAME)
				shift
				ap_name=$1
			;;
			INTERFACE)
				# skip since it was read in loop before
				shift
			;;
			PMF)
				shift
				lower "$1" ap_pmf_ena
			;;
			*)
				error_print "while loop error $1"
				send_invalid ",errorCode,340"
				return
			;;
		esac
		shift
	done

	# Required/Optional/Disabled
	# TODO: Debug: Why do required and optional get the same value? Answer: In our dB we support boolean. (AP is always optional?)
	if [ "$ap_pmf_ena" = "required" ]; then
		ap_pmf="1"
	elif [ "$ap_pmf_ena" = "optional" ]; then
		ap_pmf="1"
	elif [ "$ap_pmf_ena" = "disable" ]; then
		ap_pmf="0"
	elif [ "$ap_pmf_ena" = "disabled" ]; then
		ap_pmf="0"
	else
		error_print "Unsupported value - ap_pmf_ena:$ap_pmf_ena"
		send_error ",errorCode,345"
		return
	fi

	# new clish command
	sec_params=""
	[ "$ap_pmf" != "" ] && sec_params=$sec_params" ManagementFrameProtection $ap_pmf"
	clish_config_set
	send_complete

	if [ "$DEBUGGING" = "1" ]; then
		get_wlan_param "ap security" "pmfEnable" "ap_pmf"
		debug_print "***read from clish"
		debug_print "ap_pmf:$ap_pmf"
	fi
}

ap_set_apqos()
{
	ap_name="INTEL"
	# default to wlan0 in case no interface parameter is given
	ap_interface=""

	IFS=" "
	send_running
	# checking for interface parameter (might fail if some value is also "interface")
	get_interface_name $@

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
				ap_name=$1
			;;
			INTERFACE)
				# skip as it is determined in get_interface_name
				shift
			;;
			CWMIN*)
				lower "${token#CWMIN_*}" ap_actype
				shift
				lower "$1" ap_cwmin
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
				lower "${token#CWMAX_*}" ap_actype
				shift
				lower "$1" ap_cwmax
				lower "$ap_cwmax" ap_cwmax_${ap_actype}
			;;
			AIFS*)
				lower "${token#AIFS_*}" ap_actype
				shift
				lower "$1" ap_aifs
				lower "$ap_aifs" ap_aifs_${ap_actype}
			;;
			TXOP*)
				lower "${token#TXOP_*}" ap_actype
				shift
				lower "$1" ap_txop
				lower "$ap_txop" ap_txop_${ap_actype}
			;;
			ACM*)
				lower "${token#ACM_*}" ap_actype
				shift
				lower "$1" ap_acm
				lower "$ap_acm" ap_acm_${ap_actype}
			;;
			*)
				error_print "while loop error $1"
				send_invalid ",errorCode,350"
				return
			;;
		esac
		shift
	done

	if [ "$ap_acm_vo" != "" ]; then
		if [ "$ap_acm_vo" = "on" ]; then
			ap_acm_val_vo="true"
		elif [ "$ap_acm_vo" = "off" ]; then
			ap_acm_val_vo="false"
		else
			error_print "Unsupported value - ap_acm_vo:$ap_acm_vo"
			send_error ",errorCode,355"
			return
		fi
	fi

	if [ "$ap_acm_vi" != "" ]; then
		if [ "$ap_acm_vi" = "on" ]; then
			ap_acm_val_vi="true"
		elif [ "$ap_acm_vi" = "off" ]; then
			ap_acm_val_vi="false"
		else
			error_print "Unsupported value - ap_acm_vi:$ap_acm_vi"
			send_error ",errorCode,360"
			return
		fi
	fi

	if [ "$ap_acm_bk" != "" ]; then
		if [ "$ap_acm_bk" = "on" ]; then
			ap_acm_val_bk="true"
		elif [ "$ap_acm_bk" = "off" ]; then
			ap_acm_val_bk="false"
		else
			error_print "Unsupported value - ap_acm_bk:$ap_acm_bk"
			send_error ",errorCode,365"
			return
		fi
	fi

	if [ "$ap_acm_be" != "" ]; then
		if [ "$ap_acm_be" = "on" ]; then
			ap_acm_val_be="true"
		elif [ "$ap_acm_be" = "off" ]; then
			ap_acm_val_be="false"
		else
			error_print "Unsupported value - ap_acm_be:$ap_acm_be"
			send_error ",errorCode,370"
			return
		fi
	fi

	debug_print "ap_cwmin_vo:$ap_cwmin_vo"
	debug_print "ap_cwmax_vo:$ap_cwmax_vo"
	debug_print "ap_aifs_vo:$ap_aifs_vo"
	debug_print "ap_txop_vo:$ap_txop_vo"
	debug_print "ap_acm_val_vo:$ap_acm_val_vo"

	debug_print "ap_cwmin_vi:$ap_cwmin_vi"
	debug_print "ap_cwmax_vi:$ap_cwmax_vi"
	debug_print "ap_aifs_vi:$ap_aifs_vi"
	debug_print "ap_txop_vi:$ap_txop_vi"
	debug_print "ap_acm_val_vi:$ap_acm_val_vi"

	debug_print "ap_cwmin_bk:$ap_cwmin_bk"
	debug_print "ap_cwmax_bk:$ap_cwmax_bk"
	debug_print "ap_aifs_bk:$ap_aifs_bk"
	debug_print "ap_txop_bk:$ap_txop_bk"
	debug_print "ap_acm_val_bk:$ap_acm_val_bk"

	debug_print "ap_cwmin_be:$ap_cwmin_be"
	debug_print "ap_cwmax_be:$ap_cwmax_be"
	debug_print "ap_aifs_be:$ap_aifs_be"
	debug_print "ap_txop_be:$ap_txop_be"
	debug_print "ap_acm_val_be:$ap_acm_val_be"

	# new clish command
	# TODO: Convert to new SM
	#debug_print "clish -c \"set debug on\" -c \"configure wlan\" -c \"start\" -c \"set ap ac $ap_interface ECWMin_VO $ap_cwmin_vo ECWMax_VO $ap_cwmax_vo TxOpMax_VO $ap_txop_vo AIFSN_VO $ap_aifs_vo AckPolicy_VO $ap_acm_val_vo ECWMin_VI $ap_cwmin_vi ECWMax_VI $ap_cwmax_vi TxOpMax_VI $ap_txop_vi AIFSN_VI $ap_aifs_vi AckPolicy_VI $ap_acm_val_vi ECWMin_BK $ap_cwmin_bk ECWMax_BK $ap_cwmax_bk TxOpMax_BK $ap_txop_bk AIFSN_BK $ap_aifs_bk AckPolicy_BK $ap_acm_val_bk ECWMin_BE $ap_cwmin_be ECWMax_BE $ap_cwmax_be TxOpMax_BE $ap_txop_be AIFSN_BE $ap_aifs_be AckPolicy_BE $ap_acm_val_be\" -c \"commit\""
	#clish -c "configure wlan" -c "start" -c "set ap ac $ap_interface ECWMin_VO $ap_cwmin_vo ECWMax_VO $ap_cwmax_vo TxOpMax_VO $ap_txop_vo AIFSN_VO $ap_aifs_vo AckPolicy_VO $ap_acm_val_vo" -c "set ap ac $ap_interface ECWMin_VI $ap_cwmin_vi ECWMax_VI $ap_cwmax_vi TxOpMax_VI $ap_txop_vi AIFSN_VI $ap_aifs_vi AckPolicy_VI $ap_acm_val_vi" -c "set ap ac $ap_interface ECWMin_BK $ap_cwmin_bk ECWMax_BK $ap_cwmax_bk TxOpMax_BK $ap_txop_bk AIFSN_BK $ap_aifs_bk AckPolicy_BK $ap_acm_val_bk" -c "set ap ac $ap_interface ECWMin_BE $ap_cwmin_be ECWMax_BE $ap_cwmax_be TxOpMax_BE $ap_txop_be AIFSN_BE $ap_aifs_be AckPolicy_BE $ap_acm_val_be" -c "commit" 1>&2
	#send_complete

	ap_params=""
	# check values using: clish -c "show wlan ap ac wlan2" | grep -i TxOpMax
	# cat /opt/lantiq/wave/confs/hostapd_wlan2.conf | grep -i txop_limit
	[ "$ap_txop_be" != "" ] && ap_params=$ap_params" TxOpMax_BE $ap_txop_be"
	[ "$ap_txop_bk" != "" ] && ap_params=$ap_params" TxOpMax_BK $ap_txop_bk"
	[ "$ap_txop_vi" != "" ] && ap_params=$ap_params" TxOpMax_VI $ap_txop_vi"
	[ "$ap_txop_vo" != "" ] && ap_params=$ap_params" TxOpMax_VO $ap_txop_vo"

	# check values using: clish -c "show wlan ap ac wlan2" | grep -i ECWMax
	# cat /opt/lantiq/wave/confs/hostapd_wlan2.conf | grep -i cwmax
	[ "$ap_cwmax_be" != "" ] && ap_params=$ap_params" ECWMax_BE $ap_cwmax_be"
	[ "$ap_cwmax_bk" != "" ] && ap_params=$ap_params" ECWMax_BK $ap_cwmax_bk"
	[ "$ap_cwmax_vi" != "" ] && ap_params=$ap_params" ECWMax_VI $ap_cwmax_vi"
	[ "$ap_cwmax_vo" != "" ] && ap_params=$ap_params" ECWMax_VO $ap_cwmax_vo"

	# check values using: clish -c "show wlan ap ac wlan2" | grep -i AIFSN
	# cat /opt/lantiq/wave/confs/hostapd_wlan2.conf | grep -i aifs
	[ "$ap_aifs_be" != "" ] && ap_params=$ap_params" AIFSN_BE $ap_aifs_be"
	[ "$ap_aifs_bk" != "" ] && ap_params=$ap_params" AIFSN_BK $ap_aifs_bk"
	[ "$ap_aifs_vi" != "" ] && ap_params=$ap_params" AIFSN_VI $ap_aifs_vi"
	[ "$ap_aifs_vo" != "" ] && ap_params=$ap_params" AIFSN_VO $ap_aifs_vo"

	# check values using: clish -c "show wlan ap ac wlan2" | grep -i ECWMin
	# cat /opt/lantiq/wave/confs/hostapd_wlan2.conf | grep -i cwmin
	[ "$ap_cwmin_be" != "" ] && ap_params=$ap_params" ECWMin_BE $ap_cwmin_be"
	[ "$ap_cwmin_bk" != "" ] && ap_params=$ap_params" ECWMin_BK $ap_cwmin_bk"
	[ "$ap_cwmin_vi" != "" ] && ap_params=$ap_params" ECWMin_VI $ap_cwmin_vi"
	[ "$ap_cwmin_vo" != "" ] && ap_params=$ap_params" ECWMin_VO $ap_cwmin_vo"

	# check values using: clish -c "show wlan ap ac wlan2" | grep -i AckPolicy
	# cat /opt/lantiq/wave/confs/hostapd_wlan2.conf | grep -i AckPolicy
	[ "$ap_acm_val_be" != "" ] && ap_params=$ap_params" AckPolicy_BE $ap_acm_val_be"
	[ "$ap_acm_val_bk" != "" ] && ap_params=$ap_params" AckPolicy_BK $ap_acm_val_bk"
	[ "$ap_acm_val_vi" != "" ] && ap_params=$ap_params" AckPolicy_VI $ap_acm_val_vi"
	[ "$ap_acm_val_vo" != "" ] && ap_params=$ap_params" AckPolicy_VO $ap_acm_val_vo"

	clish_config_set
	send_complete

	if [ "$DEBUGGING" = "1" ]; then
		# ac types: vo=3 vi=2 bk=1 be=0
		get_wlan_param_qos_all "ap_cwmin_vo" "ap_cwmax_vo" "ap_aifs_vo" "ap_txop_vo" "ap_acm_val_vo" "ap_cwmin_vi" "ap_cwmax_vi" "ap_aifs_vi" "ap_txop_vi" "ap_acm_val_vi" "ap_cwmin_bk" "ap_cwmax_bk" "ap_aifs_bk" "ap_txop_bk" "ap_acm_val_bk" "ap_cwmin_be" "ap_cwmax_be" "ap_aifs_be" "ap_txop_be" "ap_acm_val_be"
		debug_print "***read from clish"
		debug_print "ap_cwmin_vo:$ap_cwmin_vo"
		debug_print "ap_cwmax_vo:$ap_cwmax_vo"
		debug_print "ap_aifs_vo:$ap_aifs_vo"
		debug_print "ap_txop_vo:$ap_txop_vo"
		debug_print "ap_acm_val_vo:$ap_acm_val_vo"

		debug_print "ap_cwmin_vi:$ap_cwmin_vi"
		debug_print "ap_cwmax_vi:$ap_cwmax_vi"
		debug_print "ap_aifs_vi:$ap_aifs_vi"
		debug_print "ap_txop_vi:$ap_txop_vi"
		debug_print "ap_acm_val_vi:$ap_acm_val_vi"

		debug_print "ap_cwmin_bk:$ap_cwmin_bk"
		debug_print "ap_cwmax_bk:$ap_cwmax_bk"
		debug_print "ap_aifs_bk:$ap_aifs_bk"
		debug_print "ap_txop_bk:$ap_txop_bk"
		debug_print "ap_acm_val_bk:$ap_acm_val_bk"

		debug_print "ap_cwmin_be:$ap_cwmin_be"
		debug_print "ap_cwmax_be:$ap_cwmax_be"
		debug_print "ap_aifs_be:$ap_aifs_be"
		debug_print "ap_txop_be:$ap_txop_be"
		debug_print "ap_acm_val_be:$ap_acm_val_be"
	fi
}

ap_set_staqos()
{
	ap_name="INTEL"
	# default to wlan0 in case no interface parameter is given
	ap_interface=""

	IFS=" "
	send_running
	# checking for interface parameter (might fail if some value is also "interface")
	get_radio_interface_name $@

	stop_wlan

	# ac types: vo=3 vi=2 bk=1 be=0

	while [ "$1" != "" ]; do
		# for upper case only
		upper "$1" token
		debug_print "while loop $1 - token:$token"
		case "$token" in
			NAME)
				shift
				ap_name=$1
			;;
			INTERFACE)
				# skip since it was read in loop before
				shift
			;;
			CWMIN*)
				lower "${token#CWMIN_*}" sta_actype
				shift
				lower "$1" sta_cwmin
				lower "$sta_cwmin" sta_cwmin_${sta_actype}
				debug_print "sta_cwmin:$sta_cwmin"
				debug_print "sta_actype:$sta_actype"
				debug_print "sta_param:$sta_param"
				debug_print "sta_cwmin_vo:$sta_cwmin_vo"
				debug_print "sta_cwmin_vi:$sta_cwmin_vi"
				debug_print "sta_cwmin_bk:$sta_cwmin_bk"
				debug_print "sta_cwmin_be:$sta_cwmin_be"
			;;
			CWMAX*)
				lower "${token#CWMAX_*}" sta_actype
				shift
				lower "$1" sta_cwmax
				lower "$sta_cwmax" sta_cwmax_${sta_actype}
			;;
			AIFS*)
				lower "${token#AIFS_*}" sta_actype
				shift
				lower "$1" sta_aifs
				lower "$sta_aifs" sta_aifs_${sta_actype}
			;;
			TXOP*)
				lower "${token#TXOP_*}" sta_actype
				shift
				lower "$1" sta_txop
				lower "$sta_txop" sta_txop_${sta_actype}
			;;
			ACM*)
				# not supported for stations by mapi
				lower "${token#ACM_*}" sta_actype
				shift
				lower "$1" sta_acm
				lower "$sta_acm" sta_acm_${sta_actype}
			;;
			*)
				error_print "while loop error $1"
				send_invalid ",errorCode,375"
				return
			;;
		esac
		shift
	done
	# TODO: 1. Add STA QoS support 2. Use new SM to write efficiently

	# run-time config:
	# ----------------
	# [ "$sta_txop_be" != "" ] && hostapd_cli -i $ap_interface set wmm_ac_be_txop_limit $sta_txop_be > /dev/null && sta_hostapd_perform_reload=1
	# [ "$sta_txop_vi" != "" ] && hostapd_cli -i $ap_interface set wmm_ac_vi_txop_limit $sta_txop_vi > /dev/null && sta_hostapd_perform_reload=1
	# [ "$sta_cwmax_be" != "" ] && hostapd_cli -i $ap_interface set wmm_ac_be_cwmax $sta_cwmax_be > /dev/null && sta_hostapd_perform_reload=1
	# [ "$sta_cwmax_vi" != "" ] && hostapd_cli -i $ap_interface set wmm_ac_vi_cwmax $sta_cwmax_vi > /dev/null && sta_hostapd_perform_reload=1
	# [ "$sta_aifs_be" != "" ] && hostapd_cli -i $ap_interface set wmm_ac_be_aifs $sta_aifs_be > /dev/null && sta_hostapd_perform_reload=1
	# [ "$sta_aifs_vi" != "" ] && hostapd_cli -i $ap_interface set wmm_ac_vi_aifs $sta_aifs_vi > /dev/null && sta_hostapd_perform_reload=1
	# if [ "$sta_hostapd_perform_reload" = "1" ]; then
		# hostapd_cli -i $sta_interface reload > /dev/null
	# fi

	radio_params=""
	# check values using: clish -c "show wlan radio wlan2" | grep -i TxOpMax
	# cat /opt/lantiq/wave/confs/hostapd_wlan2.conf | grep -i txop_limit
	[ "$sta_txop_be" != "" ] && radio_params=$radio_params" STA_TxOpMax_BE $sta_txop_be"
	[ "$sta_txop_bk" != "" ] && radio_params=$radio_params" STA_TxOpMax_BK $sta_txop_bk"
	[ "$sta_txop_vi" != "" ] && radio_params=$radio_params" STA_TxOpMax_VI $sta_txop_vi"
	[ "$sta_txop_vo" != "" ] && radio_params=$radio_params" STA_TxOpMax_VO $sta_txop_vo"

	# check values using: clish -c "show wlan ap ac wlan2" | grep -i ECWMax
	# cat /opt/lantiq/wave/confs/hostapd_wlan2.conf | grep -i cwmax
	[ "$sta_cwmax_be" != "" ] && radio_params=$radio_params" STA_ECWMax_BE $sta_cwmax_be"
	[ "$sta_cwmax_bk" != "" ] && radio_params=$radio_params" STA_ECWMax_BK $sta_cwmax_bk"
	[ "$sta_cwmax_vi" != "" ] && radio_params=$radio_params" STA_ECWMax_VI $sta_cwmax_vi"
	[ "$sta_cwmax_vo" != "" ] && radio_params=$radio_params" STA_ECWMax_VO $sta_cwmax_vo"

	# check values using: clish -c "show wlan ap ac wlan2" | grep -i AIFSN
	# cat /opt/lantiq/wave/confs/hostapd_wlan2.conf | grep -i aifs
	[ "$sta_aifs_be" != "" ] && radio_params=$radio_params" STA_AIFSN_BE $sta_aifs_be"
	[ "$sta_aifs_bk" != "" ] && radio_params=$radio_params" STA_AIFSN_BK $sta_aifs_bk"
	[ "$sta_aifs_vi" != "" ] && radio_params=$radio_params" STA_AIFSN_VI $sta_aifs_vi"
	[ "$sta_aifs_vo" != "" ] && radio_params=$radio_params" STA_AIFSN_VO $sta_aifs_vo"

	# check values using: clish -c "show wlan ap ac wlan2" | grep -i ECWMin
	# cat /opt/lantiq/wave/confs/hostapd_wlan2.conf | grep -i cwmin
	[ "$sta_cwmin_be" != "" ] && radio_params=$radio_params" STA_ECWMin_BE $sta_cwmin_be"
	[ "$sta_cwmin_bk" != "" ] && radio_params=$radio_params" STA_ECWMin_BK $sta_cwmin_bk"
	[ "$sta_cwmin_vi" != "" ] && radio_params=$radio_params" STA_ECWMin_VI $sta_cwmin_vi"
	[ "$sta_cwmin_vo" != "" ] && radio_params=$radio_params" STA_ECWMin_VO $sta_cwmin_vo"

	clish_config_set
	send_complete
}

ap_set_radius()
{
	ap_name="INTEL"
	# default to wlan0 in case no interface parameter is given
	ap_interface=""
	IFS=" "
	# checking for interface parameter (might fail if some value is also "interface")
	send_running
	get_interface_name $@

	while [ "$1" != "" ]; do
		# for upper case only
		upper "$1" token
		debug_print "while loop $1 - token:$token"
		case "$token" in
			NAME)
				shift
				ap_name=$1
			;;
			INTERFACE)
				# skip since it was read in loop before
				shift
			;;
			IPADDR)
				shift
				ap_ipaddr=$1
			;;
			PORT)
				shift
				ap_port=$1
			;;
			PASSWORD)
				shift
				ap_password=$1
			;;
			*)
				error_print "while loop error $1"
				send_invalid ",errorCode,380"
				return
			;;
		esac
		shift
	done

	# new clish command
	sec_params=""
	if [ "$ap_ipaddr" != "" ]; then
		sec_params=$sec_params" RadiusServerIPAddr $ap_ipaddr"
	fi

	if [ "$ap_port" != "" ]; then
		sec_params=$sec_params" RadiusServerPort $ap_port"
	fi

	if [ "$ap_password" != "" ]; then
		sec_params=$sec_params" RadiusSecret $ap_password"
	fi

#	if [ "$sec_params" != "" ]; then
#		sec_params=$sec_params" ModeEnabled WPA2-Enterprise"
#	fi

	clish_config_set
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
				ap_name=$1
			;;
				INTERWORKING)
				wlancli -c set_wlan_hs_config -P hs20Mode "$1"
			;;
			ACCS_NET_TYPE)
				wlancli -c set_wlan_hs_config -P accessNetType "$1"
			;;
			INTERNET)
				wlancli -c set_wlan_hs_config -P internetConnectivity "$1"
			;;
			VENUE_GRP)
				wlancli -c set_wlan_hs_config -P venueGroup "$1"
			;;
			VENUE_TYPE)
				wlancli -c set_wlan_hs_config -P venueType "$1"
			;;
			HESSID)
				wlancli -c set_wlan_hs_config -P hessid "$1"
			;;
			ROAMING_CONS)
				wlancli -c set_wlan_hs_roam_consort_list -P delList
				for val in `echo "$1" | tr ";" "\n"`
				do
					wlancli -c set_wlan_hs_roam_consort_list -P roamConsort "$val"
				done
			;;
			DGAF_DISABLE)
				wlancli -c set_wlan_hs_config -P dgafDisabled "$1"
			;;
			ANQP)
				# ap_set_hs2 do nothing
			;;
			NET_AUTH_TYPE)
				case "$1" in
					1)
						wlancli -c set_wlan_hs_config -P netAuthType 00 netAuthUrl "https://tandc-server.wi-fi.org"
					;;
				esac
			;;
			NAI_REALM_LIST)
				wlancli -c set_wlan_hs_nai_realm_list -P delList
				case "$1" in
					1)
						wlancli -c set_wlan_hs_nai_realm_list -P addNaiRealmName "mail.example.com" addNaiRealmEap1 1
						wlancli -c set_wlan_hs_nai_realm_list -P addNaiRealmName "cisco.com" addNaiRealmEap1 1
						wlancli -c set_wlan_hs_nai_realm_list -P addNaiRealmName "wi-fi.org" addNaiRealmEap1 1 addNaiRealmEap2 0
						wlancli -c set_wlan_hs_nai_realm_list -P addNaiRealmName "example.com" addNaiRealmEap1 0
					;;
					2)
						wlancli -c set_wlan_hs_nai_realm_list -P addNaiRealmName "wi-fi.org" addNaiRealmEap1 1
					;;
					3)
						wlancli -c set_wlan_hs_nai_realm_list -P addNaiRealmName "cisco.com" addNaiRealmEap1 1
						wlancli -c set_wlan_hs_nai_realm_list -P addNaiRealmName "wi-fi.org" addNaiRealmEap1 1 addNaiRealmEap2 0
						wlancli -c set_wlan_hs_nai_realm_list -P addNaiRealmName "example.com" addNaiRealmEap1 0
					;;
					4)
						wlancli -c set_wlan_hs_nai_realm_list -P addNaiRealmName "mail.example.com" addNaiRealmEap1 1 addNaiRealmEap2 0
					;;
				esac
			;;
			DOMAIN_LIST)
				wlancli -c set_wlan_hs_domain_name_list -P delList
				case "$1" in
					1)
						wlancli -c set_wlan_hs_domain_name_list -P addDomainName "WFA: wi-fi.org"
					;;
				esac
			;;
			OPER_NAME)
				wlancli -c set_wlan_hs_op_name_list -P delList
				case "$1" in
					1)
						wlancli -c set_wlan_hs_op_name_list -P addOpFriendlyName "eng:Wi-Fi Alliance"
						wlancli -c set_wlan_hs_op_name_list -P addOpFriendlyName "chi:Wi-Fi¿?¿?"
					;;
				esac
			;;
			VENUE_NAME)
				wlancli -c set_wlan_hs_venue_name_list -P delList
				case "$1" in
					1)
						wlancli -c set_wlan_hs_venue_name_list -P addVenueName 'P"eng:Wi-Fi Alliance\n2989 Copper Road\nSanta Clara, CA 95051, USA"'
					;;
				esac
			;;
			GAS_CB_DELAY)
				wlancli -c set_wlan_hs_config -P gasComebackDelay "$1"
			;;
			MIH)
				# ap_set_hs2 do nothing
			;;
			L2_TRAFFIC_INSPECT)
				# ap_set_hs2 do nothing
			;;
			BCST_UNCST)
				# ap_set_hs2 do nothing
			;;
			PLMN_MCC)
				PLMN_MCC_VAL="$1"
			;;
			PLMN_MNC)
				PLMN_MNC_VAL="$1"
				PLMN_MCC_VAL=`echo $PLMN_MCC_VAL | tr ';' ' '`
				I=1
				for MCC_VAL in $PLMN_MCC_VAL
				do
					eval MNC_VAL=`echo $PLMN_MNC_VAL | cut -d ';' -f$I`
					THREE_GPP_STRING="$THREE_GPP_STRING$MCC_VAL,$MNC_VAL;"
					let "I++"
				done
				THREE_GPP_STRING=`echo ${THREE_GPP_STRING%?}` #remove unneeded ";" at the end of the string
				wlancli -c set_wlan_hs_config -P threeGpp "$THREE_GPP_STRING"
			;;
			PROXY_ARP)
				wlancli -c set_wlan_hs_config -P proxyArp "$1"
			;;
			WAN_METRICS)
				case "$1" in
					1)
						wlancli -c set_wlan_hs_wan_metric -P wanMetric "01:2500:384:0:0:0"
						# if the 6th column equal to 0, the 4th and 5th column value is irrelevant
					;;
				esac
			;;
			CONN_CAP)
				wlancli -c set_wlan_hs_conn_cap_list -P delList
				case "$1" in
					1)
						wlancli -c set_wlan_hs_conn_cap_list -P addConnectionCap "6:20:1"
						wlancli -c set_wlan_hs_conn_cap_list -P addConnectionCap "6:80:1"
						wlancli -c set_wlan_hs_conn_cap_list -P addConnectionCap "6:443:1"
						wlancli -c set_wlan_hs_conn_cap_list -P addConnectionCap "17:244:1"
						wlancli -c set_wlan_hs_conn_cap_list -P addConnectionCap "17:4500:1"
					;;
				esac
			;;
			IP_ADD_TYPE_AVAIL)
				case "$1" in
					1)
						wlancli -c set_wlan_hs_config -P ipv4AddrType 3 ipv6AddrType 1
					;;
				esac
			;;
			ICMPv4_ECHO)
				wlancli -c set_wlan_hs_l2_firewall_list -P addAction "$1" addProtocol 1
			;;
			OPER_CLASS)
				case "$1" in
					1)
						wlancli -c set_wlan_hs_config -P operatingClass 51
					;;
					2)
						wlancli -c set_wlan_hs_config -P operatingClass 73
					;;
					3)
						wlancli -c set_wlan_hs_config -P operatingClass 5173
					;;
				esac
			;;
			*)
				error_print "while loop error $1"
				send_invalid ",errorCode,385"
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
	ap_interface=""

	IFS=" "
	send_running
	# checking for interface parameter (might fail if some value is also "interface")
	get_interface_name $@

	while [ "$1" != "" ]; do
		# for upper case only
		upper "$1" token
		debug_print "while loop $1 - token:$token"
		case "$token" in
			NAME)
				shift
				ap_name=$1
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
				ap_type=$1
			;;
			BSS_TRANSITION)
				# do nothing
				shift
			;;
			NSS_MCS_OPT)
				shift
				ap_nss_mcs_opt=$1
			;;
			OPT_MD_NOTIF_IE)
				shift
				ap_opt_md_notif_ie=$1
			;;
			CHNUM_BAND)
				shift
				ap_chnum_band=$1
			;;
			BTM_DISASSOCIMNT)
				shift
				ap_btmreq_disassoc_imnt=$1
			;;
			BTMREQ_DISASSOC_IMNT)
				shift
				ap_btmreq_disassoc_imnt=$1
			;;
			BTMREQ_TERM_BIT)
				shift
				ap_btmreq_term_bit=$1
			;;
			BTM_BSSTERM)
				shift
				ap_btm_bssterm=$1
			;;
			BSS_TERM_DURATION)
				shift
				ap_btm_bssterm=$1
			;;
			ASSOC_DISALLOW)
				shift
				ap_assoc_disallow=$1
			;;
			DISASSOC_TIMER)
				shift
				ap_disassoc_timer=$1
			;;
			ASSOC_DELAY)
				shift
				ap_assoc_delay=$1
			;;
			NEBOR_BSSID)
				shift
				ap_nebor_bssid=$1
			;;
			NEBOR_OP_CLASS)
				shift
				ap_nebor_op_class=$1
			;;
			NEBOR_OP_CH)
				shift
				ap_nebor_op_ch=$1
			;;
			NEBOR_PREF)
				shift
				ap_nebor_priority=$1
			;;
			BSS_TERM_TSF)
				shift
				ap_bssTermTSF=$1
			;;
			PROGRAM)
				shift
				ap_program=$1
			;;
			DOWNLINKAVAILCAP)
				shift
				ap_down_link_avail_cap=$1
			;;
			UPLINKAVAILCAP)
				shift
				ap_up_link_avail_cap=$1
			;;
			RSSITHRESHOLD)
				shift
				ap_rssi_threshold=$1
			;;
			RETRYDELAY)
				shift
				ap_retry_delay=$1
			;;
			TXPOWER)
				shift
				ap_tx_power=$1
			;;
			TXBANDWIDTH)
				shift
				ap_txbandwidth=$1
			;;
			LTF)
				shift
				ap_ltf=$1
			;;
			GI)
				shift
				ap_gi=$1
			;;
			RUALLOCTONES)
				shift
				ap_rualloctones=$1
			;;
			ACKPOLICY)
				shift
				ap_ack_policy=$1
			;;
			ACKPOLICY_MAC)
				shift
				ap_ack_policy_mac=$1
			;;
			TRIGGER_TXBF)
				# do nothing
				shift
				# TODO: currently this command is passive. Should affect the OFDMA TF BF only.
				#ap_trigger_tx_bf=$1
			;;
			TRIGGERTYPE)
				shift
				ap_trigger_type=$1
			;;
			AID)
				shift
				ap_sta_aid=$1
			;;
			TRIG_COMINFO_BW)
				shift
				ap_cominfo_bw=$1
			;;
			TRIG_COMINFO_GI-LTF)
				shift
				ap_cominfo_gi_ltf=$1
			;;
			TRIG_COMINFO_APTXPOWER)
				shift
				ap_cominfo_ap_tx_power=$1
			;;
			TRIG_COMINFO_TARGETRSSI)
				shift
				ap_cominfo_target_rssi=$1
			;;
			TRIG_USRINFO_SSALLOC_RA-RU)
				shift
				ap_usrinfo_ss_alloc_ra_ru=$1
			;;
			TRIG_USRINFO_RUALLOC)
				shift
				ap_usrinfo_ru_alloc=$1
			;;
			NUMSS)
				shift
				ap_numss=$1
			;;
			NUMSS_MAC)
				shift
				ap_numss_mac=$1
			;;
			TRIGGERCODING)
				shift
				ap_trigger_coding=$1
			;;
			ACKTYPE)
				# do nothing
				shift
			;;
			LDPC)
				# do nothing (TBD:related to 5.49.1:its not clear why we have it here should be only in ap_set_wireless)
				shift
			;;
			OMCTRL_CHNLWIDTH)
				shift
				ap_omctrl_chnlwidth=$1
			;;
			OMCTRL_RXNSS)
				shift
				ap_omctrl_rxnss=$1
			;;
			ADDBARESP)
				shift
				ap_addbaresp=$1
			;;
			ADDBAREQ)
				shift
				ap_addbareq=$1
			;;
			STA_MUEDCA_ECWMIN_BE)
				shift
				ap_sta_muedca_ecwmin_be=$1
			;;
			STA_MUEDCA_ECWMIN_VI)
				shift
				ap_sta_muedca_ecwmin_vi=$1
			;;
			STA_MUEDCA_ECWMIN_VO)
				shift
				ap_sta_muedca_ecwmin_vo=$1
			;;
			STA_MUEDCA_ECWMIN_BK)
				shift
				ap_sta_muedca_ecwmin_bk=$1
			;;
			STA_MUEDCA_ECWMAX_BE)
				shift
				ap_sta_muedca_ecwmax_be=$1
			;;
			STA_MUEDCA_ECWMAX_VI)
				shift
				ap_sta_muedca_ecwmax_vi=$1
			;;
			STA_MUEDCA_ECWMAX_VO)
				shift
				ap_sta_muedca_ecwmax_vo=$1
			;;
			STA_MUEDCA_ECWMAX_BK)
				shift
				ap_sta_muedca_ecwmax_bk=$1
			;;
			STA_WMMPE_ECWMIN_VI)
				shift
				ap_sta_wmmpe_ecwmin_vi=$1
			;;
			STA_WMMPE_ECWMAX_VI)
				shift
				ap_sta_wmmpe_ecwmax_vi=$1
			;;
			STA_MUEDCA_AIFSN_BE)
				shift
				ap_sta_muedca_aifsn_be=$1
			;;
			STA_MUEDCA_AIFSN_VI)
				shift
				ap_sta_muedca_aifsn_vi=$1
			;;
			STA_MUEDCA_AIFSN_VO)
				shift
				ap_sta_muedca_aifsn_vo=$1
			;;
			STA_MUEDCA_AIFSN_BK)
				shift
				ap_sta_muedca_aifsn_bk=$1
			;;
			STA_MUEDCA_TIMER_BE)
				shift
				ap_sta_muedca_timer_be=$1
			;;
			STA_MUEDCA_TIMER_VI)
				shift
				ap_sta_muedca_timer_vi=$1
			;;
			STA_MUEDCA_TIMER_VO)
				shift
				ap_sta_muedca_timer_vo=$1
			;;
			STA_MUEDCA_TIMER_BK)
				shift
				ap_sta_muedca_timer_bk=$1
			;;
			CLIENT_MAC)
				shift
				ap_client_mac=$1
			;;
			PPDUTXTYPE)
				shift
				ap_ppdutxtype=$1
			;;
			DISABLETRIGGERTYPE)
				shift
				ap_disable_trigger_type=$1
			;;
			TXOPDURATION)
				# do nothing
				# WLANRTSYS-11513 TC 5.61.1
				shift
			;;
			TRIG_INTERVAL)
				shift
				ap_trig_interval=$1
			;;
			TRIG_ULLENGTH)
				shift
				ap_trig_ullength=$1
			;;
			TRANSMITOMI)
				# do nothing
				# for test # 5.56.1
				shift
			;;
			MPDU_MU_SPACINGFACTOR)
				shift
				ap_mpdu_mu_spacingfactor=$1
			;;
			TRIG_COMINFO_ULLENGTH)
				shift
				ap_trig_cominfo_ullength=$1
			;;
			TRIG_USRINFO_UL-MCS)
				shift
				ap_trig_usrinfo_ul_mcs=$1
			;;
			TRIG_USRINFO_UL-TARGET-RSSI)
				shift
				ap_trig_usrinfo_ul_target_rssi=$1
			;;
			HE_TXOPDURRTSTHR)
				shift
				ap_he_txop_dur_rts_thr=$1
			;;
			TRIG_COMINFO_CSREQUIRED)
				# do nothing
				# for test # 5.62.1: in the FW 'commonTfInfoPtr->tfCsRequired' = 1 by default
				if [ "$1" != "1" ]; then
					error_print "Trig_ComInfo_CSRequired = '$1' ; only '1' is supported"
					send_invalid ",errorCode,390"
					return
				fi
				shift
			;;
			*)
				error_print "while loop error $1 ; token = $token"
				send_invalid ",errorCode,400"
				return
			;;
		esac
		shift
	done

	debug_print "ap_program:$ap_program"

	# TODO: move to a common function
	# make sure there is no running clish command
	while [ -n "$(pgrep -f clish)" ]; do
		info_print "ap_set_rfeature: clish command is on-going. waiting ..."
		sleep 1
	done
	debug_print "ap_set_rfeature: clish command finished. continue ..."

	# new clish command
	ap_btm_params=""
	ap_btm_param_found="0"
	ap_bss_neighbor_set_params=""
	ap_bss_neighbor_set_param_found="0"

	local is_activate_sigmaManagerDaemon=0

	if [ "$ap_trigger_type" != "" ]; then
		info_print "iwpriv $ap_interface sDoSimpleCLI 70 1"
		iwpriv $ap_interface sDoSimpleCLI 70 1

		# JIRA WLANRTSYS-9307: in case "TRIGGERTYPE" was set, activate the SMD
		is_activate_sigmaManagerDaemon=1
	fi

	if [ "$ap_btmreq_term_bit" != "" ] || [ "$ap_btm_bssterm" != "" ] || [ "$ap_btmreq_disassoc_imnt" != "" ] || [ "$ap_disassoc_timer" != "" ] || [ "$ap_assoc_delay" != "" ] || [ "$ap_bssTermTSF" != "" ]; then
		ap_btm_param_found="1"
		if [ "$ap_bssTermTSF" != "" ]; then
			# Save TSF to be used in next BTM Req
			g_bssTermTSF=$ap_bssTermTSF
			ap_bssTermTSF=$(($ap_bssTermTSF * 1000))
			ap_btm_params=$ap_btm_params" bssTermTSF=$ap_bssTermTSF"
		fi
		if [ "$ap_btm_bssterm" != "" ]; then
			# Save duration to be used in next BTM Req
			g_bssTermDuration=$ap_btm_bssterm
			ap_btm_params=$ap_btm_params" bssTermDuration=$ap_btm_bssterm"
#		else
#			ap_btm_params=$ap_btm_params" bssTermDuration=0" # mandatory parameter set to default value 0
		fi
		if [ "$ap_btmreq_disassoc_imnt" != "" ]; then
			ap_btm_params=$ap_btm_params" disassocImminent=$ap_btmreq_disassoc_imnt"
#		else
#			ap_btm_params=$ap_btm_params" disassocImminent=1" # mandatory parameter set to default value 1
		fi
		if [ "$ap_btmreq_term_bit" != "" ]; then
			ap_btm_params=$ap_btm_params" btmReqTermBit=$ap_btmreq_term_bit"
#		else
#			ap_btm_params=$ap_btm_params" btmReqTermBit=NULL"
		fi
		if [ "$ap_disassoc_timer" != "" ]; then
			ap_btm_params=$ap_btm_params" disassocTimer=$ap_disassoc_timer"
#		else
#			ap_btm_params=$ap_btm_params" disassocTimer=NULL" # optional parameter
		fi
		if [ "$ap_assoc_delay" != "" ]; then
			ap_btm_params=$ap_btm_params" reassocDelay=$ap_assoc_delay"
#		else
#			ap_btm_params=$ap_btm_params" reassocDelay=NULL" # optional parameter
		fi
		debug_print "ap_btm_params:$ap_btm_params"
	fi

	if [ "$ap_nebor_bssid" != "" ] || [ "$ap_nebor_op_class" != "" ] || [ "$ap_nebor_op_ch" != "" ] || [ "$ap_nebor_priority" != "" ]; then
		ap_bss_neighbor_set_param_found="1"
		if [ "$ap_nebor_bssid" != "" ]; then
			bss_neighbor_set_mac="$ap_nebor_bssid"
		fi
		if [ "$ap_nebor_op_class" != "" ]; then
			ap_bss_neighbor_set_params=$ap_bss_neighbor_set_params" opClass=$ap_nebor_op_class"
		else
			if [ "$AP_IF_ACTIVE" = "wlan2" ]; then
				ap_bss_neighbor_set_params=$ap_bss_neighbor_set_params" opClass=115"
			else
				ap_bss_neighbor_set_params=$ap_bss_neighbor_set_params" opClass=81"
			fi
		fi
		if [ "$ap_nebor_op_ch" != "" ]; then
			ap_bss_neighbor_set_params=$ap_bss_neighbor_set_params" channelNumber=$ap_nebor_op_ch"
		else
			if [ "$AP_IF_ACTIVE" = "wlan2" ]; then
				ap_bss_neighbor_set_params=$ap_bss_neighbor_set_params" channelNumber=36"
			else
				ap_bss_neighbor_set_params=$ap_bss_neighbor_set_params" channelNumber=1"
			fi
		fi
		if [ "$ap_nebor_priority" != "" ]; then
			ap_bss_neighbor_set_params=$ap_bss_neighbor_set_params" priority=$ap_nebor_priority"
		fi
		if [ "$ap_program" != "" ]; then
			[ "$ap_program" = "OCE" ] && ap_program=1
			ap_bss_neighbor_set_params=$ap_bss_neighbor_set_params" oce=$ap_program"
		fi
		debug_print "ap_bss_neighbor_set_params:$ap_bss_neighbor_set_params"
	fi

	if [ "$ap_btm_param_found" = "1" ]; then
		# lets jump here for the moment
		debug_print "/usr/sbin/fapi_wlan_debug_cli COMMAND \"AML_BTM_PARAMS_SET $ap_interface $ap_btm_params\""
		/usr/sbin/fapi_wlan_debug_cli COMMAND "AML_BTM_PARAMS_SET $ap_interface $ap_btm_params" >> $CONF_DIR/BeeRock_CMD.log
	fi
	if [ "$ap_bss_neighbor_set_param_found" = "1" ]; then
		debug_print "/usr/sbin/fapi_wlan_debug_cli COMMAND \"AML_BSS_NEIGHBOR_SET $ap_interface $bss_neighbor_set_mac $ap_bss_neighbor_set_params\""
		/usr/sbin/fapi_wlan_debug_cli COMMAND "AML_BSS_NEIGHBOR_SET $ap_interface $bss_neighbor_set_mac $ap_bss_neighbor_set_params" 1 >> $CONF_DIR/BeeRock_CMD.log
	fi

	if [ "$ap_assoc_disallow" = "Enable" ]; then
		debug_print "/usr/sbin/fapi_wlan_debug_cli COMMAND \"AML_ASSOC_DISALLOW $ap_interface 1\""
		/usr/sbin/fapi_wlan_debug_cli COMMAND "AML_ASSOC_DISALLOW $ap_interface 1" >> $CONF_DIR/BeeRock_CMD.log
	elif [ "$ap_assoc_disallow" = "Disable" ]; then
		debug_print "/usr/sbin/fapi_wlan_debug_cli COMMAND \"AML_ASSOC_DISALLOW $ap_interface 0\""
		/usr/sbin/fapi_wlan_debug_cli COMMAND "AML_ASSOC_DISALLOW $ap_interface 0" >> $CONF_DIR/BeeRock_CMD.log
	fi

	if [ "$ap_down_link_avail_cap" != "" ]; then
		debug_print "/usr/sbin/fapi_wlan_debug_cli COMMAND \"OCE_WAN_METRICS_SET $ap_interface downlink_capacity=$ap_down_link_avail_cap\""
		/usr/sbin/fapi_wlan_debug_cli COMMAND "OCE_WAN_METRICS_SET $ap_interface downlink_capacity=$ap_down_link_avail_cap" >> $CONF_DIR/BeeRock_CMD.log

	fi
	if [ "$ap_up_link_avail_cap" != "" ]; then
		debug_print "/usr/sbin/fapi_wlan_debug_cli COMMAND \"OCE_WAN_METRICS_SET $ap_interface uplink_capacity=$ap_up_link_avail_cap\""
		/usr/sbin/fapi_wlan_debug_cli COMMAND "OCE_WAN_METRICS_SET $ap_interface uplink_capacity=$ap_up_link_avail_cap" >> $CONF_DIR/BeeRock_CMD.log

	fi

	if [ "$ap_rssi_threshold" != "" ]; then
		debug_print "/usr/sbin/fapi_wlan_debug_cli COMMAND \"OCE_ASSOC_REJECT_SET $ap_interface 1 $ap_rssi_threshold\""
		/usr/sbin/fapi_wlan_debug_cli COMMAND "OCE_ASSOC_REJECT_SET $ap_interface 1 min_rssi_threshold=$ap_rssi_threshold" >> $CONF_DIR/BeeRock_CMD.log
	fi

	if [ "$ap_retry_delay" != "" ]; then
		debug_print "/usr/sbin/fapi_wlan_debug_cli COMMAND \"OCE_ASSOC_REJECT_SET $ap_interface 1 $ap_retry_delay\""
		/usr/sbin/fapi_wlan_debug_cli COMMAND "OCE_ASSOC_REJECT_SET $ap_interface 1 retry_delay=$ap_retry_delay" >> $CONF_DIR/BeeRock_CMD.log
	fi

	if [ "$ap_tx_power" != "" ]; then
		[ "$ap_tx_power" = "low" ] && ap_tx_power="6"
		[ "$ap_tx_power" = "high" ] && ap_tx_power="58"
		debug_print "/usr/sbin/fapi_wlan_debug_cli COMMAND \"AML_FIXED_TX_POWER_SET $ap_interface $ap_tx_power\""
		/usr/sbin/fapi_wlan_debug_cli COMMAND "AML_FIXED_TX_POWER_SET $ap_interface $ap_tx_power" >> $CONF_DIR/BeeRock_CMD.log
	fi

	local CMD_FILE="/tmp/sigma-cmds"

	# 11AX PF#5: Workaround for supporting AddbaResp and AddbaReq: Send Debug CLI directly
	if [ "$ap_addbaresp" = "Accepted" ] || [ "$ap_addbareq" = "Enable" ]  ; then
		# TODO: Verify if AID is a single value in all tests that use addbaresp, or else parse in a loop as seen elsewhere in this file (currently only used in 5.52.1)
		let ap_sta_id=$ap_sta_aid-1
		iwpriv $ap_interface sDoSimpleCLI 1 $ap_sta_id 0 1
		iwpriv $ap_interface sDoSimpleCLI 1 $ap_sta_id 1 1
	fi
	if [ "$ap_addbaresp" = "Refused" ] || [ "$ap_addbareq" = "Disable" ]  ; then
		iwpriv $ap_interface sDoSimpleCLI 1 $ap_sta_id 0 0
		iwpriv $ap_interface sDoSimpleCLI 1 $ap_sta_id 1 0
	fi

	# initialize clish command file for all following commands
	echo "configure wlan" > $CMD_FILE
	echo "start" >> $CMD_FILE

	if [ "$ap_nss_mcs_opt" != "" ]; then
		debug_print "ap_nss_mcs_opt:$ap_nss_mcs_opt"
		ap_nss_opt=${ap_nss_mcs_opt%%;*}
		ap_mcs_opt=${ap_nss_mcs_opt##*;}

		if [ "$ap_nss_opt" = "def" ]; then
			# change the NSS to the default value
			ap_nss_opt="$nss_def_val_dl"
			[ "$glob_ofdma_phase_format" = "1" ] && ap_nss_opt="$nss_def_val_ul"
			if [ "$ucc_type" = "testbed" ]; then
				ap_nss_opt="$nss_def_val_dl_testbed"
				[ "$glob_ofdma_phase_format" = "1" ] && ap_nss_opt="$nss_def_val_ul_testbed"
			fi
		fi

		if [ "$ap_mcs_opt" = "def" ]; then
			# change the MCS to the default value
			ap_mcs_opt="$mcs_def_val_dl"
			[ "$glob_ofdma_phase_format" = "1" ] && ap_mcs_opt="$mcs_def_val_ul"
			if [ "$ucc_type" = "testbed" ]; then
				ap_mcs_opt="$mcs_def_val_dl_testbed"
				[ "$glob_ofdma_phase_format" = "1" ] && ap_mcs_opt="$mcs_def_val_ul_testbed"
			fi
		fi

		# set for SU, only if not OFDMA MU TC
		if [ "$glob_ofdma_phase_format" = "" ]; then
			echo "set radio $ap_interface WaveFRNss ${ap_nss_opt}" >> $CMD_FILE
			echo "set radio $ap_interface WaveFRMcs ${ap_mcs_opt}" >> $CMD_FILE
			echo "set radio $ap_interface WaveFRAutoRate 0" >> $CMD_FILE
			debug_print "ap_type:$ap_type"
			if [ "$ap_type" = "HE" ]; then
				echo "set radio $ap_interface WaveFRPhyMode ax" >> $CMD_FILE
				echo "set radio $ap_interface WaveFRCpMode 5" >> $CMD_FILE
			fi
		fi

		# JIRA WLANRTSYS-9813: check whether the previously set value of nss is lower than the current one; if so, set the one we just got (the higher one)
		if [ -n "$ap_nss_opt" ] && [ -n "$global_nss_opt_ul" ] && [ -n "$global_nss_opt_dl" ]; then
			if [ $ap_nss_opt -gt $global_nss_opt_ul ] || [ $ap_nss_opt -gt $global_nss_opt_dl ]; then
				local nss_mcs_val=`get_nss_mcs_val $ap_nss_opt $ap_mcs_opt`

				if [ $ap_nss_opt -gt $global_nss_opt_ul ]; then
					echo "set radio $ap_interface HeheMcsNssRxHeMcsMapLessThanOrEqual80Mhz ${nss_mcs_val}" >> $CMD_FILE
					# JIRA WLANRTSYS-11028: part0-Rx part1-Tx
					echo "set radio $ap_interface WaveVhtMcsSetPart0 ${nss_mcs_val}" >> $CMD_FILE
				fi

				if [ $ap_nss_opt -gt $global_nss_opt_dl ]; then
					echo "set radio $ap_interface HeheMcsNssTxHeMcsMapLessThanOrEqual80Mhz ${nss_mcs_val}" >> $CMD_FILE
					# JIRA WLANRTSYS-11028: part0-Rx part1-Tx
					echo "set radio $ap_interface WaveVhtMcsSetPart1 ${nss_mcs_val}" >> $CMD_FILE
				fi
			fi
		fi

		# calculate the OFDMA MU NSS-MCS value (NSS: bits 5-4, MCS: bits 3-0)
		let ap_ofdma_mu_nss_mcs_val="($ap_nss_opt-1)*16+$ap_mcs_opt"

		# set for MU
		for ap_usr_index in 1 2 3 4
		do
			# DL
			if [ "$glob_ofdma_phase_format" = "0" ]; then
				echo "set radio $ap_interface WaveSPDlUsrPsduRatePerUsp${ap_usr_index} ${ap_ofdma_mu_nss_mcs_val}" >> $CMD_FILE
			elif [ "$glob_ofdma_phase_format" = "1" ]; then
			# UL
				echo "set radio $ap_interface WaveSPDlUsrUlPsduRatePerUsp${ap_usr_index} ${ap_ofdma_mu_nss_mcs_val}" >> $CMD_FILE
			fi
		done

		[ "$glob_ofdma_phase_format" != "" ] && ap_check_plan_active=1
		ap_clish_commit=1
	fi

	if [ "$ap_trigger_tx_bf" != "" ]; then
		debug_print "ap_trigger_tx_bf:$ap_trigger_tx_bf"
		if [ "$ap_trigger_tx_bf" = "disable" ]; then
			ap_trigger_tx_bf_val="Disabled"
		elif [ "$ap_trigger_tx_bf" = "enable" ]; then
			ap_trigger_tx_bf_val="Auto"
		else
			error_print "Unsupported value - ap_trigger_tx_bf:$ap_trigger_tx_bf"
			send_invalid ",errorCode,410"
		fi
		echo "set radio $ap_interface WaveBfMode ${ap_trigger_tx_bf_val}" >> $CMD_FILE
		ap_clish_commit=1
	fi

	if [ "$ap_txbandwidth" != "" ]; then
		debug_print "ap_txbandwidth:$ap_txbandwidth"

		# set for SU, only if not OFDMA MU TC
		if [ "$glob_ofdma_phase_format" = "" ]; then
			echo "set radio $ap_interface WaveFRBandwidth ${ap_txbandwidth}MHz" >> $CMD_FILE
			debug_print "ap_type:$ap_type"
			if [ "$ap_type" = "HE" ]; then
				echo "set radio $ap_interface WaveFRPhyMode ax" >> $CMD_FILE
				echo "set radio $ap_interface WaveFRCpMode 5" >> $CMD_FILE

				# JIRA WLANRTSYS-9189: remove the call to 'is_test_case_permitted_to_set_channel' - always set the channel
				echo "set radio $ap_interface OperatingChannelBandwidth ${ap_txbandwidth}MHz" >> $CMD_FILE
			fi
		else
			# JIRA WLANRTSYS-9189: remove the call to 'is_test_case_permitted_to_set_channel' - always set the channel
			echo "set radio $ap_interface OperatingChannelBandwidth ${ap_txbandwidth}MHz" >> $CMD_FILE
		fi

		# set for MU
		[ "$ap_txbandwidth" = "20" ] && ap_txbandwidth=0
		[ "$ap_txbandwidth" = "40" ] && ap_txbandwidth=1
		[ "$ap_txbandwidth" = "80" ] && ap_txbandwidth=2
		[ "$ap_txbandwidth" = "160" ] && ap_txbandwidth=3
		echo "set radio $ap_interface WaveSPTxopComStartBwLimit ${ap_txbandwidth}" >> $CMD_FILE

		ap_num_participating_users=`get_plan_num_users $ap_interface`

		local dl_sub_band1 dl_start_ru1 dl_ru_size1
		local dl_sub_band2 dl_start_ru2 dl_ru_size2
		local dl_sub_band3 dl_start_ru3 dl_ru_size3
		local dl_sub_band4 dl_start_ru4 dl_ru_size4

		# update 4 user plan according to BW - W/A to be align to WFA UCC.
		# 0-20MHz, 1-40MHz, 2-80MHz, 3-160MHz
		case "$ap_txbandwidth" in
			"0")
				if [ $ap_num_participating_users -gt 2 ]; then
					#USER1
					dl_sub_band1=0;dl_start_ru1=0;dl_ru_size1=1
					#USER2
					dl_sub_band2=0;dl_start_ru2=2;dl_ru_size2=1
					#USER3
					dl_sub_band3=0;dl_start_ru3=5;dl_ru_size3=1
					#USER4
					dl_sub_band4=0;dl_start_ru4=7;dl_ru_size4=1
				else
					#USER1
					dl_sub_band1=0;dl_start_ru1=0;dl_ru_size1=2
					#USER2
					dl_sub_band2=0;dl_start_ru2=5;dl_ru_size2=2
				fi
			;;
			"1")
				if [ $ap_num_participating_users -gt 2 ]; then
					#USER1
					dl_sub_band1=0;dl_start_ru1=0;dl_ru_size1=2
					#USER2
					dl_sub_band2=0;dl_start_ru2=5;dl_ru_size2=2
					#USER3
					dl_sub_band3=1;dl_start_ru3=0;dl_ru_size3=2
					#USER4
					dl_sub_band4=1;dl_start_ru4=5;dl_ru_size4=2
				else
					#USER1
					dl_sub_band1=0;dl_start_ru1=0;dl_ru_size1=3
					#USER2
					dl_sub_band2=1;dl_start_ru2=0;dl_ru_size2=3
				fi
			;;
			"2")
				if [ $ap_num_participating_users -gt 2 ]; then
					#USER1
					dl_sub_band1=0;dl_start_ru1=0;dl_ru_size1=3
					#USER2
					dl_sub_band2=1;dl_start_ru2=0;dl_ru_size2=3
					#USER3
					dl_sub_band3=2;dl_start_ru3=0;dl_ru_size3=3
					#USER4
					dl_sub_band4=3;dl_start_ru4=0;dl_ru_size4=3
				else
					#USER1
					dl_sub_band1=0;dl_start_ru1=0;dl_ru_size1=4;
					#USER2
					dl_sub_band2=2;dl_start_ru2=0;dl_ru_size2=4;
				fi
			;;
			"3")
				if [ $ap_num_participating_users -gt 2 ]; then
					#USER1
					dl_sub_band1=0;dl_start_ru1=0;dl_ru_size1=4
					#USER2
					dl_sub_band2=2;dl_start_ru2=0;dl_ru_size2=4
					#USER3
					dl_sub_band3=4;dl_start_ru3=0;dl_ru_size3=4
					#USER4
					dl_sub_band4=6;dl_start_ru4=0;dl_ru_size4=4
				else
					#USER1
					dl_sub_band1=0;dl_start_ru1=0;dl_ru_size1=5
					#USER2
					dl_sub_band2=4;dl_start_ru2=0;dl_ru_size2=5
				fi
			;;
		esac

		## WLANRTSYS-12035
		if [ $dl_ru_size1 -lt 2 ]; then
			echo "set radio $ap_interface WaveSPDlComMaximumPpduTransmissionTimeLimit 2300" >> $CMD_FILE
		else
			echo "set radio $ap_interface WaveSPDlComMaximumPpduTransmissionTimeLimit 2700" >> $CMD_FILE
		fi

		# update per-user params in DB
		ap_user_list="1,2"
		[ $ap_num_participating_users -gt 2 ] && ap_user_list="1,2,3,4"
		for usr_index in $ap_user_list
		do
			local tmp_param tmp_val
			tmp_param="dl_sub_band$usr_index";eval tmp_val=\$$tmp_param
			echo "set radio $ap_interface WaveSPDlUsrSubBandPerUsp$usr_index ${tmp_val}" >> $CMD_FILE
			tmp_param="dl_start_ru$usr_index";eval tmp_val=\$$tmp_param
			echo "set radio $ap_interface WaveSPDlUsrStartRuPerUsp$usr_index ${tmp_val}" >> $CMD_FILE
			tmp_param="dl_ru_size$usr_index";eval tmp_val=\$$tmp_param
			echo "set radio $ap_interface WaveSPDlUsrRuSizePerUsp$usr_index ${tmp_val}" >> $CMD_FILE
		done

		[ "$glob_ofdma_phase_format" != "" ] && ap_check_plan_active=1
		ap_clish_commit=1
	fi

	if [ "$ap_ltf" != "" ] || [ "$ap_gi" != "" ]; then
		debug_print "ap_ltf:$ap_ltf ap_gi:$ap_gi"
		if [ "$ap_ltf" = "6.4" ] && [ "$ap_gi" = "0.8" ]; then
			ap_su_ltf_gi="He0p8usCP2xLTF"
			ap_mu_dl_com_he_cp=0
			ap_mu_dl_com_he_ltf=1
			if [ "$glob_ofdma_phase_format" = "1" ]; then
				# this LTF and GI combination is not supported in MU UL
				error_print "Unsupported value - glob_ofdma_phase_format:$glob_ofdma_phase_format ap_ltf:$ap_ltf ap_gi:$ap_gi"
				send_invalid ",errorCode,420"
				return
			fi
		elif [ "$ap_gi" = "1.6" ]; then
			# JIRA WLANRTSYS-9350: in this case, handle not getting "ap_ltf" as if it has the value of "6.4"
			if [ "$ap_ltf" = "" ] || [ "$ap_ltf" = "6.4" ]; then
				ap_su_ltf_gi="He1p6usCP2xLTF"
				ap_mu_dl_com_he_cp=1
				ap_mu_dl_com_he_ltf=1
				ap_mu_ul_com_he_cp=1
				ap_mu_ul_com_he_ltf=1
				ap_mu_ul_com_he_tf_cp_and_ltf=1
				ap_mu_tf_len=3094
			fi
		elif [ "$ap_ltf" = "12.8" ] && [ "$ap_gi" = "3.2" ]; then
			ap_su_ltf_gi="He3p2usCP4xLTF"
			ap_mu_dl_com_he_cp=2
			ap_mu_dl_com_he_ltf=2
			ap_mu_ul_com_he_cp=2
			ap_mu_ul_com_he_ltf=2
			ap_mu_ul_com_he_tf_cp_and_ltf=2
			ap_mu_tf_len=2914
		else
			# all other LTF and GI combinations are not required by WFA
			error_print "Unsupported value - ap_ltf:$ap_ltf ap_gi:$ap_gi"
			send_invalid ",errorCode,430"
			return
		fi

		debug_print "ap_su_ltf_gi:$ap_su_ltf_gi ap_mu_dl_com_he_cp:$ap_mu_dl_com_he_cp ap_mu_dl_com_he_ltf:$ap_mu_dl_com_he_ltf"
		debug_print "ap_mu_ul_com_he_cp:$ap_mu_ul_com_he_cp ap_mu_ul_com_he_ltf:$ap_mu_ul_com_he_ltf ap_mu_ul_com_he_tf_cp_and_ltf:$ap_mu_ul_com_he_tf_cp_and_ltf ap_mu_tf_len:$ap_mu_tf_len"

		# set for SU, only if not in OFDMA MU SP
		[ "$glob_ofdma_phase_format" = "" ] && echo "set radio $ap_interface WaveltfAndGiFixedRate Fixed WaveltfAndGiValue ${ap_su_ltf_gi}" >> $CMD_FILE

		# set for MU
		[ "$ap_mu_dl_com_he_cp" != "" ] && echo "set radio $ap_interface WaveSPDlComHeCp ${ap_mu_dl_com_he_cp}" >> $CMD_FILE
		[ "$ap_mu_dl_com_he_ltf" != "" ] && echo "set radio $ap_interface WaveSPDlComHeLtf ${ap_mu_dl_com_he_ltf}" >> $CMD_FILE
		[ "$ap_mu_ul_com_he_cp" != "" ] && echo "set radio $ap_interface WaveSPUlComHeCp ${ap_mu_ul_com_he_cp}" >> $CMD_FILE
		[ "$ap_mu_ul_com_he_ltf" != "" ] && echo "set radio $ap_interface WaveSPUlComHeLtf ${ap_mu_ul_com_he_ltf}" >> $CMD_FILE
		[ "$ap_mu_ul_com_he_tf_cp_and_ltf" != "" ] && echo "set radio $ap_interface WaveSPRcrComTfHegiAndLtf ${ap_mu_ul_com_he_tf_cp_and_ltf}" >> $CMD_FILE
		[ "$ap_mu_tf_len" != "" ] && echo "set radio $ap_interface WaveSPRcrComTfLength ${ap_mu_tf_len}" >> $CMD_FILE

		[ "$glob_ofdma_phase_format" != "" ] && ap_check_plan_active=1
		ap_clish_commit=1
	fi

	# handle RU allocation for DL
	if [ "$ap_rualloctones" != "" ]; then
		debug_print "ap_rualloctones:$ap_rualloctones"

		# replace all ':' with " "
		ap_rualloctones=${ap_rualloctones//:/,}
		# ap_rualloctones implicitly holds the number of users.

		local user_index user_list index user_value start_bw_limit
		local dl_sub_band1 dl_start_ru1 dl_ru_size1
		local dl_sub_band2 dl_start_ru2 dl_ru_size2
		local dl_sub_band3 dl_start_ru3 dl_ru_size3
		local dl_sub_band4 dl_start_ru4 dl_ru_size4

		if [ "$ap_txbandwidth" != "" ]; then
			# if exist, get the bw from previous parameter in this command
			start_bw_limit=$ap_txbandwidth
		else
			# else, get the bw from the SP
			start_bw_limit=`get_plan_bw $ap_interface`
		fi
		user_index=0

		for user_value in $ap_rualloctones
		do
			let user_index=$user_index+1

			### BW=160MHz ###
			if [ "$start_bw_limit" = "3" ]; then
				if [ "$user_value" = "106" ]; then
					case "$user_index" in
						"1") #USER1
							dl_sub_band1=0;dl_start_ru1=0;dl_ru_size1=2
						;;
						"2") #USER2
							dl_sub_band2=2;dl_start_ru2=0;dl_ru_size2=2
						;;
						"3") #USER3
							dl_sub_band3=4;dl_start_ru3=0;dl_ru_size3=2
						;;
						"4") #USER4
							dl_sub_band4=6;dl_start_ru4=0;dl_ru_size4=2
						;;
					esac
				elif [ "$user_value" = "242" ]; then
					case "$user_index" in
						"1") #USER1
							dl_sub_band1=0;dl_start_ru1=0;dl_ru_size1=3
						;;
						"2") #USER2
							dl_sub_band2=2;dl_start_ru2=0;dl_ru_size2=3
						;;
						"3") #USER3
							dl_sub_band3=4;dl_start_ru3=0;dl_ru_size3=3
						;;
						"4") #USER4
							dl_sub_band4=6;dl_start_ru4=0;dl_ru_size4=3
						;;
					esac
				elif [ "$user_value" = "484" ]; then
					case "$user_index" in
						"1") #USER1
							dl_sub_band1=0;dl_start_ru1=0;dl_ru_size1=4
						;;
						"2") #USER2
							dl_sub_band2=2;dl_start_ru2=0;dl_ru_size2=4
						;;
						"3") #USER3
							dl_sub_band3=4;dl_start_ru3=0;dl_ru_size3=4
						;;
						"4") #USER4
							dl_sub_band4=6;dl_start_ru4=0;dl_ru_size4=4
						;;
					esac
				elif [ "$user_value" = "996" ]; then
					case "$user_index" in
						"1") #USER1
							dl_sub_band1=0;dl_start_ru1=0;dl_ru_size1=5
						;;
						"2") #USER2
							dl_sub_band2=4;dl_start_ru2=0;dl_ru_size2=5
						;;
						"3") #USER3 - not supported
							error_print "cannot set user${user_index} with ru=${user_value}"
							send_invalid ",errorCode,640"
							return
						;;
						"4") #USER4 - not supported
							error_print "cannot set user${user_index} with ru=${user_value}"
							send_invalid ",errorCode,645"
							return
						;;
					esac
				else
					error_print "cannot set user${user_index} with ru=${user_value}"
					send_invalid ",errorCode,650"
					return
				fi

			### BW=80MHz ###
			elif [ "$start_bw_limit" = "2" ]; then
				if [ "$user_value" = "26" ]; then
					case "$user_index" in
						"1") #USER1
							dl_sub_band1=0;dl_start_ru1=0;dl_ru_size1=0
						;;
						"2") #USER2
							dl_sub_band2=1;dl_start_ru2=0;dl_ru_size2=0
						;;
						"3") #USER3
							dl_sub_band3=2;dl_start_ru3=0;dl_ru_size3=0
						;;
						"4") #USER4
							dl_sub_band4=3;dl_start_ru4=0;dl_ru_size4=0
						;;
					esac
				elif [ "$user_value" = "52" ]; then
					case "$user_index" in
						"1") #USER1
							dl_sub_band1=0;dl_start_ru1=0;dl_ru_size1=1
						;;
						"2") #USER2
							dl_sub_band2=1;dl_start_ru2=0;dl_ru_size2=1
						;;
						"3") #USER3
							dl_sub_band3=2;dl_start_ru3=0;dl_ru_size3=1
						;;
						"4") #USER4
							dl_sub_band4=3;dl_start_ru4=0;dl_ru_size4=1
						;;
					esac
				elif [ "$user_value" = "106" ]; then
					case "$user_index" in
						"1") #USER1
							dl_sub_band1=0;dl_start_ru1=0;dl_ru_size1=2
						;;
						"2") #USER2
							dl_sub_band2=1;dl_start_ru2=0;dl_ru_size2=2
						;;
						"3") #USER3
							dl_sub_band3=2;dl_start_ru3=0;dl_ru_size3=2
						;;
						"4") #USER4
							dl_sub_band4=3;dl_start_ru4=0;dl_ru_size4=2
						;;
					esac
				elif [ "$user_value" = "242" ]; then
					case "$user_index" in
						"1") #USER1
							dl_sub_band1=0;dl_start_ru1=0;dl_ru_size1=3
						;;
						"2") #USER2
							dl_sub_band2=1;dl_start_ru2=0;dl_ru_size2=3
						;;
						"3") #USER3
							dl_sub_band3=2;dl_start_ru3=0;dl_ru_size3=3
						;;
						"4") #USER4
							dl_sub_band4=3;dl_start_ru4=0;dl_ru_size4=3
						;;
					esac
				elif [ "$user_value" = "484" ]; then
					case "$user_index" in
						"1") #USER1
							dl_sub_band1=0;dl_start_ru1=0;dl_ru_size1=4
						;;
						"2") #USER2
							dl_sub_band2=2;dl_start_ru2=0;dl_ru_size2=4
						;;
						"3") #USER3 - not supported
							error_print "cannot set user${user_index} with ru=${user_value}"
							send_invalid ",errorCode,440"
							return
						;;
						"4") #USER4 - not supported
							error_print "cannot set user${user_index} with ru=${user_value}"
							send_invalid ",errorCode,441"
							return
						;;
					esac
				elif [ "$user_value" = "996" ]; then
					case "$user_index" in
						"1") #USER1
							dl_sub_band1=0;dl_start_ru1=0;dl_ru_size1=5
						;;
						"2") #USER2
							error_print "cannot set user${user_index} with ru=${user_value}"
							send_invalid ",errorCode,442"
							return
						;;
						"3") #USER3 - not supported
							error_print "cannot set user${user_index} with ru=${user_value}"
							send_invalid ",errorCode,443"
							return
						;;
						"4") #USER4 - not supported
							error_print "cannot set user${user_index} with ru=${user_value}"
							send_invalid ",errorCode,444"
							return
						;;
					esac
				else
					error_print "cannot set user${user_index} with ru=${user_value}"
					send_invalid ",errorCode,445"
					return
				fi

			### BW=40MHz ###
			elif [ "$start_bw_limit" = "1" ]; then
				if [ "$user_value" = "26" ]; then
					case "$user_index" in
						"1") #USER1
							dl_sub_band1=0;dl_start_ru1=0;dl_ru_size1=0
						;;
						"2") #USER2
							dl_sub_band2=0;dl_start_ru2=5;dl_ru_size2=0
						;;
						"3") #USER3
							dl_sub_band3=1;dl_start_ru3=0;dl_ru_size3=0
						;;
						"4") #USER4
							dl_sub_band4=1;dl_start_ru4=5;dl_ru_size4=0
						;;
					esac
				elif [ "$user_value" = "52" ]; then
					case "$user_index" in
						"1") #USER1
							dl_sub_band1=0;dl_start_ru1=0;dl_ru_size1=1
						;;
						"2") #USER2
							dl_sub_band2=0;dl_start_ru2=5;dl_ru_size2=1
						;;
						"3") #USER3
							dl_sub_band3=1;dl_start_ru3=0;dl_ru_size3=1
						;;
						"4") #USER4
							dl_sub_band4=1;dl_start_ru4=5;dl_ru_size4=1
						;;
					esac
				elif [ "$user_value" = "106" ]; then
					case "$user_index" in
						"1") #USER1
							dl_sub_band1=0;dl_start_ru1=0;dl_ru_size1=2
						;;
						"2") #USER2
							dl_sub_band2=0;dl_start_ru2=5;dl_ru_size2=2
						;;
						"3") #USER3
							dl_sub_band3=1;dl_start_ru3=0;dl_ru_size3=2
						;;
						"4") #USER4
							dl_sub_band4=1;dl_start_ru4=5;dl_ru_size4=2
						;;
					esac
				elif [ "$user_value" = "242" ]; then
					case "$user_index" in
						"1") #USER1
							dl_sub_band1=0;dl_start_ru1=0;dl_ru_size1=3
						;;
						"2") #USER2
							dl_sub_band2=1;dl_start_ru2=0;dl_ru_size2=3
						;;
						"3") #USER3 - not supported
							error_print "cannot set user${user_index} with ru=${user_value}"
							send_invalid ",errorCode,446"
							return
						;;
						"4") #USER4 - not supported
							error_print "cannot set user${user_index} with ru=${user_value}"
							send_invalid ",errorCode,447"
							return
						;;
					esac
				else
					error_print "cannot set user${user_index} with ru=${user_value}"
					send_invalid ",errorCode,448"
					return
				fi

			### BW=20MHz ###
			elif [ "$start_bw_limit" = "0" ]; then
				if [ "$user_value" = "26" ]; then
					case "$user_index" in
						"1") #USER1
							dl_sub_band1=0;dl_start_ru1=0;dl_ru_size1=0
						;;
						"2") #USER2
							dl_sub_band2=0;dl_start_ru2=2;dl_ru_size2=0
						;;
						"3") #USER3
							dl_sub_band3=0;dl_start_ru3=5;dl_ru_size3=0
						;;
						"4") #USER4
							dl_sub_band4=0;dl_start_ru4=7;dl_ru_size4=0
						;;
					esac
				elif [ "$user_value" = "52" ]; then
					case "$user_index" in
						"1") #USER1
							dl_sub_band1=0;dl_start_ru1=0;dl_ru_size1=1
						;;
						"2") #USER2
							dl_sub_band2=0;dl_start_ru2=2;dl_ru_size2=1
						;;
						"3") #USER3
							dl_sub_band3=0;dl_start_ru3=5;dl_ru_size3=1
						;;
						"4") #USER4
							dl_sub_band4=0;dl_start_ru4=7;dl_ru_size4=1
						;;
					esac
				elif [ "$user_value" = "106" ]; then
					case "$user_index" in
						"1") #USER1
							dl_sub_band1=0;dl_start_ru1=0;dl_ru_size1=2
						;;
						"2") #USER2
							dl_sub_band2=0;dl_start_ru2=5;dl_ru_size2=2
						;;
						"3") #USER3
							error_print "cannot set user${user_index} with ru=${user_value}"
							send_invalid ",errorCode,450"
							return
						;;
						"4") #USER4
							error_print "cannot set user${user_index} with ru=${user_value}"
							send_invalid ",errorCode,451"
							return
						;;
					esac
				elif [ "$user_value" = "242" ]; then
					case "$user_index" in
						"1") #USER1
							dl_sub_band1=0;dl_start_ru1=0;dl_ru_size1=3
						;;
						"2") #USER2
							error_print "cannot set user${user_index} with ru=${user_value}"
							send_invalid ",errorCode,452"
							return
						;;
						"3") #USER3
							error_print "cannot set user${user_index} with ru=${user_value}"
							send_invalid ",errorCode,453"
							return
						;;
						"4") #USER4
							error_print "cannot set user${user_index} with ru=${user_value}"
							send_invalid ",errorCode,454"
							return
						;;
					esac
				elif [ "$user_value" = "484" ]; then
					error_print "cannot set user${user_index} with ru=${user_value}"
					send_invalid ",errorCode,455"
					return
				else
					error_print "cannot set user${user_index} with ru=${user_value}"
					send_invalid ",errorCode,456"
					return
				fi
			else
				error_print "Unsupported value - start_bw_limit:$start_bw_limit"
				send_invalid ",errorCode,457"
				return
			fi
		done

		# user_index contains the number of users. set it to DB to be used by static plan.
		echo "set radio $ap_interface WaveSPDlComNumOfParticipatingStations ${user_index}" >> $CMD_FILE

		## WLANRTSYS-12035
		if [ $dl_ru_size1 -lt 2 ]; then
			echo "set radio $ap_interface WaveSPDlComMaximumPpduTransmissionTimeLimit 2300" >> $CMD_FILE
		else
			echo "set radio $ap_interface WaveSPDlComMaximumPpduTransmissionTimeLimit 2700" >> $CMD_FILE
		fi

		# update per-user params in DB, per number of users
		#for index in $user_index
		user_list="1,2"
		[ "$user_index" = "4" ] && user_list="1,2,3,4"
		for index in $user_list
		do
			local tmp_param tmp_val
			tmp_param="dl_sub_band${index}";eval tmp_val=\$$tmp_param
			echo "set radio $ap_interface WaveSPDlUsrSubBandPerUsp${index} ${tmp_val}" >> $CMD_FILE
			tmp_param="dl_start_ru${index}";eval tmp_val=\$$tmp_param
			echo "set radio $ap_interface WaveSPDlUsrStartRuPerUsp${index} ${tmp_val}" >> $CMD_FILE
			tmp_param="dl_ru_size${index}";eval tmp_val=\$$tmp_param
			echo "set radio $ap_interface WaveSPDlUsrRuSizePerUsp${index} ${tmp_val}" >> $CMD_FILE
		done

		# dynamically update STA index in DB
		ap_aid_list=`cat /proc/net/mtlk/$ap_interface/Debug/sta_list | awk '{print $3}' | tr  "\n" ","`
		ap_aid_list=${ap_aid_list##*AID}
		ap_aid_list="${ap_aid_list##,,}"
		ap_aid_list="${ap_aid_list%%,,}"

		index=0
		debug_print "ap_aid_list:$ap_aid_list"
		# assure that aid list is not empty (i.e. it contains one ',' when no sta connected)
		if [ "$ap_aid_list" != "," ]; then
			for ap_aid_index in $ap_aid_list
			do
				let index=index+1
				[ $ap_aid_index -gt 0 ] && let ap_sta_index=$ap_aid_index-1
				echo "set radio $ap_interface WaveSPDlUsrUspStationIndexes${index} ${ap_sta_index}" >> $CMD_FILE
			done
		fi

		ap_check_plan_active=1
		ap_clish_commit=1
	fi

	# ap_sequence_type is dependent on AckPolicy and TriggerType, but not both CAPI parameters are always sent.
	# FW 'HeMuSequence_e' enum:
    #           HE_MU_SEQ_MU_BAR = 0,
    #           HE_MU_SEQ_VHT_LIKE,
    #           HE_MU_SEQ_DL_BASIC_TF,
    #           HE_MU_SEQ_VHT_LIKE_IMM_ACK,
    #           HE_MU_SEQ_VHT_LIKE_PROTECTION,
    #           HE_MU_UL,
    #           HE_MU_BSRP,
    #           HE_MU_BSRP_UL,
	# The following two ifs handle these permutations (except HE_MU_UL, which is set by ap_set_wireless)

	# 1. Handle cases where AckPolicy was sent, either with or without TriggerType
	if [ "$ap_ack_policy" != "" ]; then
		debug_print "ap_ack_policy:$ap_ack_policy, ap_trigger_type:$ap_trigger_type"

		case "$ap_ack_policy" in
		0)
			# Ack Policy set to Normal Ack (internal name: immediate Ack)
			# we use the ap_ack_policy_mac to set the requested user as primary.

			# Ack Policy MAC address handling
			if [ "$ap_ack_policy_mac" != "" ]; then
				ap_aid=`cat /proc/net/mtlk/${ap_interface}/Debug/sta_list | grep "0" | grep "$ap_ack_policy_mac" | awk '{print $3}'`
				[ "$ap_aid" = "" ] && error_print ""MAC not found: ap_ack_policy_mac:$ap_ack_policy_mac""
			fi
			info_print "ap_interface:$ap_interface ap_ack_policy:$ap_ack_policy ap_ack_policy_mac:$ap_ack_policy_mac"
			ap_sequence_type=3  #HE_MU_SEQ_VHT_LIKE_IMM_ACK
			;;
		1)
			# Ack Policy set to No Ack
			# nothing to do. not supported yet.
			info_print "ap_interface:$ap_interface ap_ack_policy:$ap_ack_policy"
		;;
		2)
			# Ack Policy set to Implicit Ack (internal name: immediate Ack on Aggr., VHT-like)
			# nothing to do. not supported yet.
			info_print "ap_interface:$ap_interface ap_ack_policy:$ap_ack_policy"
		;;
		3)
			# Ack Policy set to Block Ack (internal name: sequential BAR, VHT-like)
			# this is the default value, but we anyway return it to default.
			info_print "ap_interface:$ap_interface ap_ack_policy:$ap_ack_policy"
			ap_sequence_type=1  #HE_MU_SEQ_VHT_LIKE

			# MU-BAR scenario TC 5.51 & 4.45 and WLANRTSYS-11971 update
			if [ "$ap_trigger_type" = "2" ]; then
				ap_sequence_type=0  #HE_MU_SEQ_MU_BAR
				echo "set radio $ap_interface WaveSPRcrComTfLength 310" >> $CMD_FILE

				for ap_usr_index in 1 2 3 4
				do
					echo "set radio $ap_interface WaveSPDlUsrUlPsduRatePerUsp${ap_usr_index} 0" >> $CMD_FILE
					echo "set radio $ap_interface WaveSPRcrTfUsrPsduRate${ap_usr_index} 0" >> $CMD_FILE
				done

				ap_clish_commit=1
			fi
		;;
		4)
			# Ack Policy set to Unicast TF Basic
			if [ "$ap_trigger_type" = "0" ]; then
				ap_sequence_type=2  #HE_MU_SEQ_DL_BASIC_TF
			fi
		;;
		*)
			error_print "Unsupported value - ap_ack_policy:$ap_ack_policy"
			send_invalid ",errorCode,460"
			return
		;;
		esac
	fi

    # FW 'TfType_e' enum:
    #           TF_TYPE_BASIC = 0,
    #           TF_TYPE_BF_RPT_POLL,
    #           TF_TYPE_MU_BAR,
    #           TF_TYPE_MU_RTS,
    #           TF_TYPE_BUFFER_STATUS_RPT

	# 2. Handle cases where TriggerType was sent without AckPolicy
	if [ "$ap_ack_policy" = "" ] && [ "$ap_trigger_type" != "" ]; then
		info_print "ap_interface:$ap_interface ap_trigger_type:$ap_trigger_type"
		case "$ap_trigger_type" in
		0)
			# BASIC - do nothing. We passed without configuring this.
		;;
		1)
			# BF_RPT_POLL (MU-BRP)
			ap_sequence_type=1  #HE_MU_SEQ_VHT_LIKE
			if [ "$global_mu_txbf" != "" ] && [ "$global_mu_txbf" = "enable" ]; then
				# for test # 5.57
				glob_ofdma_phase_format=0
			else
				glob_ofdma_phase_format=2
			fi
			echo "set radio $ap_interface WaveSPDlComPhasesFormat ${glob_ofdma_phase_format}" >> $CMD_FILE

			for ap_usr_index in 1 2
			do
				echo "set radio $ap_interface WaveSPDlUsrUlPsduRatePerUsp${ap_usr_index} 4" >> $CMD_FILE
				echo "set radio $ap_interface WaveSPRcrTfUsrPsduRate${ap_usr_index} 4" >> $CMD_FILE
			done

			ap_clish_commit=1
		;;
		3)
			# MU-RTS
			ap_sequence_type=4  #HE_MU_SEQ_VHT_LIKE_PROTECTION
			ap_clish_commit=1
		;;
		4)
			# BUFFER_STATUS_RPT (BSRP)
			ap_sequence_type=6  #HE_MU_BSRP
			echo "set radio $ap_interface WaveSPRcrComTfLength 106" >> $CMD_FILE  # (was 109) UL_LEN = 109 (together with PE_dis=1) causes T_PE to exceed 16usec, which is not according to standard
			echo "set radio $ap_interface WaveSPDlComNumberOfPhaseRepetitions 0" >> $CMD_FILE
			ap_clish_commit=1
		;;
		*)
			error_print "Unsupported value - ap_ack_policy empty and ap_trigger_type:$ap_trigger_type"
			send_invalid ",errorCode,461"
			return
		;;
		esac
	fi

	# update the phase format, only if needed
	if [ "$ap_sequence_type" != "" ]; then
		echo "set radio $ap_interface WaveSPSequenceType ${ap_sequence_type}" >> $CMD_FILE

		ap_interface_index=${ap_interface/wlan/}

		# update the primary sta, only if needed
		if [ "$ap_aid" != "" ]; then
			# convert aid=1,2,3,... to  sta_id=0,1,2,...
			let ap_sta_id=$ap_aid-1

			# find the user id of the requested primary sta
			for ap_user_id in 1 2 3 4
			do
				local TMP1=`cat $CONF_DIR/fapi_wlan_wave_radio_conf | sed -n 's/^WaveSPDlUsrUspStationIndexes'"$ap_user_id"'_'"$ap_interface_index"'=//p'`
				eval ap_sta_index=`printf $TMP1`
				# save to user id prim (the user id that contains the requested sta id)
				[ "$ap_sta_index" = "$ap_sta_id" ] && ap_user_id_prim=$ap_user_id
			done

			# switch the OFDMA users, so the primary sta id will be at user 1 (first user). not needed if it is already in user 1.
			if [ "$ap_user_id_prim" != "1" ]; then
				# 1. load the sta id of user1
				local TMP2=`cat $CONF_DIR/fapi_wlan_wave_radio_conf | sed -n 's/^WaveSPDlUsrUspStationIndexes1_'"$ap_interface_index"'=//p'`
				eval ap_orig_sta_index=`printf $TMP2`

				# 2. store the primary sta id in user 1
				echo "set radio $ap_interface WaveSPDlUsrUspStationIndexes1 ${ap_sta_id}" >> $CMD_FILE

				# 3. store the original sta id from user 1 in the found user
				echo "set radio $ap_interface WaveSPDlUsrUspStationIndexes${ap_user_id_prim} ${ap_orig_sta_index}" >> $CMD_FILE
			fi
		fi

		ap_check_plan_active=1
		ap_clish_commit=1
	fi

	if [ "$ap_cominfo_bw" != "" ]; then
		debug_print "ap_cominfo_bw:$ap_cominfo_bw"
		echo "set radio $ap_interface WaveSPTxopComStartBwLimit ${ap_cominfo_bw}" >> $CMD_FILE

		ap_check_plan_active=1
		ap_clish_commit=1
	fi

	if [ "$ap_cominfo_gi_ltf" != "" ]; then
		debug_print "ap_cominfo_gi_ltf:$ap_cominfo_gi_ltf"
		if [ "$ap_cominfo_gi_ltf" = "1" ]; then
			ap_mu_ul_com_he_cp=1
			ap_mu_ul_com_he_ltf=1
			ap_mu_ul_com_he_tf_cp_and_ltf=1
			ap_mu_tf_len=`sp_set_plan_tf_length $ap_interface 3094`
		elif [ "$ap_cominfo_gi_ltf" = "2" ]; then
			ap_mu_ul_com_he_cp=2
			ap_mu_ul_com_he_ltf=2
			ap_mu_ul_com_he_tf_cp_and_ltf=2
			ap_mu_tf_len=2914
		else
			# all other LTF and GI combinations are not required by WFA
			error_print "Unsupported value - ap_ltf:$ap_ltf ap_gi:$ap_gi"
			send_invalid ",errorCode,482"
			return
		fi
		debug_print "ap_mu_ul_com_he_cp:$ap_mu_ul_com_he_cp ap_mu_ul_com_he_ltf:$ap_mu_ul_com_he_ltf ap_mu_ul_com_he_tf_cp_and_ltf:$ap_mu_ul_com_he_tf_cp_and_ltf ap_mu_tf_len:$ap_mu_tf_len"
		[ "$ap_mu_ul_com_he_cp" != "" ] && echo "set radio $ap_interface WaveSPUlComHeCp ${ap_mu_ul_com_he_cp}" >> $CMD_FILE
		[ "$ap_mu_ul_com_he_ltf" != "" ] && echo "set radio $ap_interface WaveSPUlComHeLtf ${ap_mu_ul_com_he_ltf}" >> $CMD_FILE
		[ "$ap_mu_ul_com_he_tf_cp_and_ltf" != "" ] && echo "set radio $ap_interface WaveSPRcrComTfHegiAndLtf ${ap_mu_ul_com_he_tf_cp_and_ltf}" >> $CMD_FILE
		[ "$ap_mu_tf_len" != "" ] && echo "set radio $ap_interface WaveSPRcrComTfLength ${ap_mu_tf_len}" >> $CMD_FILE

		ap_check_plan_active=1
		ap_clish_commit=1
	fi

	if [ "$ap_cominfo_ap_tx_power" != "" ]; then
		debug_print "ap_cominfo_ap_tx_power:$ap_cominfo_ap_tx_power"
		echo "set radio $ap_interface WaveSPDlComRfPower ${ap_cominfo_ap_tx_power}" >> $CMD_FILE

		ap_check_plan_active=1
		ap_clish_commit=1
	fi

	if [ "$ap_cominfo_target_rssi" != "" ]; then
		debug_print "ap_cominfo_target_rssi:$ap_cominfo_target_rssi"
		#echo "set radio $ap_interface TBD ${ap_cominfo_target_rssi}" >> $CMD_FILE

		ap_check_plan_active=1
		ap_clish_commit=1
	fi

	if [ "$ap_usrinfo_ss_alloc_ra_ru" != "" ]; then
		debug_print "ap_sta_aid:$ap_sta_aid ap_usrinfo_ss_alloc_ra_ru:$ap_usrinfo_ss_alloc_ra_ru"

		local AID_SS_FILE="/tmp/sigma-aid-ss-conf"
		local AID_SS_FILE_SORTED="/tmp/sigma-aid-ss-conf-sort"
		[ -e $AID_SS_FILE ] && rm $AID_SS_FILE && rm $AID_SS_FILE_SORTED
		ap_sta_aid="${ap_sta_aid//:/,}"
		ap_sta_aid="${ap_sta_aid// /,}"
		ap_usrinfo_ss_alloc_ra_ru="${ap_usrinfo_ss_alloc_ra_ru//:/,}"
		ap_usrinfo_ss_alloc_ra_ru="${ap_usrinfo_ss_alloc_ra_ru// /,}"
		for ap_aid_index in $ap_sta_aid
		do
			local tmp_ap_sta_aid=${ap_sta_aid%%,*}
			local tmp_ap_usrinfo_ss_alloc_ra_ru=${ap_usrinfo_ss_alloc_ra_ru%%,*}

			echo "$tmp_ap_usrinfo_ss_alloc_ra_ru,$tmp_ap_sta_aid" >> $AID_SS_FILE
			ap_sta_aid="${ap_sta_aid/$tmp_ap_sta_aid,/""}"
			ap_usrinfo_ss_alloc_ra_ru="${ap_usrinfo_ss_alloc_ra_ru/$tmp_ap_usrinfo_ss_alloc_ra_ru,/""}"
		done

		sort -r $AID_SS_FILE > $AID_SS_FILE_SORTED
		local line param index sta_aid_val
		# update all users according to the AID_SS_FILE_SORTED

		index=0
		while read -r line || [[ -n "$line" ]]
		do
			# 3 params per line: ss_alloc_ra_ru,sta_aid,ru_alloc
			let index=index+1
			info_print "line=$line"
			ap_usrinfo_ss_alloc_ra_ru=${line%%,*}
			line="${line//$ap_usrinfo_ss_alloc_ra_ru,/""}"
			# update the DB with ss_alloc_ra_ru
			# get MU UL MCS value from FAPI
			ap_mcs=`get_plan_mcs_ul $ap_interface $index`
			# calculate the OFDMA MU NSS-MCS value (NSS: bits 6-4, MCS: bits 3-0)
			let ap_ofdma_mu_nss_mcs_val="($ap_usrinfo_ss_alloc_ra_ru)*16+$ap_mcs"
			# set MU UL NSS MCS value
			echo "set radio $ap_interface WaveSPDlUsrUlPsduRatePerUsp${index} ${ap_ofdma_mu_nss_mcs_val}" >> $CMD_FILE
			ap_sta_aid=${line%%,*}
			line="${line//$ap_sta_aid,/""}"
			# update the DB with AID
			[ "$ap_sta_aid" -gt "0" ] && let sta_aid_val=$ap_sta_aid-1
			echo "set radio $ap_interface WaveSPDlUsrUspStationIndexes${index} ${sta_aid_val}" >> $CMD_FILE
		done < $AID_SS_FILE_SORTED

		ap_check_plan_active=1
		ap_clish_commit=1
	fi

	if [ "$ap_numss" != "" ]; then
		debug_print "ap_numss_mac:$ap_numss_mac ap_numss:$ap_numss"
		local STA_LIST_FILE="/tmp/sigma-mac_sta_list"
		local NUMSS_FILE="/tmp/sigma-aid-ss-conf"
		local NUMSS_FILE_SORTED="/tmp/sigma-aid-ss-conf-sort"
		local MAC_AID_FILE="/tmp/sigma-mac_aid_file"
		rm -f $NUMSS_FILE $NUMSS_FILE_SORTED $MAC_AID_FILE $STA_LIST_FILE

		# Find the AID matching for each MAC
		# Create a file in the structure of MAC,AID
		cat /proc/net/mtlk/$ap_interface/Debug/sta_list | awk '{print $1 $2 $3}' > $STA_LIST_FILE
		num_of_sta=0
		mac=""
		aid=""
		while read line
		do
			if [ ! -z "$line" ] && [ $line != ${line//:/} ]
			then
				mac=${line%%|*}
				aid=${line##*|}
				echo "${mac},${aid}" >> $MAC_AID_FILE
				num_of_sta=$((num_of_sta+1))
				mac=""
				aid=""
			fi
		done < $STA_LIST_FILE
		# Convert each MAC to its corresponding AID
		ap_numss_mac=${ap_numss_mac// /,}
		sta_aid_list=""
		for ap_numss_index in $ap_numss_mac
		do
			ap_numss_index_tmp=${ap_numss_index%%,*}
			current_aid=`grep -i $ap_numss_index_tmp $MAC_AID_FILE`
			if [ -n "$current_aid" ]; then
				current_aid=${current_aid##*,}
				if [ ! -z "$sta_aid_list" ]
				then
					sta_aid_list="$sta_aid_list $current_aid"
				else
					sta_aid_list="$current_aid"
				fi
			fi
		done

		i=1
		local cur_ss=""
		local cur_aid=""
		end_of_list="no"
		while [ "$end_of_list" = "no" ]
		do
			cur_aid=`echo $sta_aid_list | cut -d ' ' -f$i`
			[ "$num_of_sta" = "1" ] && end_of_list="yes"
			if [ -z $cur_aid ]
			then
				end_of_list="yes"
				continue
			fi
			cur_ss=`echo $ap_numss | cut -d ' ' -f$i`

			if [ "$cur_ss" != "" ] && [ "$cur_aid" != "" ]; then
				echo "${cur_ss},${cur_aid}" >> $NUMSS_FILE
			fi

			cur_ss=""
			cur_aid=""
			i=$((i+1))
		done

		sort -r $NUMSS_FILE > $NUMSS_FILE_SORTED

		# update all users according to the NUMSS_FILE
		local line param index sta_aid_val
		index=0
		while read -r line || [[ -n "$line" ]]
		do
			# 2 params per line: ss,sta_aid
			let index=index+1
			info_print "line=$line"
			ap_numss=${line%%,*}
			line="${line//$ap_numss,/""}"
			# update the DB with ss
			# get MU DL MCS value from FAPI
			ap_mcs=`get_plan_mcs_dl $ap_interface $index`
			# calculate the OFDMA MU NSS-MCS value (NSS: bits 5-4, MCS: bits 3-0)
			let ap_ofdma_mu_nss_mcs_val="($ap_numss-1)*16+$ap_mcs"
			# set MU DL NSS MCS value
			echo "set radio $ap_interface WaveSPDlUsrPsduRatePerUsp${index} ${ap_ofdma_mu_nss_mcs_val}" >> $CMD_FILE
			ap_sta_aid=${line%%,*}
			line="${line//$ap_sta_aid,/""}"
			# update the DB with AID
			[ "$ap_sta_aid" -gt "0" ] && let sta_aid_val=$ap_sta_aid-1
			echo "set radio $ap_interface WaveSPDlUsrUspStationIndexes${index} ${sta_aid_val}" >> $CMD_FILE
		done < $NUMSS_FILE_SORTED

		ap_check_plan_active=1
		ap_clish_commit=1
	fi

	# handle RU allocation for UL
	if [ "$ap_usrinfo_ru_alloc" != "" ]; then
		debug_print "ap_usrinfo_ru_alloc:$ap_usrinfo_ru_alloc"
		# replace all ':' with " "
		ap_usrinfo_ru_alloc=${ap_usrinfo_ru_alloc//:/,}
		# ap_usrinfo_ru_alloc implicitly holds the number of users.

		local user_index user_list index user_value start_bw_limit
		local ul_sub_band1 ul_start_ru1 ul_ru_size1
		local ul_sub_band2 ul_start_ru2 ul_ru_size2
		local ul_sub_band3 ul_start_ru3 ul_ru_size3
		local ul_sub_band4 ul_start_ru4 ul_ru_size4

		if [ "$ap_cominfo_bw" != "" ]; then
			# if exist, get the bw from previous parameter in this command
			start_bw_limit=$ap_cominfo_bw
		else
			# else, get the bw from the SP
			start_bw_limit=`get_plan_bw $ap_interface`
		fi
		user_index=0
		for user_value in $ap_usrinfo_ru_alloc
		do
			let user_index=user_index+1

			### BW=160MHz ###
			if [ "$start_bw_limit" = "3" ]; then
				info_print "RU allocation for 160MHz UL: currently values are same as DL need to be updated"
				if [ "$user_value" = "106" ]; then
					case "$user_index" in
						"1") #USER1
							ul_sub_band1=0;ul_start_ru1=0;ul_ru_size1=2
						;;
						"2") #USER2
							ul_sub_band2=2;ul_start_ru2=0;ul_ru_size2=2
						;;
						"3") #USER3
							ul_sub_band3=4;ul_start_ru3=0;ul_ru_size3=2
						;;
						"4") #USER4
							ul_sub_band4=6;ul_start_ru4=0;ul_ru_size4=2
						;;
					esac
				elif [ "$user_value" = "242" ]; then
					case "$user_index" in
						"1") #USER1
							ul_sub_band1=0;ul_start_ru1=0;ul_ru_size1=3
						;;
						"2") #USER2
							ul_sub_band2=2;ul_start_ru2=0;ul_ru_size2=3
						;;
						"3") #USER3
							ul_sub_band3=4;ul_start_ru3=0;ul_ru_size3=3
						;;
						"4") #USER4
							ul_sub_band4=6;ul_start_ru4=0;ul_ru_size4=3
						;;
					esac
				elif [ "$user_value" = "484" ]; then
					case "$user_index" in
						"1") #USER1
							ul_sub_band1=0;ul_start_ru1=0;ul_ru_size1=4
						;;
						"2") #USER2
							ul_sub_band2=2;ul_start_ru2=0;ul_ru_size2=4
						;;
						"3") #USER3 - not supported
							ul_sub_band3=4;ul_start_ru3=0;ul_ru_size3=4
						;;
						"4") #USER4 - not supported
							ul_sub_band4=6;ul_start_ru4=0;ul_ru_size4=4
						;;
					esac
				elif [ "$user_value" = "996" ]; then
					case "$user_index" in
						"1") #USER1
							ul_sub_band1=0;ul_start_ru1=0;ul_ru_size1=5
						;;
						"2") #USER2
							ul_sub_band2=4;ul_start_ru2=0;ul_ru_size2=5
						;;
						"3") #USER3 - not supported
							error_print "cannot set user${user_index} with ru=${user_value}"
							send_invalid ",errorCode,655"
							return
						;;
						"4") #USER4 - not supported
							error_print "cannot set user${user_index} with ru=${user_value}"
							send_invalid ",errorCode,660"
							return
						;;
					esac
				else
					error_print "cannot set user${user_index} with ru=${user_value}"
					send_invalid ",errorCode,665"
					return
				fi

			### BW=80MHz ###
			elif [ "$start_bw_limit" = "2" ]; then
				if [ "$user_value" = "26" ]; then
					case "$user_index" in
						"1") #USER1
							ul_sub_band1=0;ul_start_ru1=0;ul_ru_size1=0
						;;
						"2") #USER2
							ul_sub_band2=1;ul_start_ru2=0;ul_ru_size2=0
						;;
						"3") #USER3
							ul_sub_band3=2;ul_start_ru3=0;ul_ru_size3=0
						;;
						"4") #USER4
							ul_sub_band4=3;ul_start_ru4=0;ul_ru_size4=0
						;;
					esac
				elif [ "$user_value" = "52" ]; then
					case "$user_index" in
						"1") #USER1
							ul_sub_band1=0;ul_start_ru1=0;ul_ru_size1=1
						;;
						"2") #USER2
							ul_sub_band2=1;ul_start_ru2=0;ul_ru_size2=1
						;;
						"3") #USER3
							ul_sub_band3=2;ul_start_ru3=0;ul_ru_size3=1
						;;
						"4") #USER4
							ul_sub_band4=3;ul_start_ru4=0;ul_ru_size4=1
						;;
					esac
				elif [ "$user_value" = "106" ]; then
					case "$user_index" in
						"1") #USER1
							ul_sub_band1=0;ul_start_ru1=0;ul_ru_size1=2
						;;
						"2") #USER2
							ul_sub_band2=1;ul_start_ru2=0;ul_ru_size2=2
						;;
						"3") #USER3
							ul_sub_band3=2;ul_start_ru3=0;ul_ru_size3=2
						;;
						"4") #USER4
							ul_sub_band4=3;ul_start_ru4=0;ul_ru_size4=2
						;;
					esac
				elif [ "$user_value" = "242" ]; then
					case "$user_index" in
						"1") #USER1
							ul_sub_band1=0;ul_start_ru1=0;ul_ru_size1=3
						;;
						"2") #USER2
							ul_sub_band2=1;ul_start_ru2=0;ul_ru_size2=3
						;;
						"3") #USER3
							ul_sub_band3=2;ul_start_ru3=0;ul_ru_size3=3
						;;
						"4") #USER4
							ul_sub_band4=3;ul_start_ru4=0;ul_ru_size4=3
						;;
					esac
				elif [ "$user_value" = "484" ]; then
					case "$user_index" in
						"1") #USER1
							ul_sub_band1=0;ul_start_ru1=0;ul_ru_size1=4
						;;
						"2") #USER2
							ul_sub_band2=2;ul_start_ru2=0;ul_ru_size2=4
						;;
						"3") #USER3 - not supported
							error_print "cannot set user${user_index} with ru=${user_value}"
							send_invalid ",errorCode,481"
							return
						;;
						"4") #USER4 - not supported
							error_print "cannot set user${user_index} with ru=${user_value}"
							send_invalid ",errorCode,482"
							return
						;;
					esac
				elif [ "$user_value" = "996" ]; then
					case "$user_index" in
						"1") #USER1
							ul_sub_band1=0;ul_start_ru1=0;ul_ru_size1=5
						;;
						"2") #USER2
							error_print "cannot set user${user_index} with ru=${user_value}"
							send_invalid ",errorCode,483"
							return
						;;
						"3") #USER3 - not supported
							error_print "cannot set user${user_index} with ru=${user_value}"
							send_invalid ",errorCode,484"
							return
						;;
						"4") #USER4 - not supported
							error_print "cannot set user${user_index} with ru=${user_value}"
							send_invalid ",errorCode,485"
							return
						;;
					esac
				else
					error_print "cannot set user${user_index} with ru=${user_value}"
					send_invalid ",errorCode,486"
					return
				fi

			### BW=40MHz ###
			elif [ "$start_bw_limit" = "1" ]; then
				if [ "$user_value" = "26" ]; then
					case "$user_index" in
						"1") #USER1
							ul_sub_band1=0;ul_start_ru1=0;ul_ru_size1=0
						;;
						"2") #USER2
							ul_sub_band2=0;ul_start_ru2=5;ul_ru_size2=0
						;;
						"3") #USER3
							ul_sub_band3=1;ul_start_ru3=0;ul_ru_size3=0
						;;
						"4") #USER4
							ul_sub_band4=1;ul_start_ru4=5;ul_ru_size4=0
						;;
					esac
				elif [ "$user_value" = "52" ]; then
					case "$user_index" in
						"1") #USER1
							ul_sub_band1=0;ul_start_ru1=0;ul_ru_size1=1
						;;
						"2") #USER2
							ul_sub_band2=0;ul_start_ru2=5;ul_ru_size2=1
						;;
						"3") #USER3
							ul_sub_band3=1;ul_start_ru3=0;ul_ru_size3=1
						;;
						"4") #USER4
							ul_sub_band4=1;ul_start_ru4=5;ul_ru_size4=1
						;;
					esac
				elif [ "$user_value" = "106" ]; then
					case "$user_index" in
						"1") #USER1
							ul_sub_band1=0;ul_start_ru1=0;ul_ru_size1=2
						;;
						"2") #USER2
							ul_sub_band2=0;ul_start_ru2=5;ul_ru_size2=2
						;;
						"3") #USER3
							ul_sub_band3=1;ul_start_ru3=0;ul_ru_size3=2
						;;
						"4") #USER4
							ul_sub_band4=1;ul_start_ru4=5;ul_ru_size4=2
						;;
					esac
				elif [ "$user_value" = "242" ]; then
					case "$user_index" in
						"1") #USER1
							ul_sub_band1=0;ul_start_ru1=0;ul_ru_size1=3
						;;
						"2") #USER2
							ul_sub_band2=1;ul_start_ru2=0;ul_ru_size2=3
						;;
						"3") #USER3 - not supported
							error_print "cannot set user${user_index} with ru=${user_value}"
							send_invalid ",errorCode,487"
							return
						;;
						"4") #USER4 - not supported
							error_print "cannot set user${user_index} with ru=${user_value}"
							send_invalid ",errorCode,488"
							return
						;;
					esac
				else
					error_print "cannot set user${user_index} with ru=${user_value}"
					send_invalid ",errorCode,489"
					return
				fi

			### BW=20MHz ###
			elif [ "$start_bw_limit" = "0" ]; then
				if [ "$user_value" = "26" ]; then
					case "$user_index" in
						"1") #USER1
							ul_sub_band1=0;ul_start_ru1=0;ul_ru_size1=0
						;;
						"2") #USER2
							ul_sub_band2=0;ul_start_ru2=2;ul_ru_size2=0
						;;
						"3") #USER3
							ul_sub_band3=0;ul_start_ru3=5;ul_ru_size3=0
						;;
						"4") #USER4
							ul_sub_band4=0;ul_start_ru4=7;ul_ru_size4=0
						;;
					esac
				elif [ "$user_value" = "52" ]; then
					case "$user_index" in
						"1") #USER1
							ul_sub_band1=0;ul_start_ru1=0;ul_ru_size1=1
						;;
						"2") #USER2
							ul_sub_band2=0;ul_start_ru2=2;ul_ru_size2=1
						;;
						"3") #USER3
							ul_sub_band3=0;ul_start_ru3=5;ul_ru_size3=1
						;;
						"4") #USER4
							ul_sub_band4=0;ul_start_ru4=7;ul_ru_size4=1
						;;
					esac
				elif [ "$user_value" = "106" ]; then
					case "$user_index" in
						"1") #USER1
							ul_sub_band1=0;ul_start_ru1=0;ul_ru_size1=2
						;;
						"2") #USER2
							ul_sub_band2=0;ul_start_ru2=5;ul_ru_size2=2
						;;
						"3") #USER3
							error_print "cannot set user${user_index} with ru=${user_value}"
							send_invalid ",errorCode,490"
							return
						;;
						"4") #USER4
							error_print "cannot set user${user_index} with ru=${user_value}"
							send_invalid ",errorCode,491"
							return
						;;
					esac
				elif [ "$user_value" = "242" ]; then
					case "$user_index" in
						"1") #USER1
							ul_sub_band1=0;ul_start_ru1=0;ul_ru_size1=3
						;;
						"2") #USER2
							error_print "cannot set user${user_index} with ru=${user_value}"
							send_invalid ",errorCode,492"
							return
						;;
						"3") #USER3
							error_print "cannot set user${user_index} with ru=${user_value}"
							send_invalid ",errorCode,493"
							return
						;;
						"4") #USER4
							error_print "cannot set user${user_index} with ru=${user_value}"
							send_invalid ",errorCode,494"
							return
						;;
					esac
				elif [ "$user_value" = "484" ]; then
					error_print "cannot set user${user_index} with ru=${user_value}"
					send_invalid ",errorCode,495"
					return
				else
					error_print "cannot set user${user_index} with ru=${user_value}"
					send_invalid ",errorCode,496"
					return
				fi
			else
				error_print "Unsupported value - start_bw_limit:$start_bw_limit"
				send_invalid ",errorCode,497"
				return
			fi
		done

		# user_index contains the number of users. set it to DB to be used by static plan.
		echo "set radio $ap_interface WaveSPDlComNumOfParticipatingStations ${user_index}" >> $CMD_FILE

		# update per-user params in DB, per number of users
		#for index in $user_index
		user_list="1,2"
		[ "$user_index" = "4" ] && user_list="1,2,3,4"
		for index in $user_list
		do
			local tmp_param tmp_val
			tmp_param="ul_sub_band${index}";eval tmp_val=\$$tmp_param
			echo "set radio $ap_interface WaveSPRcrTfUsrSubBand${index} ${tmp_val}" >> $CMD_FILE
			tmp_param="ul_start_ru${index}";eval tmp_val=\$$tmp_param
			echo "set radio $ap_interface WaveSPRcrTfUsrStartRu${index} ${tmp_val}" >> $CMD_FILE
			tmp_param="ul_ru_size${index}";eval tmp_val=\$$tmp_param
			echo "set radio $ap_interface WaveSPRcrTfUsrRuSize${index} ${tmp_val}" >> $CMD_FILE
		done

		# AIDs are given as input and were set according to NSS (higher to lower).
		ap_check_plan_active=1
		ap_clish_commit=1
	fi

	# Set trigger coding (LDPC/BCC) for UL
	if [ "$ap_trigger_coding" != "" ]; then
		debug_print "ap_trigger_coding:$ap_trigger_coding"

		local LDPC_INT=1
		local BCC_INT=0

		eval ap_bcc_ldpc_int=\${${ap_trigger_coding}_INT}

		for usr_index in 1 2 3 4
		do
			echo "set radio $ap_interface WaveSPRcrTfUsrLdpc${usr_index} $ap_bcc_ldpc_int " >> $CMD_FILE
		done

		ap_check_plan_active=1
		ap_clish_commit=1
	fi

	# test 5.71.1 - set TXOP Duration RTS Threshold in HE Operation
	[ "$ap_he_txop_dur_rts_thr" != "" ] && hostapd_cli -i $ap_interface set he_operation_txop_duration_rts_threshold $ap_he_txop_dur_rts_thr > /dev/null && ap_hostapd_perform_reload=1

	[ "$ap_sta_muedca_ecwmin_be" != "" ] && hostapd_cli -i $ap_interface set he_mu_edca_ac_be_ecwmin $ap_sta_muedca_ecwmin_be > /dev/null && ap_hostapd_perform_edca_reload=1
	[ "$ap_sta_muedca_ecwmin_vi" != "" ] && hostapd_cli -i $ap_interface set he_mu_edca_ac_vi_ecwmin $ap_sta_muedca_ecwmin_vi > /dev/null && ap_hostapd_perform_edca_reload=1
	[ "$ap_sta_muedca_ecwmin_vo" != "" ] && hostapd_cli -i $ap_interface set he_mu_edca_ac_vo_ecwmin $ap_sta_muedca_ecwmin_vo > /dev/null && ap_hostapd_perform_edca_reload=1
	[ "$ap_sta_muedca_ecwmin_bk" != "" ] && hostapd_cli -i $ap_interface set he_mu_edca_ac_bk_ecwmin $ap_sta_muedca_ecwmin_bk > /dev/null && ap_hostapd_perform_edca_reload=1

	[ "$ap_sta_muedca_ecwmax_be" != "" ] && hostapd_cli -i $ap_interface set he_mu_edca_ac_be_ecwmax $ap_sta_muedca_ecwmax_be > /dev/null && ap_hostapd_perform_edca_reload=1
	[ "$ap_sta_muedca_ecwmax_vi" != "" ] && hostapd_cli -i $ap_interface set he_mu_edca_ac_vi_ecwmax $ap_sta_muedca_ecwmax_vi > /dev/null && ap_hostapd_perform_edca_reload=1
	[ "$ap_sta_muedca_ecwmax_vo" != "" ] && hostapd_cli -i $ap_interface set he_mu_edca_ac_vo_ecwmax $ap_sta_muedca_ecwmax_vo > /dev/null && ap_hostapd_perform_edca_reload=1
	[ "$ap_sta_muedca_ecwmax_bk" != "" ] && hostapd_cli -i $ap_interface set he_mu_edca_ac_bk_ecwmax $ap_sta_muedca_ecwmax_bk > /dev/null && ap_hostapd_perform_edca_reload=1

	[ "$ap_sta_muedca_aifsn_be" != "" ] && hostapd_cli -i $ap_interface set he_mu_edca_ac_be_aifsn $ap_sta_muedca_aifsn_be > /dev/null && ap_hostapd_perform_edca_reload=1
	[ "$ap_sta_muedca_aifsn_vi" != "" ] && hostapd_cli -i $ap_interface set he_mu_edca_ac_vi_aifsn $ap_sta_muedca_aifsn_vi > /dev/null && ap_hostapd_perform_edca_reload=1
	[ "$ap_sta_muedca_aifsn_vo" != "" ] && hostapd_cli -i $ap_interface set he_mu_edca_ac_vo_aifsn $ap_sta_muedca_aifsn_vo > /dev/null && ap_hostapd_perform_edca_reload=1
	[ "$ap_sta_muedca_aifsn_bk" != "" ] && hostapd_cli -i $ap_interface set he_mu_edca_ac_bk_aifsn $ap_sta_muedca_aifsn_bk > /dev/null && ap_hostapd_perform_edca_reload=1

	[ "$ap_sta_muedca_timer_be" != "" ] && hostapd_cli -i $ap_interface set he_mu_edca_ac_be_timer $ap_sta_muedca_timer_be > /dev/null && ap_hostapd_perform_edca_reload=1
	[ "$ap_sta_muedca_timer_vi" != "" ] && hostapd_cli -i $ap_interface set he_mu_edca_ac_vi_timer $ap_sta_muedca_timer_vi > /dev/null && ap_hostapd_perform_edca_reload=1
	[ "$ap_sta_muedca_timer_vo" != "" ] && hostapd_cli -i $ap_interface set he_mu_edca_ac_vo_timer $ap_sta_muedca_timer_vo > /dev/null && ap_hostapd_perform_edca_reload=1
	[ "$ap_sta_muedca_timer_bk" != "" ] && hostapd_cli -i $ap_interface set he_mu_edca_ac_bk_timer $ap_sta_muedca_timer_bk > /dev/null && ap_hostapd_perform_edca_reload=1

	# test 5.64.1
	[ "$ap_sta_wmmpe_ecwmin_vi" != "" ] && hostapd_cli -i $ap_interface set wmm_ac_vi_cwmin $ap_sta_wmmpe_ecwmin_vi > /dev/null && ap_hostapd_perform_edca_reload=1
	[ "$ap_sta_wmmpe_ecwmax_vi" != "" ] && hostapd_cli -i $ap_interface set wmm_ac_vi_cwmax $ap_sta_wmmpe_ecwmax_vi > /dev/null && ap_hostapd_perform_edca_reload=1

	# Tests 4.52 & 5.56.1
	if [ "$ap_omctrl_rxnss" != "" ] && [ "$ap_omctrl_chnlwidth" != "" ] && [ "$ap_client_mac" != "" ]
	then
		sta_id=`cat /proc/net/mtlk/$ap_interface/Debug/sta_list | grep -i $ap_client_mac`
		sta_id=`echo $sta_id | cut -d'|' -f 2`
		if [ "$sta_id" != "" ] && [ $sta_id -gt 0 ]
		then
			sta_id=$((sta_id-1))
			debug_print "ap_omctrl_rxnss:$ap_omctrl_rxnss ap_omctrl_chnlwidth:$ap_omctrl_chnlwidth sta_id:$sta_id"
			iw $ap_interface iwlwav sDoSimpleCLI 15 $sta_id $ap_omctrl_rxnss $ap_omctrl_chnlwidth
		fi
	fi

	# WLANRTSYS-11513 TC 5.61.1
	if [ "$ap_ppdutxtype" != "" ]
	then
		if [ "$ap_ppdutxtype" = "HE-SU" ]; then
			echo "set radio $ap_interface WaveSPDlComMaximumPpduTransmissionTimeLimit 0" >> $CMD_FILE
		elif [ "$ap_ppdutxtype" = "legacy" ]; then
			echo "set radio $ap_interface WaveSPDlComMaximumPpduTransmissionTimeLimit 2700" >> $CMD_FILE
		else
			error_print "!!! PPDUTXTYPE wrong value !!!"
		fi
			ap_check_plan_active=1
			ap_clish_commit=1
	fi


	# for test HE-4.43.1
	if [ "$ap_disable_trigger_type" != "" ] && [ "$ap_disable_trigger_type" = "0" ]
	then
		# turn static plan off
		clish -c "configure wlan" -c "set radio $ap_interface WaveSPEnable 0" > /dev/console
		ap_check_plan_active=0
	fi

	# for test HE-5.64.1 & 5.61.1
	if [ "$ap_trig_interval" != "" ]; then
		ap_trig_interval_orig=$ap_trig_interval
		# check if ap_trig_interval divides by 10
		if [ $((ap_trig_interval % 10 )) -eq 0 ]; then
			ap_trig_interval=$((ap_trig_interval / 10 ))
			# param3: interval value [10ms] ;param 4: fixed interval (0=false)
			iw $ap_interface iwlwav sDoSimpleCLI 3 13 $ap_trig_interval 1
		else
			error_print "!!! Unsupported value TRIG_INTERVAL: ap_trig_interval:$ap_trig_interval_orig !!!"
			error_print "!!! TRIG_INTERVAL setting was not done !!!"
		fi

		if [ $ap_trig_interval != "1" ]; then
			echo "set radio $ap_interface WaveSPDlComNumberOfPhaseRepetitions 0" >> $CMD_FILE
		fi

		ap_clish_commit=1
	fi

	# for test HE-5.64.1
	if [ "$ap_trig_ullength" != "" ]
	then
		echo "set radio $ap_interface WaveSPRcrComTfLength ${ap_trig_ullength}" >> $CMD_FILE
		ap_clish_commit=1
	fi

	# for test HE-5.61.1
	if [ "$ap_mpdu_mu_spacingfactor" != "" ]
	then
		for usr_index in 1 2 3 4
		do
			echo "set radio $ap_interface WaveSPTfUsrTfMpduMuSpacingFactor${usr_index} $ap_mpdu_mu_spacingfactor " >> $CMD_FILE
		done
		ap_clish_commit=1
	fi

	if [ "$ap_trig_cominfo_ullength" != "" ]
	then
		# for test HE-5.61.1
		if [ "$ap_trig_cominfo_ullength" = "601" ]; then
			echo "set radio $ap_interface WaveSPRcrComTfLength 610 " >> $CMD_FILE
		# for test HE-5.64.1
		elif [ "$ap_trig_cominfo_ullength" = "2251" ]; then
			echo "set radio $ap_interface WaveSPRcrComTfLength 2254 " >> $CMD_FILE
		else
			# regular case, not '601' nor '2251'
			echo "set radio $ap_interface WaveSPRcrComTfLength $ap_trig_cominfo_ullength " >> $CMD_FILE
		fi

		ap_clish_commit=1
	fi

	# for tests HE-5.59.2 & HE-5.61.1
	if [ "$ap_trig_usrinfo_ul_mcs" != "" ]
	then
		for usr_index in 1 2 3 4
		do
			local ap_trig_ul_nss ap_trig_nss_mcs_val

			# bits 0-3: ap_trig_usrinfo_ul_mcs
			# bits 4-6: ap_trig_ul_nss

			ap_trig_ul_nss=`get_plan_nss_ul $ap_interface $usr_index`
			ap_trig_ul_nss=$((ap_trig_ul_nss-1))
			ap_trig_nss_mcs_val=$(((ap_trig_ul_nss*16)+ap_trig_usrinfo_ul_mcs))
			echo "set radio $ap_interface WaveSPDlUsrUlPsduRatePerUsp${usr_index} $ap_trig_nss_mcs_val " >> $CMD_FILE
		done
		ap_clish_commit=1
	fi

	# for test HE-5.59.2
	if [ "$ap_trig_usrinfo_ul_target_rssi" != "" ]
	then
		for usr_index in 1 2 3 4
		do
			echo "set radio $ap_interface WaveSPRcrTfUsrTargetRssi${usr_index} $ap_trig_usrinfo_ul_target_rssi " >> $CMD_FILE
		done
		ap_clish_commit=1
	fi

	# check if we need to turn the static plan off and then on
	if [ "$ap_check_plan_active" = "1" ]; then
		# if plan is active, disable it and set for re-enable with the new parameters.
#		ap_spenable=`get_plan_enable $ap_interface`
#		if 	[ "$ap_spenable" = "1" ]; then
			# first, turn off the static plan (immediate action)
			clish -c "configure wlan" -c "set radio $ap_interface WaveSPEnable 0" > /dev/console
			# second, turn on the static plan with the new parameter (delayed action).
			echo "set radio $ap_interface WaveSPEnable 1" >> $CMD_FILE
#		fi
		# Sigma Manager Daemon (SMD) will be triggered only if it is active.
	fi

	# JIRA WLANRTSYS-9307: in case the SMD needed to be activated, make sure the plan won't be set
	if [ "$is_activate_sigmaManagerDaemon" = "1" ]; then
		radio_params=$radio_params" WaveSPEnable 0"
	fi

	# finalize and commit clish command file for all previous commands
	if [ "$ap_clish_commit" = "1" ]; then
		echo "commit" >> $CMD_FILE
		echo "exit" >> $CMD_FILE
		clish $CMD_FILE > /dev/console
	fi
	rm -f $CMD_FILE

	if [ "$ap_hostapd_perform_reload" = "1" ]; then
		hostapd_cli -i $ap_interface reload > /dev/null
	fi

	if [ "$ap_hostapd_perform_edca_reload" = "1" ]; then
		hostapd_cli -i $ap_interface increment_mu_edca_counter_and_reload > /dev/null
	fi

	# JIRA WLANRTSYS-9307: in case "TRIGGERTYPE" was set, activate the SMD
	if [ "$is_activate_sigmaManagerDaemon" = "1" ]; then
		[ -z "$(pgrep -f sigmaManagerDaemon.sh)" ] && /opt/lantiq/wave/scripts/sigmaManagerDaemon.sh &
		is_activate_sigmaManagerDaemon=0
	fi

	send_complete
}

dev_send_frame()
{
	ap_name="INTEL"
	# default to wlan0 in case no interface parameter is given
	ap_interface=""

	IFS=" "
	send_running
	# checking for interface parameter (might fail if some value is also "interface")
	get_interface_name $@

	#get_wlan_param_radio_all "ap_nss_mcs_opt"
	#debug_print "ifn ap_nss_mcs_opt:$ap_nss_mcs_opt"

	while [ "$1" != "" ]; do
		# for upper case only
		upper "$1" token
		debug_print "while loop $1 - token:$token"
		case "$token" in
			NAME)
				shift
				ap_name=$1
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
				ap_dest_mac=$1
			;;
			PROGRAM)
				# dev_send_frame do nothing
				shift
			;;
			FRAMENAME)
				shift
				ap_frame_name=$1
			;;
			CAND_LIST)
				shift
				ap_cand_list=$1
			;;
			BTMQUERY_REASON_CODE)
				shift
				ap_btmquery_reason_code=$1
			;;
			DISASSOC_TIMER)
				shift
				ap_disassoc_timer=$1
			;;
			MEAMODE)
				shift
				ap_meamode=$1
			;;
			REGCLASS)
				shift
				ap_regclass=$1
			;;
			CHANNEL)
				shift
				ap_channel=$1
			;;
			RANDINT)
				shift
				ap_randint=$1
			;;
			MEADUR)
				shift
				ap_meadur=$1
			;;
			SSID)
				shift
				ap_ssid=$1
			;;
			RPTCOND)
				shift
				ap_rptcond=$1
			;;
			RPTDET)
				shift
				ap_rpt_det=$1
			;;
			MEADURMAND)
				shift
				ap_meadurmand=$1
			;;
			APCHANRPT)
				shift
				ap_apchanrpt=$1
			;;
			REQINFO)
				shift
				ap_reqinfo=$1
			;;
			REQUEST_MODE)
				# dev_send_frame do nothing
				shift
			;;
			LASTBEACONRPTINDICATION)
				shift
				ap_lastbeaconrptindication=$1
			;;
			BSSID)
				shift
				ap_bssid=$1
			;;
			*)
				error_print "while loop error $1"
				send_invalid ",errorCode,500"
				return
			;;
		esac
		shift
	done

	# new clish command
	ap_btm_req_params=""
	ap_beacon_req_get_params=""
	ap_mac_addr=""
	ap_disassoc_sta_params=""
	if [ "$ap_dest_mac" != "" ]; then
		ap_mac_addr=$ap_dest_mac
	fi

	if [ "$ap_frame_name" != "" ]; then
		if [ "$ap_frame_name" = "BTMReq" ]; then
			# first parameter preference is 1 for the time being
			ap_btm_req_params=$ap_btm_req_params" pref=1"
			if [ "$ap_btmquery_reason_code" != "" ]; then
				ap_btm_req_params=$ap_btm_req_params" reason=$ap_btmquery_reason_code"
			else
				ap_btm_req_params=$ap_btm_req_params" reason=0" # mandatory parameter set to default value 0
			fi
			if [ "$ap_disassoc_timer" != "" ]; then
				ap_btm_req_params=$ap_btm_req_params" disassocTimer=$ap_disassoc_timer"
#			else
#				ap_btm_req_params=$ap_btm_req_params" disassocTimer=NULL"
			fi
		elif [ "$ap_frame_name" = "BcnRptReq" ]; then
			# first parameter numOfRepetitions is NULL for the time being
			ap_beacon_req_get_params=$ap_beacon_req_get_params" numOfRepetitions=0"

			if [ "$ap_meadurmand" != "" ]; then
				ap_beacon_req_get_params=$ap_beacon_req_get_params" durationMandatory=$ap_meadurmand"
			else
				ap_beacon_req_get_params=$ap_beacon_req_get_params" durationMandatory=0"
			fi

			if [ "$ap_regclass" != "" ]; then
				ap_beacon_req_get_params=$ap_beacon_req_get_params" opClass=$ap_regclass"
			else
				# opClass=115 for 5g and opClass=81 for 2.4G
				# TBD check on band
				ap_beacon_req_get_params=$ap_beacon_req_get_params" opClass=115"
			fi

			if [ "$ap_channel" != "" ]; then
				ap_beacon_req_get_params=$ap_beacon_req_get_params" Channel=$ap_channel"
			else
				ap_beacon_req_get_params=$ap_beacon_req_get_params" Channel=255"
			fi

			if [ "$ap_randint" != "" ]; then
				ap_beacon_req_get_params=$ap_beacon_req_get_params" randInt=$ap_randint"
			else
				ap_beacon_req_get_params=$ap_beacon_req_get_params" randInt=0"
			fi

			if [ "$ap_meadur" != "" ]; then
				ap_beacon_req_get_params=$ap_beacon_req_get_params" duration=$ap_meadur"
			else
				ap_beacon_req_get_params=$ap_beacon_req_get_params" duration=20"
			fi

			if [ "$ap_meamode" != "" ]; then
				ap_beacon_req_get_params=$ap_beacon_req_get_params" mode=$ap_meamode"
			else
				ap_beacon_req_get_params=$ap_beacon_req_get_params" mode=active"
			fi

			if [ "$ap_bssid" != "" ]; then
				ap_beacon_req_get_params=$ap_beacon_req_get_params" bssid=$ap_bssid"
			else
				ap_beacon_req_get_params=$ap_beacon_req_get_params" bssid=ff:ff:ff:ff:ff:ff"
			fi

			# the remaining parameters are optional
			if [ "$ap_ssid" != "" ]; then
				ap_beacon_req_get_params=$ap_beacon_req_get_params" 'ssid=\"$ap_ssid\"'"
			fi

			if [ "$ap_rpt_det" != "" ]; then
				ap_beacon_req_get_params=$ap_beacon_req_get_params" rep_detail=$ap_rpt_det"
			fi

			if [ "$ap_rptcond" != "" ]; then
				ap_beacon_req_get_params=$ap_beacon_req_get_params" rep_cond=$ap_rptcond"
			fi

			if [ "$ap_lastbeaconrptindication" != "" ]; then
				ap_beacon_req_get_params=$ap_beacon_req_get_params"last_indication=$ap_lastbeaconrptindication "
			fi

			if [ "$ap_apchanrpt" != "" ]; then
				# replace all "_" with "," in received string
				ap_apchanrpt_param="${ap_apchanrpt//_/,}"
				ap_beacon_req_get_params=$ap_beacon_req_get_params" ap_ch_report=$ap_apchanrpt_param"
			fi

			if [ "$ap_reqinfo" != "" ]; then
				# replace all "_" with "," in received string
				ap_reqinfo_param="${ap_reqinfo//_/,}"
				ap_beacon_req_get_params=$ap_beacon_req_get_params" req_elements=$ap_reqinfo_param"
			fi
		elif [ "$ap_frame_name" = "disassoc" ]; then
			ap_disassoc_sta_params="disassoc"
		else
			error_print "Unsupported value - ap_frame_name:$ap_frame_name"
			send_error ",errorCode,510"
			return
		fi
	fi

	if [ "$ap_btm_req_params" != "" ]; then
		debug_print "ap_btm_req_params:$ap_btm_req_params"
		debug_print "/usr/sbin/fapi_wlan_debug_cli COMMAND \"AML_BTM_REQ $ap_interface $ap_mac_addr $ap_btm_req_params\""

		# this should run in background in order to allow bringing down interface after BTM Req (w/a)
		/usr/sbin/fapi_wlan_debug_cli COMMAND "AML_BTM_REQ $ap_interface $ap_mac_addr $ap_btm_req_params" & >> $CONF_DIR/BeeRock_CMD.log

		if [ -n "$g_bssTermTSF" ] && [ -n "$g_bssTermDuration" ]; then
			# BSS Termination request - stop beaconing between TSF and duration
			# Convert duration from minutes to seconds, plus buffer
			g_bssTermDuration=$((g_bssTermDuration*60+3))
			cat > /tmp/bss_term.sh << EOF
sleep  $g_bssTermTSF
echo WLAN interfaces down > /dev/console
hostapd_cli -iwlan2 disassociate wlan2 ff:ff:ff:ff:ff:ff; ifconfig wlan2 down
hostapd_cli -iwlan0 disassociate wlan0 ff:ff:ff:ff:ff:ff; ifconfig wlan0 down
sleep $g_bssTermDuration
echo WLAN interfaces up > /dev/console
ifconfig wlan2 up; ifconfig wlan0 up
EOF
			chmod +x /tmp/bss_term.sh
			/tmp/bss_term.sh &
			g_bssTermTSF=""
			g_bssTermDuration=""
		fi
	elif [ "$ap_beacon_req_get_params" != "" ]; then
		debug_print "ap_beacon_req_get_params:$ap_beacon_req_get_params"
		debug_print "/usr/sbin/fapi_wlan_debug_cli COMMAND \"AML_BEACON_REQ_GET $ap_interface $ap_mac_addr $ap_beacon_req_get_params\""
		/usr/sbin/fapi_wlan_debug_cli COMMAND "AML_BEACON_REQ_GET $ap_interface $ap_mac_addr $ap_beacon_req_get_params" >> $CONF_DIR/BeeRock_CMD.log
	 elif [ "$ap_disassoc_sta_params" != "" ]; then
		debug_print "ap_disassoc_sta_params:$ap_disassoc_sta_params"
		debug_print "/usr/sbin/fapi_wlan_debug_cli COMMAND \"AML_DISASSOC_SET $ap_interface $ap_mac_addr\""
		/usr/sbin/fapi_wlan_debug_cli COMMAND "AML_DISASSOC_SET $ap_interface $ap_mac_addr" >> $CONF_DIR/BeeRock_CMD.log
	fi

	#sleep 3
	#killall fapi_wlan_debug_cli 1>&2
	#/usr/sbin/fapi_wlan_debug_cli AML_START 1>&2 &

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
	# ap_config_commit,NAME,name_of_AP
	# NAME parameter is ignored here

	send_running
	get_radio_interface_name $@

	# Close any existing configuration and bring up the new configuration
	clish_config_commit

	# perform mbssid commands (if needed)
	perform_mbssid_commands $ap_radio_interface

	if [ -s "$CMD_WPA3_SECURITY_FILE" ]; then
		echo "commit" >> $CMD_WPA3_SECURITY_FILE
		echo "exit" >> $CMD_WPA3_SECURITY_FILE
		clish $CMD_WPA3_SECURITY_FILE > /dev/console
		rm -f $CMD_WPA3_SECURITY_FILE
	fi

	# change station's power (if needed)
	perform_power_change $ap_radio_interface

	WLAN_RUNNING=1

	send_complete
}

# check one parameter in DB and reset it (only if needed). return value: none (concatenate the value thr. global variables)
ap_check_and_reset_one_param()
{
	#intf_index=$1
	#param_type=$2
	param_name=$3
	#param_default_value=$4

	local TMP1
	local TMP2

	# select the corresponding DB info (radio, security, ap)
	if [ "$2" = "radio" ]; then
		conf_file="fapi_wlan_wave_radio_conf"
		conf_param="radio_params"
		[ "$param_name" = "radioEnable" ] && param_name="Enable" # special case: radioEnable
		[ "$param_name" = "ConfigState" ] && conf_file="fapi_wlan_wave_wps_conf"
	elif [ "$2" = "security" ]; then
		conf_file="fapi_wlan_wave_security_conf"
		conf_param="sec_params"
	elif [ "$2" = "ap" ]; then
		conf_file="fapi_wlan_wave_access_point_conf"
		conf_param="ap_params"
	fi

	# get the value from DB
	TMP1=`cat $CONF_DIR/$conf_file | sed -n 's/^'"$param_name"'_'"$1"'=//p'`
	if [ "$TMP1" == "" ]; then
		error_print "Empty value - cannot find $param_name in DB"
		return
	else
		eval TMP2=`printf $TMP1`
		TMP2=${TMP2//' '/,} # replace delimiters (spaces to commas)
	fi

	# if param_default_value is NOT true/false, check if need to ignore true/false inside the DB
	if [ "$4" != "true" ] && [ "$4" != "false" ]; then
		if [ "$TMP2" = "true" ]; then
			# ignore "true". convert it to "1".
			TMP2="1"
		elif [ "$TMP2" = "false" ]; then
			# ignore "false". convert it to "0".
			TMP2="0"
		fi
	fi

	# check if value from DB equals the required default value
	if [ "$TMP2" != "$4" ]; then
		info_print "current:$3:$TMP2"
		ap_do_reset_defaults="1"

		[ "$conf_param" = "radio_params" ] && echo "set radio wlan$1 $3 $4" >> $CMD_FILE && conf_param=""
		[ "$conf_param" = "sec_params" ] && echo "set ap security wlan$1 $3 $4" >> $CMD_FILE && conf_param=""
		[ "$conf_param" = "ap_params" ] && echo "set ap wlan$1 $3 $4" >> $CMD_FILE && conf_param=""
	fi
}

## generic reset default for certification
ap_default_check_and_reset()
{
	local tmp_bw tmp_mode_val modessupported_val

	debug_print "ap_default_check_and_reset $1"

	ap_do_reset_defaults="0"
	radio_params=""
	sec_params=""
	ap_params=""
	modessupported_val="None,WEP-64,WEP-128,WPA2-Personal,WPA-WPA2-Personal,WPA2-Enterprise,WPA-WPA2-Enterprise"

	[ "$1" = "0" ] && tmp_bw="80MHz" && tmp_mode_val="b,g,n"
	[ "$1" = "2" ] && tmp_bw="80MHz" && tmp_mode_val="a,n,ac"
	# check if wlan0 is 5GHz (possible only in single interface support)
	if [ "$SINGLE_INTF_SUPPORT" = "1" ]; then
		tmp_channel=`cat /proc/net/mtlk/wlan$1/channel | grep primary_channel | awk '{print $2}'`
		[ $tmp_channel -ge 36 ] && tmp_bw="80MHz" && tmp_mode_val="a,n,ac"
	fi

	local CMD_FILE="/tmp/sigma-cmds"
	echo "configure wlan" > $CMD_FILE
	echo "start" >> $CMD_FILE

	##############################################
	# fapi_wlan_wave_radio default setting
	##############################################
	#func						intf_idx	param_type		param_name						param_default_value
	ap_check_and_reset_one_param	$1		radio			radioEnable							1
	ap_check_and_reset_one_param	$1		radio			OperatingChannelBandwidth			"$tmp_bw"
	ap_check_and_reset_one_param	$1		radio			WaveFRBandwidth						"$tmp_bw"
	ap_check_and_reset_one_param	$1		radio			OperatingStandards					"$tmp_mode_val"
	ap_check_and_reset_one_param	$1		radio			CoexRssiThreshold					-99
	ap_check_and_reset_one_param	$1		radio			WaveQamPlus							0
	ap_check_and_reset_one_param	$1		radio			WaveTxOpMode						Disabled
	ap_check_and_reset_one_param	$1		radio			WaveBfMode							Explicit
	ap_check_and_reset_one_param	$1		radio			ConfigState							Unconfigured
	ap_check_and_reset_one_param	$1		radio			WaveMuOperation						1


	##############################################
	# fapi_wlan_wave_security default setting
	##############################################
	#func						intf_idx	param_type		param_name						param_default_value
	ap_check_and_reset_one_param	$1		security		ModesSupported						"$modessupported_val"
	ap_check_and_reset_one_param	$1		security		ManagementFrameProtection			1
	ap_check_and_reset_one_param	$1		security		ManagementFrameProtectionRequired	0

	##############################################
	# fapi_wlan_wave_access_point default setting
	##############################################
	#func						intf_idx	param_type		param_name		param_default_value
	ap_check_and_reset_one_param	$1		ap				WaveMBOEnabled						0

	if [ "$ap_do_reset_defaults" = "1" ]; then
		ap_interface="wlan${1}"
		ap_radio_interface="wlan${1}"
		ap_ssid="test_ssid_wlan${1}"

		[ "$1" = "0" ] && echo "set ssid wlan$1 SSID CERT-test0" >> $CMD_FILE
		[ "$1" = "2" ] && echo "set ssid wlan$1 SSID CERT-test2" >> $CMD_FILE
		echo "commit" >> $CMD_FILE
		echo "exit" >> $CMD_FILE
		clish $CMD_FILE > /dev/console
		rm -f $CMD_FILE
	fi
}

ap_mbo_check_and_reset()
{
	local tmp_bw tmp_mode_val

	debug_print "ap_mbo_check_and_reset $1"

	ap_do_reset_defaults="0"
	radio_params=""
	sec_params=""
	ap_params=""

	local CMD_FILE="/tmp/sigma-cmds"
	echo "configure wlan" > $CMD_FILE
	echo "start" >> $CMD_FILE

	[ "$1" = "0" ] && tmp_bw="20MHz"
	[ "$1" = "2" ] && tmp_bw="80MHz"

	##############################################
	# fapi_wlan_wave_radio default setting
	##############################################
	#func						intf_idx	param_type		param_name		param_default_value
	ap_check_and_reset_one_param	$1		radio			radioEnable							1
	ap_check_and_reset_one_param	$1		radio			IEEE80211hEnabled					0
	ap_check_and_reset_one_param	$1		radio			OperatingChannelBandwidth			"$tmp_bw"
	ap_check_and_reset_one_param	$1		radio			WaveFRBandwidth						"$tmp_bw"


	##############################################
	# fapi_wlan_wave_security default setting
	##############################################
	#func						intf_idx	param_type		param_name		param_default_value
	ap_check_and_reset_one_param	$1		security		FastTransionSupport					0
	ap_check_and_reset_one_param	$1		security		ManagementFrameProtection			0
	ap_check_and_reset_one_param	$1		security		ManagementFrameProtectionRequired	0

	##############################################
	# fapi_wlan_wave_access_point default setting
	##############################################
	#func						intf_idx	param_type		param_name		param_default_value
	ap_check_and_reset_one_param	$1		ap				GasComebackDelay					0

	if [ "$ap_do_reset_defaults" = "1" ]; then
		ap_interface="wlan${1}"
		ap_radio_interface="wlan${1}"
		ap_ssid="test_ssid_wlan${1}"

		[ "$1" = "0" ] && echo "set ssid wlan$1 SSID MBO-test" >> $CMD_FILE
		[ "$1" = "2" ] && echo "set ssid wlan$1 SSID MBO-test2" >> $CMD_FILE
		echo "commit" >> $CMD_FILE
		echo "exit" >> $CMD_FILE
		clish $CMD_FILE > /dev/console
		rm -f $CMD_FILE
	fi
}

ap_oce_check_and_reset()
{
	debug_print "ap_oce_check_and_reset $1"

	ap_do_reset_defaults="0"
	radio_params=""
	sec_params=""
	ap_params=""

	local CMD_FILE="/tmp/sigma-cmds"
	echo "configure wlan" > $CMD_FILE
	echo "start" >> $CMD_FILE

	##############################################
	# fapi_wlan_wave_radio default setting
	##############################################
	#func						intf_idx	param_type		param_name		param_default_value
	ap_check_and_reset_one_param	$1		radio			radioEnable				1
	ap_check_and_reset_one_param	$1		radio			IEEE80211hEnabled		0
	ap_check_and_reset_one_param	$1		radio			WaveFRAutoRate			1
#	ap_check_and_reset_one_param	$1		radio			WaveCompleteRecoveryEnabled	0
#	ap_check_and_reset_one_param	$1		radio			WaveFullRecoveryEnabled		0

	##############################################
	# fapi_wlan_wave_security default setting
	##############################################
	#func						intf_idx	param_type		param_name		param_default_value
	ap_check_and_reset_one_param	$1		security		KeyPassphrase			12345678
	ap_check_and_reset_one_param	$1		security		PMKSACaching			0
	ap_check_and_reset_one_param	$1		security		AKMSuiteType			0
	ap_check_and_reset_one_param	$1		security		WPSAction				None
	ap_check_and_reset_one_param	$1		security		ManagementFrameProtection			1
	ap_check_and_reset_one_param	$1		security		ManagementFrameProtectionRequired	1
	ap_check_and_reset_one_param	$1		security		ModeEnabled				WPA2-Personal

	##############################################
	# fapi_wlan_wave_access_point default setting
	##############################################
	#func						intf_idx	param_type		param_name		param_default_value
	ap_check_and_reset_one_param	$1		ap				OceEnabled				1
	ap_check_and_reset_one_param	$1		ap				OceFilsDscvEnabled		1
	ap_check_and_reset_one_param	$1		ap				OceProbeLogicEnabled	1
	ap_check_and_reset_one_param	$1		ap				OceTxRateEnabled		1
	ap_check_and_reset_one_param	$1		ap				OceAssocRejectEnabled	1
	ap_check_and_reset_one_param	$1		ap				WaveMBOEnabled			0
	ap_check_and_reset_one_param	$1		ap				SSIDAdvertisementEnabled	true
	ap_check_and_reset_one_param	$1		ap				OceIncludeRnrInBeaconProbe	1
	ap_check_and_reset_one_param	$1		ap				OceIncludeRnrInFils		1
	ap_check_and_reset_one_param	$1		ap				OceBleStaCount			0
	ap_check_and_reset_one_param	$1		ap				OceBleChannelUtil		0
	ap_check_and_reset_one_param	$1		ap				OceBleAvailAdminCap		0
	ap_check_and_reset_one_param	$1		ap				OceAirTimeFract			0
	ap_check_and_reset_one_param	$1		ap				OceDataPpduDuration		0
	ap_check_and_reset_one_param	$1		ap				OceFilsHlp				0
	ap_check_and_reset_one_param	$1		ap				OceNAIRealm				0
	ap_check_and_reset_one_param	$1		ap				OceDhcpServIpAddr		None
	ap_check_and_reset_one_param	$1		ap				OceBAWInSize			None
	ap_check_and_reset_one_param	$1		ap				OceDataFormat			None
	ap_check_and_reset_one_param	$1		ap				OceESPIE				0
#	ap_check_and_reset_one_param	$1		ap				WaveMBOEnabled			0

	if [ "$ap_do_reset_defaults" = "1" ]; then
		ap_interface="wlan${1}"
		ap_radio_interface="wlan${1}"

		[ "$1" = "0" ] && echo "set ssid wlan$1 SSID OCE-test" >> $CMD_FILE
		[ "$1" = "2" ] && echo "set ssid wlan$1 SSID OCE-test2" >> $CMD_FILE
		echo "commit" >> $CMD_FILE
		echo "exit" >> $CMD_FILE
		clish $CMD_FILE > /dev/console
		rm -f $CMD_FILE
	fi
}

ap_he_check_and_reset()
{
	debug_print "ap_he_check_and_reset $1"
	local tmp_testbed_mode tmp_bw tmp_mode_val tmp_channel tmp_bw_txop_com_start_bw_limit tmp_val
	local nss_mcs_def_val_dl nss_mcs_def_val_ul wave11hradardetect_val tmp_ldpc tmp_edca_ie

	ap_do_reset_defaults="0"
	radio_params=""
	sec_params=""
	ap_params=""

	## disable dual PCI on both cards
	## need to consider if we need this onetime configuration
	## this will do reboot to the board then need re-start the test again.
	## (nothing will be done if setting is correct).
	# echo 0 | fapiToolBox dp

	## disable recovery on both cards
	## need to add fapiToolBox if needed same as dual PCI ( consider to have one command for both).

	# clear VAPs created by previous MBSSID tests
	clish_del_existing_vaps

	# clear fixed rate in OFDMA MU only
	# TODO: replace this temporary command with clish configuration (through DB)
	info_print "iwpriv wlan$1 sDoSimpleCLI 70 0"

	# reset value for MU fixed rate
	iwpriv wlan$1 sDoSimpleCLI 70 0

	# reset value for trigger interval (ap_trig_interval) - param3: interval value [10ms] ;param 4: fixed interval (0=false)
	iw wlan$1 iwlwav sDoSimpleCLI 3 13 1 0

	# set AGER=ON (default)
	# TODO: replace this temporary command with clish configuration (through DB)
	#info_print "iwpriv wlan$1 sPdThresh 2 300 0"
	#iwpriv wlan$1 sPdThresh 2 300 0

	# only in 5GHz, change channel check time to 60 sec (default)
	# TODO: replace this temporary command with clish configuration (through DB)
	if [ "$1" = "2" ]; then
		info_print "iwpriv wlan$1 s11hChCheckTime 60"
		iwpriv wlan$1 s11hChCheckTime 60
	fi

	## related to HE-4.29.1_2.4G test - BAR in G rates instead of B rates to save time.
	iw dev wlan$1 iwlwav sRtsRate 0
	info_print "iw dev wlan$1 iwlwav sRtsRate 0"

	##############################################
	# init global vars
	##############################################
	glob_ofdma_phase_format=""
	glob_ssid=""
	glob_ap_mimo=""
	global_ap_mbssid=""
	global_mcs_fixedrate=""
	global_num_non_tx_bss=""
	global_is_vaps_created=""
	global_mu_txbf=""

	# remove (if exists) temporary MBSSID clish command setting files
	rm -f $CMD_MBSS_WIRELESS_FILE $CMD_MBSS_SECURITY_FILE

	##############################################
	# fapi_wlan_wave_radio default setting
	##############################################
	if [ "$1" = "0" ]; then
		 tmp_bw="20MHz"
		 tmp_mode_val="b,g,n,ax"
		 wave11hradardetect_val="0"
		 [ "$ucc_type" = "testbed" ] && tmp_ldpc="0" || tmp_ldpc="1"
	fi
	if [ "$1" = "2" ]; then
		tmp_bw="80MHz"
		tmp_mode_val="a,n,ac,ax"
		wave11hradardetect_val="0"  # turn radar detection off
		tmp_ldpc="1"
	fi
	# check if wlan0 is 5GHz (possible only in single interface support)
	if [ "$SINGLE_INTF_SUPPORT" = "1" ]; then
		tmp_channel=`cat /proc/net/mtlk/wlan$1/channel | grep primary_channel | awk '{print $2}'`
		[ $tmp_channel -ge 36 ] && tmp_bw="80MHz" && tmp_mode_val="a,n,ac,ax" && wave11hradardetect_val="0"
	fi

	[ "$tmp_bw" = "20MHz" ] && tmp_bw_txop_com_start_bw_limit=0
	[ "$tmp_bw" = "40MHz" ] && tmp_bw_txop_com_start_bw_limit=1
	[ "$tmp_bw" = "80MHz" ] && tmp_bw_txop_com_start_bw_limit=2
	[ "$tmp_bw" = "160MHz" ] && tmp_bw_txop_com_start_bw_limit=3

	[ "$ucc_type" = "testbed" ] && tmp_testbed_mode=1 || tmp_testbed_mode=0

	local CMD_FILE="/tmp/sigma-cmds"
	echo "configure wlan" > $CMD_FILE
	echo "start" >> $CMD_FILE

	#func						intf_idx	param_type		param_name							param_default_value
	ap_check_and_reset_one_param	$1		radio			radioEnable							1
	ap_check_and_reset_one_param	$1		radio			MaxNumVaps							30
	ap_check_and_reset_one_param	$1		radio			OperatingChannelBandwidth			"$tmp_bw"
	ap_check_and_reset_one_param	$1		radio			WaveFRBandwidth						"$tmp_bw"
	ap_check_and_reset_one_param	$1		radio			OperatingStandards					"$tmp_mode_val"
	ap_check_and_reset_one_param	$1		radio			HtLDPCenabled						"$tmp_ldpc"
	ap_check_and_reset_one_param	$1		radio			VhtLDPCenabled						"$tmp_ldpc"
	ap_check_and_reset_one_param	$1		radio			HeLdpcCodingInPayload				"$tmp_ldpc"
	ap_check_and_reset_one_param	$1		radio			WaveFRAutoRate						1
	ap_check_and_reset_one_param	$1		radio			WaveFRNss							2
	ap_check_and_reset_one_param	$1		radio			WaveFRMcs							7
	ap_check_and_reset_one_param	$1		radio			WaveFRPhyMode						ax
	ap_check_and_reset_one_param	$1		radio			WaveFRCpMode						1
	ap_check_and_reset_one_param	$1		radio			WaveltfAndGiFixedRate				Auto
	ap_check_and_reset_one_param	$1		radio			WaveltfAndGiValue					HtVht0p4usCP-He0p8usCP-1xLTF

	# OFDMA MU - static plan common part

	ap_check_and_reset_one_param	$1		radio			Wave11hRadarDetect					$wave11hradardetect_val
	ap_check_and_reset_one_param	$1		radio			WaveHeMuOperationEnable				1
	ap_check_and_reset_one_param	$1		radio			WaveSPEnable						0
	ap_check_and_reset_one_param	$1		radio			WaveSPSequenceType					1	# change in DB
	ap_check_and_reset_one_param	$1		radio			WaveSPTxopComStartBwLimit			$tmp_bw_txop_com_start_bw_limit
	ap_check_and_reset_one_param	$1		radio			WaveSPDlComPhasesFormat				0
	ap_check_and_reset_one_param	$1		radio			WaveSPDlComNumOfParticipatingStations		0 # change in DB
	ap_check_and_reset_one_param	$1		radio			WaveSPDlComNumberOfPhaseRepetitions	0
	#ap_check_and_reset_one_param	$1		radio			WaveSPDlComMaximumPpduTransmissionTimeLimit	2700 # change in DB
	ap_check_and_reset_one_param	$1		radio			WaveSPDlComRfPower					26
	ap_check_and_reset_one_param	$1		radio			WaveSPDlComHeCp						2
	ap_check_and_reset_one_param	$1		radio			WaveSPDlComHeLtf					2
	ap_check_and_reset_one_param	$1		radio			WaveSPDlComMuType					0
	ap_check_and_reset_one_param	$1		radio			WaveSPUlComHeCp						2
	ap_check_and_reset_one_param	$1		radio			WaveSPUlComHeLtf					2
	ap_check_and_reset_one_param	$1		radio			WaveSPRcrComTfHegiAndLtf			2
	ap_check_and_reset_one_param	$1		radio			WaveSPRcrComTfLength				2914
	ap_check_and_reset_one_param	$1		radio			WaveSPDlUsrPsduRatePerUsp1			27
	ap_check_and_reset_one_param	$1		radio			WaveSPDlUsrPsduRatePerUsp2			27
	ap_check_and_reset_one_param	$1		radio			WaveSPTfUsrTfMpduMuSpacingFactor1	0
	ap_check_and_reset_one_param	$1		radio			WaveSPTfUsrTfMpduMuSpacingFactor2	0
	ap_check_and_reset_one_param	$1		radio			WaveSPTfUsrTfMpduMuSpacingFactor3	0
	ap_check_and_reset_one_param	$1		radio			WaveSPTfUsrTfMpduMuSpacingFactor4	0
	ap_check_and_reset_one_param	$1		radio			WaveSPRcrTfUsrTargetRssi1			70
	ap_check_and_reset_one_param	$1		radio			WaveSPRcrTfUsrTargetRssi2			70
	ap_check_and_reset_one_param	$1		radio			WaveSPRcrTfUsrTargetRssi3			70
	ap_check_and_reset_one_param	$1		radio			WaveSPRcrTfUsrTargetRssi4			70

	# OFDMA MU - static plan per-user part
	if [ "$tmp_testbed_mode" = "1" ]; then
		let nss_mcs_def_val_dl="($nss_def_val_dl_testbed-1)*16+$mcs_def_val_dl_testbed"
		let nss_mcs_def_val_ul="($nss_def_val_ul_testbed-1)*16+$mcs_def_val_ul_testbed"

		he_nss_mcs_def_val_dl=`get_nss_mcs_val $nss_def_val_dl_testbed $mcs_def_val_dl_testbed`
		if [ "$he_nss_mcs_def_val_dl" = "" ]
		then
			error_print "Unsupported value - nss_def_val_dl_testbed:$nss_def_val_dl_testbed mcs_def_val_dl_testbed:$mcs_def_val_dl_testbed"
			send_invalid ",errorCode,520"
			he_nss_mcs_def_val_dl=65520
		fi

		he_nss_mcs_def_val_ul=`get_nss_mcs_val $nss_def_val_ul_testbed $mcs_def_val_ul_testbed`
		if [ "$he_nss_mcs_def_val_ul" = "" ]
		then
			error_print "Unsupported value - nss_def_val_ul_testbed:$nss_def_val_ul_testbed mcs_def_val_ul_testbed:$mcs_def_val_ul_testbed"
			send_invalid ",errorCode,540"
			he_nss_mcs_def_val_ul=65520
		fi

		## JIRA WLANRTSYS-10947: WaveHeMuEdcaIePresent
		tmp_edca_ie="0"

	else # dut mode
		let nss_mcs_def_val_dl="($nss_def_val_dl-1)*16+$mcs_def_val_dl"
		let nss_mcs_def_val_ul="($nss_def_val_ul-1)*16+$mcs_def_val_ul"

		he_nss_mcs_def_val_dl=`get_nss_mcs_val $nss_def_val_dl $mcs_def_val_dl`
		if [ "$he_nss_mcs_def_val_dl" = "" ]
		then
			error_print "Unsupported value - nss_def_val_dl:$nss_def_val_dl mcs_def_val_dl:$mcs_def_val_dl"
			send_invalid ",errorCode,560"
			he_nss_mcs_def_val_dl=65530
		fi

		he_nss_mcs_def_val_ul=`get_nss_mcs_val $nss_def_val_ul $mcs_def_val_ul`
		if [ "$he_nss_mcs_def_val_ul" = "" ]
		then
			error_print "Unsupported value - nss_def_val_ul:$nss_def_val_ul mcs_def_val_ul:$mcs_def_val_ul"
			send_invalid ",errorCode,580"
			he_nss_mcs_def_val_ul=65530
		fi
		## # JIRA WLANRTSYS-10947: WaveHeMuEdcaIePresent
		tmp_edca_ie="1"
	fi

	ap_check_and_reset_one_param	$1		radio			WaveSPDlUsrPsduRatePerUsp1			$nss_mcs_def_val_dl
	ap_check_and_reset_one_param	$1		radio			WaveSPDlUsrPsduRatePerUsp2			$nss_mcs_def_val_dl
	ap_check_and_reset_one_param	$1		radio			WaveSPDlUsrPsduRatePerUsp3			$nss_mcs_def_val_dl
	ap_check_and_reset_one_param	$1		radio			WaveSPDlUsrPsduRatePerUsp4			$nss_mcs_def_val_dl

	ap_check_and_reset_one_param	$1		radio			WaveSPDlUsrUlPsduRatePerUsp1		$nss_mcs_def_val_ul
	ap_check_and_reset_one_param	$1		radio			WaveSPDlUsrUlPsduRatePerUsp2		$nss_mcs_def_val_ul
	ap_check_and_reset_one_param	$1		radio			WaveSPDlUsrUlPsduRatePerUsp3		$nss_mcs_def_val_ul
	ap_check_and_reset_one_param	$1		radio			WaveSPDlUsrUlPsduRatePerUsp4		$nss_mcs_def_val_ul

	ap_check_and_reset_one_param	$1		radio			WaveSPRcrTfUsrPsduRate1				27
	ap_check_and_reset_one_param	$1		radio			WaveSPRcrTfUsrPsduRate2				27
	ap_check_and_reset_one_param	$1		radio			WaveSPRcrTfUsrPsduRate3				27
	ap_check_and_reset_one_param	$1		radio			WaveSPRcrTfUsrPsduRate4				27

	# reset sub-band according to interface (wlan0 or wlan2)
	tmp_val=0;[ "$1" = "2" ] && tmp_val=0
	ap_check_and_reset_one_param	$1		radio			WaveSPDlUsrSubBandPerUsp1			$tmp_val
	ap_check_and_reset_one_param	$1		radio			WaveSPRcrTfUsrSubBand1				$tmp_val
	tmp_val=0;[ "$1" = "2" ] && tmp_val=1
	ap_check_and_reset_one_param	$1		radio			WaveSPDlUsrSubBandPerUsp2			$tmp_val
	ap_check_and_reset_one_param	$1		radio			WaveSPRcrTfUsrSubBand2				$tmp_val
	tmp_val=0;[ "$1" = "2" ] && tmp_val=2
	ap_check_and_reset_one_param	$1		radio			WaveSPDlUsrSubBandPerUsp3			$tmp_val
	ap_check_and_reset_one_param	$1		radio			WaveSPRcrTfUsrSubBand3				$tmp_val
	tmp_val=0;[ "$1" = "2" ] && tmp_val=3
	ap_check_and_reset_one_param	$1		radio			WaveSPRcrTfUsrSubBand4				$tmp_val
	ap_check_and_reset_one_param	$1		radio			WaveSPDlUsrSubBandPerUsp4			$tmp_val

	# reset start-RU according to interface (wlan0 or wlan2)
	tmp_val=0;[ "$1" = "2" ] && tmp_val=0
	ap_check_and_reset_one_param	$1		radio			WaveSPDlUsrStartRuPerUsp1			$tmp_val
	ap_check_and_reset_one_param	$1		radio			WaveSPRcrTfUsrStartRu1				$tmp_val
	tmp_val=2;[ "$1" = "2" ] && tmp_val=0
	ap_check_and_reset_one_param	$1		radio			WaveSPDlUsrStartRuPerUsp2			$tmp_val
	ap_check_and_reset_one_param	$1		radio			WaveSPRcrTfUsrStartRu2				$tmp_val
	tmp_val=5;[ "$1" = "2" ] && tmp_val=0
	ap_check_and_reset_one_param	$1		radio			WaveSPDlUsrStartRuPerUsp3			$tmp_val
	ap_check_and_reset_one_param	$1		radio			WaveSPRcrTfUsrStartRu3				$tmp_val
	tmp_val=7;[ "$1" = "2" ] && tmp_val=0
	ap_check_and_reset_one_param	$1		radio			WaveSPDlUsrStartRuPerUsp4			$tmp_val
	ap_check_and_reset_one_param	$1		radio			WaveSPRcrTfUsrStartRu4				$tmp_val

	# reset RU-size according to interface (wlan0 or wlan2)
	tmp_val=1;[ "$1" = "2" ] && tmp_val=3
	ap_check_and_reset_one_param	$1		radio			WaveSPDlUsrRuSizePerUsp1			$tmp_val
	ap_check_and_reset_one_param	$1		radio			WaveSPRcrTfUsrRuSize1				$tmp_val
	ap_check_and_reset_one_param	$1		radio			WaveSPDlUsrRuSizePerUsp2			$tmp_val
	ap_check_and_reset_one_param	$1		radio			WaveSPRcrTfUsrRuSize2				$tmp_val
	ap_check_and_reset_one_param	$1		radio			WaveSPDlUsrRuSizePerUsp3			$tmp_val
	ap_check_and_reset_one_param	$1		radio			WaveSPRcrTfUsrRuSize3				$tmp_val
	ap_check_and_reset_one_param	$1		radio			WaveSPDlUsrRuSizePerUsp4			$tmp_val
	ap_check_and_reset_one_param	$1		radio			WaveSPRcrTfUsrRuSize4				$tmp_val

	ap_check_and_reset_one_param	$1		radio			WaveSPRcrTfUsrLdpc1					1
	ap_check_and_reset_one_param	$1		radio			WaveSPRcrTfUsrLdpc2					1
	ap_check_and_reset_one_param	$1		radio			WaveSPRcrTfUsrLdpc3					1
	ap_check_and_reset_one_param	$1		radio			WaveSPRcrTfUsrLdpc4					1

	ap_check_and_reset_one_param	$1		radio			WaveSPRcrTfUsrCodingTypeBccOrLpdc1	1
	ap_check_and_reset_one_param	$1		radio			WaveSPRcrTfUsrCodingTypeBccOrLpdc2	1
	ap_check_and_reset_one_param	$1		radio			WaveSPRcrTfUsrCodingTypeBccOrLpdc3	1
	ap_check_and_reset_one_param	$1		radio			WaveSPRcrTfUsrCodingTypeBccOrLpdc4	1
	if [ "$ucc_type" = "dut" ]; then
		ap_check_and_reset_one_param	$1		radio			HeAMsduInAMpduSupport			1
	else
		ap_check_and_reset_one_param	$1		radio			HeAMsduInAMpduSupport			0
	fi
	ap_check_and_reset_one_param	$1		radio			WaveBfMode							Disabled
	ap_check_and_reset_one_param	$1		radio			WaveTestBedMode						$tmp_testbed_mode
	ap_check_and_reset_one_param	$1		radio			HeOperationBssColor					1
	ap_check_and_reset_one_param	$1		radio			HeheMcsNssRxHeMcsMapLessThanOrEqual80Mhz	$he_nss_mcs_def_val_ul
	ap_check_and_reset_one_param	$1		radio			HeheMcsNssTxHeMcsMapLessThanOrEqual80Mhz	$he_nss_mcs_def_val_dl
	ap_check_and_reset_one_param	$1		radio			HeheMcsNssRxHeMcsMap160Mhz					$he_nss_mcs_def_val_ul
	ap_check_and_reset_one_param	$1		radio			HeheMcsNssTxHeMcsMap160Mhz					$he_nss_mcs_def_val_dl
	# JIRA WLANRTSYS-11028: part0-Rx part1-Tx
	ap_check_and_reset_one_param	$1		radio			WaveVhtMcsSetPart0							$he_nss_mcs_def_val_ul
	ap_check_and_reset_one_param	$1		radio			WaveVhtMcsSetPart1							$he_nss_mcs_def_val_dl
	ap_check_and_reset_one_param	$1		radio			HeOmControlSupport						1
	# in case that Type=DUT enable TWT
	if [ "$ucc_type" = "dut" ]; then
		ap_check_and_reset_one_param $1		radio			HeTargetWakeTime						1
	else
		ap_check_and_reset_one_param $1		radio			HeTargetWakeTime						0
	fi
	ap_check_and_reset_one_param	$1		radio			VhtExtNssBw3							0
	ap_check_and_reset_one_param	$1		radio			WaveHtMinMpduStartSpacing				0
	ap_check_and_reset_one_param	$1		radio			WaveMultiBssEnable						0

	if [ "$ucc_type" = "dut" ]; then
		ap_check_and_reset_one_param	$1		radio			WaveHeSrControlHesigaSpatialReuse15Ena	0
		ap_check_and_reset_one_param	$1		radio			WaveHtOperationCohostedBss				1
	else
		ap_check_and_reset_one_param	$1		radio			WaveHeSrControlHesigaSpatialReuse15Ena	1
		ap_check_and_reset_one_param	$1		radio			WaveHtOperationCohostedBss				0
	fi
	## WLANRTSYS-11027
	ap_check_and_reset_one_param	$1		radio			WaveSPDlComMaximumPpduTransmissionTimeLimit 2700
	ap_check_and_reset_one_param	$1		radio			WaveHeMuEdcaIePresent						$tmp_edca_ie

	# WLANRTSYS-10947 Test bed AP default parameters
	if [ "$ucc_type" = "testbed" ]; then
		ap_check_and_reset_one_param	$1		radio			HeMacMaximumNumberOfFragmentedMsdusAmsdus	0
		ap_check_and_reset_one_param	$1		radio			HeMacMultiTidAggregationRxSupport			0
		ap_check_and_reset_one_param	$1		radio			HeMacMultiTidAggregationTxSupport			0
		ap_check_and_reset_one_param	$1		radio			HeMacMaximumAMpduLengthExponent				0
		ap_check_and_reset_one_param	$1		radio			HeAckEnabledAggregationSupport				0
		ap_check_and_reset_one_param	$1		radio			HeTriggeredCqiFeedback						0
		ap_check_and_reset_one_param	$1		radio			HeSuPpduAndHeMuPpduWith4xHeLtfAnd08usGi		0
		ap_check_and_reset_one_param	$1		radio			HeDeviceClass								0
		ap_check_and_reset_one_param	$1		radio			HePhyStbcTxLessThanOrEqual80mhz				0
		# Disabeld and enabled in the relavant TC for TXBF and MU_TXBF.
		ap_check_and_reset_one_param	$1		radio			HeSuBeamformerCapable						0
		ap_check_and_reset_one_param	$1		radio			HeMuBeamformerCapable						0
		ap_check_and_reset_one_param	$1		radio			HeSuBeamformeeCapableEnable					0
		# he_phy_max_nc = he_phy_beamformee_sts_for_greater_than_80mhz = he_phy_num_of_sounding_dimensions = 0
		ap_check_and_reset_one_param	$1		radio			WaveHeNumOfAntennas							1
		ap_check_and_reset_one_param	$1		radio			HeTriggeredSuBeamformingFeedback			0
	else ## DUT mode
		ap_check_and_reset_one_param	$1		radio			HeMacMaximumAMpduLengthExponent				2
	fi
	## WLANRTSYS-11027
	ap_check_and_reset_one_param	$1		radio				WaveMaxMpduLen						1650

	##############################################
	# fapi_wlan_wave_security default setting
	##############################################
	#func						intf_idx	param_type		param_name							param_default_value
	ap_check_and_reset_one_param	$1		security		KeyPassphrase						12345678
	ap_check_and_reset_one_param	$1		security		ManagementFrameProtection			0
	ap_check_and_reset_one_param	$1		security		ManagementFrameProtectionRequired	0
	ap_check_and_reset_one_param	$1		security		ModeEnabled							WPA2-Personal

	##############################################
	# fapi_wlan_wave_access_point default setting
	##############################################
	#func						intf_idx	param_type		param_name							param_default_value
	## WLANRTSYS-11027 A-MSDU Disabled for both DUT and TB.
	ap_check_and_reset_one_param	$1		ap				WaveAmsduEnabled					0
	ap_check_and_reset_one_param	$1		ap				WaveMBOEnabled						0
	ap_check_and_reset_one_param	$1		ap				WaveBaAgreementEnabled				1
	ap_check_and_reset_one_param	$1		ap				WaveBaWindowSize					64


	if [ "$ap_do_reset_defaults" = "1" ]; then
		ap_interface="wlan${1}"
		ap_radio_interface="wlan${1}"
		[ "$1" = "0" ] && echo "set ssid wlan$1 SSID HE-test" >> $CMD_FILE
		[ "$1" = "2" ] && echo "set ssid wlan$1 SSID HE-test2" >> $CMD_FILE
		echo "commit" >> $CMD_FILE
		echo "exit" >> $CMD_FILE
		clish $CMD_FILE > /dev/console
		rm -f $CMD_FILE
	fi
}

ap_reset_default()
{
	info_print "ap_reset_default():start"

	# check clish validity
	local clish_valid=`ap_clish_validity`
	if [ "$clish_valid" = "" ]; then
		while [ -n "$(pgrep -f clish)" ]; do
			info_print "ap_reset_default(): clish command is on-going. killall clish before test..."
			killall clish
			sleep 1
		done
		clish_valid=`ap_clish_validity`
		if [ "$clish_valid" = "" ]; then
			error_print "###################################################"
			error_print "###################################################"
			error_print "System error - clish is not available!!!"
			error_print "###################################################"
			error_print "###################################################"
			send_invalid ",errorCode,600"
			return
		else
			info_print "after killall -> clish is OK"
		fi
	else
		info_print "clish is OK"
	fi

	send_running

	ucc_program=""
	ucc_type=""
	trace_program=""
	global_rx_mcs_max_cap=""
	global_tx_mcs_max_cap=""
	global_nss_opt_ul=$nss_def_val_ul
	global_nss_opt_dl=$nss_def_val_dl

	while [ "$1" != "" ]; do
		# for upper case only
		upper "$1" token
		debug_print "while loop $1 - token:$token"
		case "$token" in
			NAME)
				shift
				ap_name=$1
			;;
			PROGRAM)
				shift
				lower "$1" ucc_program
			;;
			TYPE)
				shift
				lower "$1" ucc_type
			;;
			*)
				shift
			;;
		esac
		shift
	done

	# clear VAPs created by previous MBSSID tests
	clish_del_existing_vaps

	# check that type field is supported. otherwise, unexpected behavior may occur.
	if [ "$ucc_type" != "" ] && [ "$ucc_type" != "dut" ] && [ "$ucc_type" != "testbed" ]; then
		error_print "Unsupported value - ucc_type:$ucc_type"
		send_invalid ",errorCode,610"
		return
	fi

	[ "$ucc_program" != "" ] && trace_program="$ucc_program" || trace_program="Default"
	info_print "############ PROGRAM:$trace_program reset_default ucc_type=$ucc_type################"

	[ "$ucc_program" != "mbo" ] && [ -e $SIGMA_MBO_ON ] && rm $SIGMA_MBO_ON

	if [ "$ucc_program" = "mbo" ]; then

		[ ! -e $SIGMA_MBO_ON ] && touch $SIGMA_MBO_ON

		## kill hostapd events listener
		TERMINATE_EVENT_LISTENER="1"

		ap_mbo_check_and_reset 0
		[ "$SINGLE_INTF_SUPPORT" = "0" ] && ap_mbo_check_and_reset 2

		# remove all Beerock files and restart listening to hostapd events via fapi_wlan_debug_cli
		killall fapi_wlan_debug_cli 1>&2
		rm -rf $CONF_DIR/Bee* 1>&2
		/usr/sbin/fapi_wlan_debug_cli AML_START 1>&2 &

		# TODO: What about really reseting to default config?? Or setting default MBO config according to test plan?
		# How do you suggest to do? factorycfg.sh will reboot and disconnect from testbed and fail the test, also pre-configured VAPs will be deleted
	elif [ "$ucc_program" = "oce" ]; then
		ap_oce_check_and_reset 0
		[ "$SINGLE_INTF_SUPPORT" = "0" ] && ap_oce_check_and_reset 2

		info_print "ap_reset_default():AML_FIXED_TX_POWER_SET wlan0 58"
		info_print "ap_reset_default():AML_FIXED_TX_POWER_SET wlan2 58"
		debug_print "/usr/sbin/fapi_wlan_debug_cli COMMAND \"AML_FIXED_TX_POWER_SET wlan0 58\""
		/usr/sbin/fapi_wlan_debug_cli COMMAND "AML_FIXED_TX_POWER_SET wlan0 58" >> $CONF_DIR/BeeRock_CMD.log
		debug_print "/usr/sbin/fapi_wlan_debug_cli COMMAND \"AML_FIXED_TX_POWER_SET wlan2 58\""
		/usr/sbin/fapi_wlan_debug_cli COMMAND "AML_FIXED_TX_POWER_SET wlan2 58" >> $CONF_DIR/BeeRock_CMD.log

		# remove all Beerock files and restart listening to hostapd events via fapi_wlan_debug_cli
		killall fapi_wlan_debug_cli 1>&2
		rm -rf $CONF_DIR/Bee* 1>&2
		/usr/sbin/fapi_wlan_debug_cli AML_START 1>&2 &
	elif [ "$ucc_program" = "he" ]; then
		[ -e ${SMDPIPE} ] && echo "EXIT" > ${SMDPIPE}
		ap_he_check_and_reset 0
		[ "$SINGLE_INTF_SUPPORT" = "0" ] && ap_he_check_and_reset 2
	else
		ap_default_check_and_reset 0
		[ "$SINGLE_INTF_SUPPORT" = "0" ] && ap_default_check_and_reset 2
	fi

	send_complete
	info_print "ap_reset_default():completed"
}

ap_get_info()
{
	# ap_get_info,NAME,name_of_AP
	# NAME parameter is ignored here

	send_running

	wd_list=`iwconfig 2>/dev/null | grep ^[a-z] | cut -f0 -d" "| xargs echo `
	debug_print "wd_list:$wd_list"
	IFS=" "
	for wd in $wd_list; do
		debug_print "device $wd"
		wrd="${wd%*.*}"
		debug_print "radio device $wrd"
		w24=`host_api get $$ hw_$wrd HW_24G`
		w52=`host_api get $$ hw_$wrd HW_52G`

		if [ "$w24" != "0" ] && [ "$w24" != "1" ]; then
			w24="0"
		fi
		if [ "$w52" != "0" ] && [ "$w52" != "1" ]; then
			w52="0"
		fi

		debug_print "w24:$w24 w52:$w52"

		case "$w24$w52" in
			00)
				# no wlan
				debug_print "case 00"
			;;
			10)
				debug_print "case 10"
				wlist="${wlist}${wd}_24G "
			;;
			01)
				debug_print "case 01"
				wlist="${wlist}${wd}_52G "
			;;
			11)
				debug_print "case 11"
				wlist="${wlist}${wd}_any "
			;;
		esac
	done

	if [ "$wlist" != "" ]; then
		answer=",$wlist"
	fi

	debug_print "wlist:$wlist"
	debug_print "answer:$answer"

	send_complete "$answer"
}

ap_deauth_sta()
{
	ap_name="INTEL"
	# default to wlan0 in case no interface parameter is given
	ap_interface=""

	IFS=" "
	send_running
	# checking for interface parameter (might fail if some value is also "interface")
	get_interface_name $@

	while [ "$1" != "" ]; do
		# for upper case only
		upper "$1" token
		debug_print "while loop $1 - token:$token"
		case "$token" in
			NAME)
				shift
				ap_name=$1
			;;
			INTERFACE)
				# skip since it was read in loop before
				shift
			;;
			STA_MAC_ADDRESS)
				shift
				lower "$1" ap_sta_mac
			;;
			MINORCODE)
				shift
				lower "$1" ap_minor_code
			;;
			*)
				error_print "while loop error $1"
				send_invalid ",errorCode,620"
				return
			;;
		esac
		shift
	done

	#hostapd_cli -i $ap_interface deauthenticate $ap_sta_mac > /dev/null
	hostapd_cli -i $ap_interface deauthenticate $ap_interface $ap_sta_mac > /dev/null
	send_complete
}

ap_get_mac_address()
{
	ap_name="INTEL"
	ap_interface="$AP_IF_ACTIVE"
	ap_interface_get_mac=""

	IFS=" "
	send_running

	[ -z "$ap_interface" ] && get_interface_name $@

	while [ "$1" != "" ]; do
		# for upper case only
		upper "$1" token
		debug_print "while loop $1 - token:$token"
		case "$token" in
			WLAN_TAG)
				shift
				ap_wlan_tag_for_mac=$1
			;;
			NONTXBSSINDEX)
				shift
				[ $1 -lt 1 ] && [ $1 -gt 8 ] && error_print "NonTxBSSIndex invalid value '$1'" && send_invalid ",errorCode,625" && return
				ap_non_tx_bss_index=$1
			;;
			*)
				debug_print "while loop 3819 no need to handle param $1"
				# getting here is expected! we do NOT support all fields; do NOT Abort/Return
			;;
		esac
		shift
	done

	## dual band tags support - tags 1 and 2 are related to main APs
	## assuming that wlan2 is tag1 and wlan0 is tag0.
	[ "$ap_wlan_tag_for_mac" = "1" ] && ap_interface_get_mac="wlan2"
	[ "$ap_wlan_tag_for_mac" = "2" ] && ap_interface_get_mac="wlan0"
	[ "$ap_wlan_tag_for_mac" = "3" ] && ap_interface_get_mac="wlan2.0"
	[ "$ap_wlan_tag_for_mac" = "4" ] && ap_interface_get_mac="wlan0.0"

	if [ -n "$ap_non_tx_bss_index" ]; then
		vap_index=$((ap_non_tx_bss_index-1))
		ap_interface_get_mac="$ap_interface.$vap_index"
	fi

	info_print "ap_get_mac_address() for interface $ap_interface_get_mac"

	if [ "$ap_interface_get_mac" != "" ]; then
		wdmac=`cat /sys/class/net/$ap_interface_get_mac/address`
	else
		wdmac=`cat /sys/class/net/$ap_interface/address`
	fi

	info_print "MAC = $wdmac"

	send_complete ",mac,$wdmac"
}

device_get_hw_model_info()
{
	local hw_revision hw_model_name nof_interfaces

	# set default value
	hw_model_name=$MODEL

	# parse the eeprom_info file
	if [ -f $CONF_DIR/eeprom_info ]; then
		nof_interfaces=`cat $CONF_DIR/eeprom_info | grep -c "HW revision"`
		hw_revision=`cat $CONF_DIR/eeprom_info | grep -m 1 "HW revision" | awk '{print $4}'`

		# TODO: finalize the model names. determine the name for 614 + 624.
		[ "$hw_revision" = "0x41" ] && [ "$nof_interfaces" = "1" ] && hw_model_name="$hw_model_name-614"
		[ "$hw_revision" = "0x42" ] && [ "$nof_interfaces" = "1" ] && hw_model_name="$hw_model_name-624"
		[ "$hw_revision" = "0x45" ] && [ "$nof_interfaces" = "2" ] && hw_model_name="$hw_model_name-654"
	fi
	echo "$hw_model_name"
}

device_get_info()
{
	send_running
	# configure following values: vendor, model name, FW version
	send_complete ",vendor,$VENDOR,model,$HW_MODEL,version,$FW_VERSION"
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
		send_invalid ",errorCode,99"
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

info_print "Sigma-AP Agent version $CA_VERSION is running ..."

HW_MODEL=`device_get_hw_model_info`
debug_print "HW_MODEL:$HW_MODEL"

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
	tline=`echo $tline`
	info_print "tline: >>>$tline<<<"
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
