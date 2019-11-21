#!/bin/sh

###########################
# Help & Input Parameters #
###########################
hotplug_path=`grep FIRMWARE_DIR= /etc/hotplug/firmware.agent | sed 's/FIRMWARE_DIR=//'` # FIRMWARE_DIR in /etc/hotplug/firmware.agent is /tmp in both UGW and CGR
eeprom_tar=eeprom.tar.gz
script_name="$0"
src_file=$1
dst_file=${hotplug_path}/$2
####################################################
# Copy source file to hotplug directory (optional) #
####################################################
if [ -e "$src_file" ]
then
	cp "$src_file" "$dst_file"
	ret=$?
	if [ $ret = 0 ]
	then
		echo "$script_name: file '$dst_file' saved."
	else
		echo "$script_name error: failed to save file '$dst_file'" >&2
		exit -1
	fi
fi
###########################################
# Detect which flash saving method to use #
###########################################
# Set default value to Puma mode
burn_mode="Puma"

# Check for upgrade tool (UGW mode)
image=`which upgrade`
status=$?
if [ $status -eq 0 ]
then
	burn_mode="UGW" 
else
	# Check for haven_park
	haven_park=""
	[ -e /etc/config.sh ] && haven_park=`grep haven_park /etc/config.sh` 
	[ "$haven_park" != "" ] && burn_mode="haven_park"
fi
###############################################
# Write the new calibration file to the FLASH #
###############################################
cd $hotplug_path
if [ "$burn_mode" = "Puma" ]
then
	#Puma: save calibration files uncompressed to nvram/etc/wave_calibration folder
	cal_path=/nvram/etc/wave_calibration
	if [ ! -d "$cal_path" ]
	then
		mkdir $cal_path
	fi
	#remove write protection
	chattr -i $cal_path
	cp ${hotplug_path}/cal_*.bin $cal_path
	#restore write protection
	chattr +i $cal_path
	sync
else
	# Create tarball from calibration bins
	tar czf $eeprom_tar cal_*.bin
	if [ $? != 0 ]
	then
		echo "$script_name: failed to create $eeprom_tar" >&2
		exit -1
	fi
	
	if [ "$burn_mode" = "haven_park" ]
	then
		#haven_park:save calibration files compressed to /nvram/ folder
		cp ${hotplug_path}/$eeprom_tar /nvram/
		sync
	else
		#UGW: Use upgrade tool to save calibration files compressed to flash partition
		upgrade ${hotplug_path}/$eeprom_tar wlanconfig 0 0
		if [ $? != 0 ]
		then
			echo "$script_name: the partition wlanconfig doesn't exist and cannot be created" >&2
			exit -1
		fi
	fi
fi
cd - > /dev/null

exit 0
