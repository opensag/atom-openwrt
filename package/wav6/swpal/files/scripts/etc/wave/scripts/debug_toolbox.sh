#!/bin/sh

. /lib/wifi/platform_dependent.sh

script_name="$0"
command=$1
pc_ip=$2
param1=$3

burn_cal_file()
{
	local no_restart tftp_path interfaces_list interface_name burn_both cal_status

	no_restart=$1
	[ -z "$pc_ip" ] && echo "The PC IP parameter is missing." && exit

	tftp_path=${param1%\/*}
	interfaces_list=${param1##*\/}
	if [ "$tftp_path" = "$interfaces_list" ]
	then
		tftp_path=""
	else
		tftp_path="$tftp_path/"
	fi

	cd /tmp/
	cal_status=0
	interface_name=${interfaces_list%%,*}
	while [ -n "$interface_name" ]
	do
		if [ "$interface_name" = "all" ]
		then
			tftp -gr "${tftp_path}cal_wlan0.bin" -l cal_wlan0.bin $pc_ip
			cal_status=$(( $cal_status + `echo $?` ))
			tftp -gr "${tftp_path}cal_wlan2.bin" -l cal_wlan2.bin $pc_ip
			cal_status=$(( $cal_status + `echo $?` ))
			tftp -gr "${tftp_path}cal_wlan4.bin" -l cal_wlan4.bin $pc_ip
			cal_status=$(( $cal_status + `echo $?` ))
		else
			tftp -gr "${tftp_path}cal_${interface_name}.bin" -l cal_${interface_name}.bin $pc_ip
			cal_status=$(( $cal_status + `echo $?` ))
		fi
		interfaces_list=${interfaces_list#$interface_name}
		interfaces_list=${interfaces_list#,}
		interface_name=${interfaces_list%%,*}
	done
	cd - > /dev/null
	
	${SCRIPTS_PATH}/flash_file_saver.sh
	ret=$?
	if [ $ret = 0 ]
	then
		echo "$script_name: calibration files saved to flash, rebooting..."
		reboot
	else
		echo "$script_name: ERROR - failed to save calibration files to flash." >&2
		exit -1
	fi
}

remove_flash_cal_files()
{
	if [ -d "/nvram/etc/wave_calibration" ]
	then
		chattr -i /nvram/etc/wave_calibration #remove write protection
		rm -rf /nvram/etc/wave_calibration
		sync
		reboot
	fi
}

case $command in
	burn_cal)
		burn_cal_file
	;;
	remove_cal)
		remove_flash_cal_files
	;;
	*)
		echo -e "$script_name: Unknown command $command\n \
		Usage: $script_name COMMAND [Argument 1] [Argument 2]\n" \
		 "\n" \
		 "Commnads:\n" \
		 "burn_cal       Burn the calibration files\n" \
		 "  Arguments:\n" \
		 "  Argument 1:  Your PC IP\n" \
		 "  Argument 2:  The interface name or names to which calibration is burned: wlan0/wlan2/wlan4/all\n" \
		 "               Names can be specified in a comma-separated list: wlan0,wlan2\n" \
		 "               This argument can contain also the path in the tftp server before the interface name: /path/wlan\n" \
		 "               Example: $script_name burn_cal <PC IC> /private_folder/wlan0,wlan2,wlan4\n" \
		 "remove_cal     Removes /nvram/etc/wave_calibration directory if exists\n"
	;;
esac
