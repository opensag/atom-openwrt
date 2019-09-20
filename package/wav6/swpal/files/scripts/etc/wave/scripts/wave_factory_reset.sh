#!/bin/sh

#
# This script performs wifi factory reet
#

UCI_DB_PATH=/nvram/etc/config
DEFAULT_DB_PATH=/etc/wave/db/
DEFAULT_DB_RADIO_5=$DEFAULT_DB_PATH/wireless_def_radio_5g
DEFAULT_DB_RADIO_24=$DEFAULT_DB_PATH/wireless_def_radio_24g
DEFAULT_DB_VAP=$DEFAULT_DB_PATH/wireless_def_vap_db
DEFAULT_DB_DUMMY_VAP=$DEFAULT_DB_PATH/wireless_def_dummy_vap_db
UCI=/usr/bin/uci
DEFAULT_DB_VAP_SPECIFIC=$DEFAULT_DB_PATH/wireless_def_vap_
TMP_CONF_FILE=$(mktemp /tmp/tmpConfFile.XXXXXX)

DEFAULT_NO_OF_VAPS=4
DUMMY_VAP_OFSET=100

function usage(){
	echo "usage: $0 [vap|radio|all_radios] <interface name>" > /dev/console
}

# Check the MAC address of the interface.
function check_mac_address(){
	local board_mac_check ret_val

	board_mac_check=$1

	ret_val=1

	board_mac_check=${board_mac_check//:/""}
	if [ "${board_mac_check//[0-9A-Fa-f]/}" != "" ]; then
		echo "$0: wrong MAC Address format!" > /dev/console
		ret_val=0
	fi

	echo $ret_val
}

# Calculate and update the MAC address of the interface.
update_mac_address()
{

	# Define local parameters
	local interface_name radio_name radio_index vap_index phy_offset board_mac mac_address \
	board_mac1 board_mac23 board_mac46 vap_mac4 vap_mac5 vap_mac6

	interface_name=$1

	#TODO: for station use master interface (for example for wlan1 radio_name should be wlan0).
	radio_name=${interface_name%%.*}
	[ "$radio_name" = "wlan0" ] && phy_offset=16
	[ "$radio_name" = "wlan2" ] && phy_offset=33
	[ "$radio_name" = "wlan4" ] && phy_offset=50

	#TODO: for station interfaces vap_index=1
	if [ "$radio_name" == "$interface_name" ] ; then
		# master VAP
		vap_index=0
	else
		# slave VAPs
		vap_index=${interface_name##*.}
		vap_index=$((vap_index+2))
	fi

        which itstore_mac_db > /dev/null
        if [ $? -eq 0 ]; then
                erouter0_mac=`itstore_mac_db`
                if [ "$(check_mac_address $erouter0_mac)" = "1" ]; then
                        board_mac="$erouter0_mac"
                else
                        echo "$0: ERROR: Retrival of default MAC Address from ITstore Failed !" > /dev/console
                        echo "$0: Check the ITstore Production settings and get the correct Mac address" > /dev/console
                        board_mac="00:50:F1:80:00:00"
                fi
        elif [ -e "/nvram/etc/wave/wav_base_mac" ]; then
                source /nvram/etc/wave/wav_base_mac
                board_mac=${board_mac##*HWaddr }
                board_mac=${board_mac%% *}
        else
                board_mac=`ifconfig eth0`
                board_mac=${board_mac##*HWaddr }
                board_mac=${board_mac%% *}
        fi

	if [ "$board_mac" == "00:50:F1:64:D7:FE" ] || [ "$board_mac" == "00:50:F1:80:00:00" ]
	then
		echo "$0:  USING DEAFULT MAC, MAC should be different than 00:50:F1:64:D7:FE and 00:50:F1:80:00:00 ##" > /dev/console
	fi

	# Divide the board MAC address to the first 3 bytes and the last 3 byte (which we are going to increment).
	board_mac1=0x`echo $board_mac | cut -c 1-2`
	board_mac23=`echo $board_mac | cut -c 4-8`
	board_mac46=0x`echo $board_mac | sed s/://g | cut -c 7-12`

	# Increment the last byte by the the proper incrementation according to the physical interface (wlan0/wlan2/wlan4)
	board_mac46=$((board_mac46+phy_offset))

	# For STA, use MAC of physical AP incremented by 1 (wlan1 increment wlan0 by 1).
	# For VAP, use MAC of physical AP incremented by the index of the interface name + 2 (wlan0.0 increment wlan0 by 2, wlan2.2 increment wlan2 by 2).
	board_mac46=$((board_mac46+$vap_index))

	# Generate the new MAC.
	vap_mac4=$((board_mac46/65536))
	board_mac46=$((board_mac46-vap_mac4*65536))
	vap_mac5=$((board_mac46/256))
	board_mac46=$((board_mac46-vap_mac5*256))
	vap_mac6=$board_mac46
	# If the 4th byte is greater than FF (255) set it to 00.
	[ $vap_mac4 -ge 256 ] && vap_mac4=0

	mac_address=`printf '%02X:%s:%02X:%02X:%02X' $board_mac1 $board_mac23 $vap_mac4 $vap_mac5 $vap_mac6`
	echo "$mac_address"
}


function set_conf_to_file(){
	rpc_idx=$1
	specific_conf_file=$2
	template_conf_file=$3
	output_conf_file=$4

	if [ -f $specific_conf_file ]; then
		local tmp_conf_file=$(mktemp /tmp/Local_tmp_conf_file.XXXXXX)
		cat $template_conf_file | sed "s,option ssid '[^']*,&_$rpc_idx,g" > $tmp_conf_file

		arr=`cat $specific_conf_file | grep "option" | awk '{ print $2 }' | uniq`
		for item in $arr
		do
			sed -i "/$item/d" $tmp_conf_file
		done

		cat $specific_conf_file >> $tmp_conf_file
		cat $tmp_conf_file >> $output_conf_file
		rm $tmp_conf_file
	else
		# save all the rest of the defaults + add suffix to ssid
		cat $template_conf_file | sed "s,option ssid '[^']*,&_$rpc_idx,g" >> $output_conf_file
	fi
}


function full_reset(){

	echo "$0: Performing full factory reset..." > /dev/console

	if [ ! -d $UCI_DB_PATH ]; then
		mkdir -p $UCI_DB_PATH
	fi

	# network file is required by UCI
	if [ ! -f $UCI_DB_PATH/network ]; then
		touch $UCI_DB_PATH/network
	fi

	# clean uci cache
	uci revert wireless > /dev/null 2>&1
	uci revert meta-wireless > /dev/null 2>&1

	# Setup default wireless UCI DB

	cat /dev/null > $UCI_DB_PATH/wireless
	rm $UCI_DB_PATH/meta-wireless > /dev/null 2>&1
	rm /tmp/meta-wireless > /dev/null 2>&1
	radios=`ifconfig -a | grep "wlan[0|2|4] " | awk '{ print $1 }'`

	local tmp_wireless=$(mktemp /tmp/wireless.XXXXXX)
	local tmp_meta=$(mktemp /tmp/meta-wireless.XXXXXX)

	# Fill Radio interfaces
	for iface in $radios; do
		iface_idx=`echo $iface | sed "s/[^0-9]//g"`
		phy_idx=`iw $iface info | grep wiphy | awk '{ print $2 }'`
		new_mac=`update_mac_address $iface`
		`iw phy$phy_idx info | grep "* 5... MHz" > /dev/null`
		is_radio_5g=$?
		echo "config wifi-device 'radio$iface_idx'" >> $tmp_wireless
		echo "        option phy 'phy$phy_idx'"	>> $tmp_wireless
		echo "        option macaddr '$new_mac'" >> $tmp_wireless

		# the radio configuration files must be named in one of the following formats:
		# <file name>
		# <file name>_<iface idx>
		# <file name>_<iface idx>_<HW type>_<HW revision>
		board=`iw dev $iface iwlwav gEEPROM | grep "HW type\|HW revision" | awk '{print $4}' | tr '\n' '_' | sed "s/.$//"`
		if [ $is_radio_5g = '0' ]; then
			set_conf_to_file $iface_idx ${DEFAULT_DB_RADIO_5}_${iface_idx} $DEFAULT_DB_RADIO_5  $TMP_CONF_FILE
			set_conf_to_file $iface_idx ${DEFAULT_DB_RADIO_5}_${iface_idx}_${board} $TMP_CONF_FILE $tmp_wireless
		else
			set_conf_to_file $iface_idx ${DEFAULT_DB_RADIO_24}_${iface_idx} $DEFAULT_DB_RADIO_24  $TMP_CONF_FILE
			set_conf_to_file $iface_idx ${DEFAULT_DB_RADIO_24}_${iface_idx}_${board} $TMP_CONF_FILE $tmp_wireless
		fi
		rm -f $TMP_CONF_FILE

		# Add per-radio meta-data
		echo "config wifi-device 'radio$iface_idx'" >> $tmp_meta
		echo "        option param_changed '1'" >> $tmp_meta
		echo "        option interface_changed '0'" >> $tmp_meta

		# Add dummy VAP
		dummy_idx=$((DUMMY_VAP_OFSET+iface_idx))
		echo "config wifi-iface 'default_radio$dummy_idx'" >> $tmp_wireless
		echo "        option device 'radio$iface_idx'" >> $tmp_wireless
		echo "        option ifname 'wlan$iface_idx'" >> $tmp_wireless

		# Add dummy VAP meta-data
		echo "config wifi-iface 'default_radio$dummy_idx'" >> $tmp_meta
		echo "        option device 'radio$iface_idx'" >> $tmp_meta
		echo "        option param_changed '0'" >> $tmp_meta

		new_mac=`update_mac_address wlan$iface_idx`
		echo "        option macaddr '$new_mac'" >> $tmp_wireless

		# save all the rest of the defaults + add suffix to ssid
		cat $DEFAULT_DB_DUMMY_VAP | sed "s,option ssid '[^']*,&_$iface_idx,g" >> $tmp_wireless
		
		# Fill VAP interfaces
		vap_idx=0
		while [ $vap_idx -lt $DEFAULT_NO_OF_VAPS ]; do
			rpc_idx=$((10+iface_idx*16+vap_idx))

			echo "config wifi-iface 'default_radio$rpc_idx'" >> $tmp_wireless
			echo "        option device 'radio$iface_idx'" >> $tmp_wireless
			minor=".$vap_idx"
			
			echo "        option ifname 'wlan$iface_idx$minor'" >> $tmp_wireless
			new_mac=`update_mac_address wlan$iface_idx$minor`
			echo "        option macaddr '$new_mac'" >> $tmp_wireless

			set_conf_to_file $rpc_idx $DEFAULT_DB_VAP_SPECIFIC$rpc_idx $DEFAULT_DB_VAP  $tmp_wireless

			# Add per-vap meta-data
			echo "config wifi-iface 'default_radio$rpc_idx'" >> $tmp_meta
			echo "        option device 'radio$iface_idx'" >> $tmp_meta
			echo "        option param_changed '0'" >> $tmp_meta

			vap_idx=$((vap_idx+1))

		done
	done


	mv $tmp_wireless $UCI_DB_PATH/wireless
	mv $tmp_meta /tmp/meta-wireless
	ln -s /tmp/meta-wireless $UCI_DB_PATH/meta-wireless

	echo "$0: Done..." > /dev/console

}

function reset_radio(){

	echo "$0: Performing radio reset for radio $1..." > /dev/console
	iface_name=$1
	radio_idx=`echo $iface_name | sed "s/[^0-9]//g"`
	phy_idx=`iw $iface_name info | grep wiphy | awk '{ print $2 }'`

	# remove all configurable parameters, since there might be parameters which do not exist in the default db file.
	extra_params=`$UCI show wireless.radio$radio_idx | grep "wireless.radio$radio_idx\." | grep -v "\.phy" \
	| grep -v "\.type" | grep -v "\.macaddr" | awk -F"=" '{ print $1 }'`

	for option in $extra_params
	do
		$UCI delete $option
	done
	
	`iw phy$phy_idx info | grep "* 5... MHz" > /dev/null`
	is_radio_5g=$?

	# the radio configuration files must be named in one of the following formats:
	# <file name>
	# <file name>_<radio idx>
	# <file name>_<radio idx>_<HW type>_<HW revision>
	board=`iw dev $iface_name iwlwav gEEPROM | grep "HW type\|HW revision" | awk '{print $4}' | tr '\n' '_' | sed "s/.$//"`
	if [ $is_radio_5g = '0' ]; then
		set_conf_to_file $radio_idx ${DEFAULT_DB_RADIO_5}_${radio_idx} $DEFAULT_DB_RADIO_5  ${TMP_CONF_FILE}_
		set_conf_to_file $radio_idx ${DEFAULT_DB_RADIO_5}_${radio_idx}_${board} ${TMP_CONF_FILE}_ $TMP_CONF_FILE
	else
		set_conf_to_file $radio_idx ${DEFAULT_DB_RADIO_24}_${radio_idx} $DEFAULT_DB_RADIO_24  ${TMP_CONF_FILE}_
		set_conf_to_file $radio_idx ${DEFAULT_DB_RADIO_24}_${radio_idx}_${board} ${TMP_CONF_FILE}_ $TMP_CONF_FILE
	fi
	rm ${TMP_CONF_FILE}_
	db_file=$TMP_CONF_FILE

	#set default params from template file
	while read line
	do
		param=`echo $line | awk '{ print $2 }'`
		# read value from default db. remove extra \ and '
		value=`echo $line | sed "s/[ ]*option $param //g" |  sed "s/[\\']//g"`
		
		$UCI set wireless.radio$radio_idx.$param="$value"
		res=`echo $?`
		if [ $res -ne '0' ]; then
			echo "$0: Error setting $param..." > /dev/console
		fi
	done < $db_file
	rm -f $TMP_CONF_FILE

	$UCI set meta-wireless.radio${radio_idx}.param_changed=1

	echo "$0: Done..." > /dev/console
}

function reset_vap(){

	echo "$0: Performing Vap reset for VAP $1 ..." > /dev/console

	def_radio=`$UCI show | grep ifname=\'$1\' | awk -F"." '{ print $2 }'`
	if [ X$def_radio == "X" ]; then
		echo "$0: Error, can't find VPA in UCI DB" > /dev/console
		exit 1
	fi
	
	glob_vap_idx=`echo $def_radio | sed "s/[^0-9]*//"`

	# remove all configurable parameters, since there might be parameters which do not exist in the default db file.
	extra_params=`$UCI show wireless.$def_radio | grep "wireless.$def_radio\." | grep -v "\.device" \
	| grep -v "\.ifname" |  grep -v "\.macaddr" | awk -F"=" '{ print $1 }'`

	for option in $extra_params
	do
		$UCI delete $option
	done

	set_conf_to_file $glob_vap_idx $DEFAULT_DB_VAP_SPECIFIC$glob_vap_idx $DEFAULT_DB_VAP  $TMP_CONF_FILE

	#set default params from template file
	while read line
	do
		param=`echo $line | awk '{ print $2 }'`
		# read value from default db and add _<index> to SSID. remove extra \ and '
		value=`echo $line | sed "s/[ ]*option $param //g" |  sed "s/[\\']//g"`
		
		$UCI set wireless.$def_radio.$param="$value"
		res=`echo $?`
		if [ $res -ne '0' ]; then
			echo "$0: Error setting $param..." > /dev/console
		fi
	done < $TMP_CONF_FILE
	rm $TMP_CONF_FILE

	$UCI set meta-wireless.${def_radio}.param_changed=1

	echo "$0: Done..." > /dev/console

}


case $1 in
	radio)
		if [ "$#" -ne 2 ]; then
			usage
			exit 1
		fi
		reset_radio $2
		break
		;;
	all_radios)
		radios=`ifconfig -a | grep "wlan[0|2|4] " | awk '{ print $1 }'`
		for iface in $radios; do
			reset_radio $iface
		done
		break
		;;
	vap)
		if [ "$#" -ne 2 ]; then
			usage
			exit 1
		fi
		reset_vap $2
		break
		;;
	*)
		full_reset
		;;
esac

