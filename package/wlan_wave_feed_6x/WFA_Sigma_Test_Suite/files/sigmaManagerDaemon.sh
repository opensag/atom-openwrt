#!/bin/sh
# Sigma Manager Daemon (SMD)

# source for common definitions with FAPI
[ ! "$LIB_COMMON_SOURCED" ] && . /opt/lantiq/wave/scripts/fapi_wlan_wave_lib_common.sh

# source for common and debug tools
[ ! "$SIGMA_COMMON_LIB_SOURCED" ] && . /opt/lantiq/wave/scripts/sigma-common-lib.sh

# make sure there is no running clish command
check_before_send_command()
{
	while [ -n "$(pgrep -f clish)" ]; do
		info_print "SMD: clish command is on-going. waiting ..."
		sleep 1
	done
	debug_print "SMD: clish command finished. continue ..."
}

# send plan upon STA connectivity status per interface.
force_plan_sending()
{
	local num_of_sta interface_name
	interface_name=$1

	num_of_sta=`get_num_of_connected_sta $interface_name`
	# send plan only if we have 1, 2 or more connected STA
	[ $num_of_sta -gt 0 ] && send_plan_per_nof_sta $interface_name
}

# send plan off per interface. check if needed before sending.
send_plan_off()
{
	local interface_name sp_enable
	interface_name=$1

	sp_enable=`get_plan_enable $interface_name`
	if 	[ "$sp_enable" = "1" ]; then
		info_print "SMD: send_plan_off $interface_name"
		clish -c "configure wlan" -c "set radio $interface_name WaveSPEnable 0" > /dev/console
	else
		debug_print "SMD: send_plan_off $interface_name: NO NEED to send"
	fi
	smd_active_plan=0 # active plan values: 0=no plan, 1=plan_of_1_sta, 2=plan_of_2_sta, 4=plan_of_4_sta
}

# send plan for 1 user.
send_plan_for_1_user()
{
	local interface_name start_bw_limit aid_list aid_index sta_index index usr_index \
	dl_sub_band1 dl_start_ru1 dl_ru_size1 ul_sub_band1 ul_start_ru1 ul_ru_size1 \
	dl_sub_band2 dl_start_ru2 dl_ru_size2 ul_sub_band2 ul_start_ru2 ul_ru_size2 \
	mu_type sp_enable_value tc_name

	interface_name=$1
	start_bw_limit=`get_plan_bw $interface_name`
	mu_type=`get_plan_dl_com_mu_type $interface_name`

	info_print "SMD: send_plan_for_1_user on $interface_name"

	# prepare static planner clish cmd file
	echo "configure wlan" > sigmaManagerCmds
	echo "start" >> sigmaManagerCmds

	# update common part (WaveHeMuOperationEnable is on by default)
	sp_enable_value=`get_sp_enable_val $glob_ssid`
	echo "set radio $interface_name WaveSPEnable $sp_enable_value" >> sigmaManagerCmds
	echo "set radio $interface_name WaveSPDlComNumOfParticipatingStations 1" >> sigmaManagerCmds

	# update 1 user plan according to BW.
	# 0-20MHz, 1-40MHz, 2-80MHz, 3-160MHz
	case "$start_bw_limit" in
		"0")
			#USER1
			dl_sub_band1=0;dl_start_ru1=0;dl_ru_size1=3;ul_sub_band1=0;ul_start_ru1=0;ul_ru_size1=3
		;;
		"1")
			#USER1
			dl_sub_band1=0;dl_start_ru1=0;dl_ru_size1=4;ul_sub_band1=0;ul_start_ru1=0;ul_ru_size1=4
		;;
		"2")
			#USER1
			dl_sub_band1=0;dl_start_ru1=0;dl_ru_size1=5;ul_sub_band1=0;ul_start_ru1=0;ul_ru_size1=5
		;;
		"3")
			#USER1
			dl_sub_band1=0;dl_start_ru1=0;dl_ru_size1=6;ul_sub_band1=0;ul_start_ru1=0;ul_ru_size1=6
		;;
	esac

	# update 1 user params in DB
	usr_index=1
	local tmp_param tmp_val
	tmp_param="dl_sub_band$usr_index";eval tmp_val=\$$tmp_param
	[ -n "$tmp_val" ] && echo "set radio $interface_name WaveSPDlUsrSubBandPerUsp$usr_index ${tmp_val}" >> sigmaManagerCmds
	tmp_param="dl_start_ru$usr_index";eval tmp_val=\$$tmp_param
	[ -n "$tmp_val" ] && echo "set radio $interface_name WaveSPDlUsrStartRuPerUsp$usr_index ${tmp_val}" >> sigmaManagerCmds
	tmp_param="dl_ru_size$usr_index";eval tmp_val=\$$tmp_param
	[ -n "$tmp_val" ] && echo "set radio $interface_name WaveSPDlUsrRuSizePerUsp$usr_index ${tmp_val}" >> sigmaManagerCmds
	tmp_param="ul_sub_band$usr_index";eval tmp_val=\$$tmp_param
	[ -n "$tmp_val" ] && echo "set radio $interface_name WaveSPRcrTfUsrSubBand$usr_index ${tmp_val}" >> sigmaManagerCmds
	tmp_param="ul_start_ru$usr_index";eval tmp_val=\$$tmp_param
	[ -n "$tmp_val" ] && echo "set radio $interface_name WaveSPRcrTfUsrStartRu$usr_index ${tmp_val}" >> sigmaManagerCmds
	tmp_param="ul_ru_size$usr_index";eval tmp_val=\$$tmp_param
	[ -n "$tmp_val" ] && echo "set radio $interface_name WaveSPRcrTfUsrRuSize$usr_index ${tmp_val}" >> sigmaManagerCmds
	#tmp_param="ul_psdu_rate_per_usp$usr_index";eval tmp_val=\$$tmp_param
	#[ -n "$tmp_val" ] && echo "set radio $interface_name WaveSPDlUsrUlPsduRatePerUsp$usr_index ${tmp_val}" >> sigmaManagerCmds
	#tmp_param="spr_cr_tf_usr_psdu_rate$usr_index";eval tmp_val=\$$tmp_param
	#[ -n "$tmp_val" ] && echo "set radio $interface_name WaveSPRcrTfUsrPsduRate$usr_index ${tmp_val}" >> sigmaManagerCmds

	## update ldpc according to STA ##
	sp_check_ldpc_support $interface_name
	aid_index=`hostapd_cli -i${interface_name} all_sta $interface_name | grep aid=`
	aid_index=${aid_index##*=}
	eval ldpc_support=\${ldpc_${aid_index}}
	if [ "$ldpc_support" != "" ]; then
		echo "set radio $interface_name WaveSPRcrTfUsrLdpc${usr_index} $ldpc_support" >> sigmaManagerCmds
		echo "set radio $interface_name WaveSPRcrTfUsrCodingTypeBccOrLpdc${usr_index} $ldpc_support" >> sigmaManagerCmds
	fi

	[ $aid_index -gt 0 ] && let sta_index=$aid_index-1
	echo "set radio $interface_name WaveSPDlUsrUspStationIndexes${usr_index} ${sta_index}" >> sigmaManagerCmds

	# Change the length according to maximum NSS value of the connected STAs.
	[ "$mu_type" = "0" ] && ap_mu_tf_len=`sp_set_plan_tf_length $interface_name`

	tc_name=`get_tc_name $glob_ssid`
	if [ "$tc_name" = "5.60.1" ]; then
		ap_mu_tf_len="1486"
	fi

	echo "set radio $interface_name WaveSPRcrComTfLength $ap_mu_tf_len" >> sigmaManagerCmds
	# send clish cmd to activate the static plan
	echo "commit" >> sigmaManagerCmds
	echo "exit" >> sigmaManagerCmds
	clish sigmaManagerCmds > /dev/console
	mv sigmaManagerCmds sigmaManagerCmds2UsersSent
}


# send plan for 2 users.
send_plan_for_2_users()
{
	local interface_name start_bw_limit aid_list aid_index sta_index usr_index \
	dl_sub_band1 dl_start_ru1 dl_ru_size1 ul_sub_band1 ul_start_ru1 ul_ru_size1 \
	dl_sub_band2 dl_start_ru2 dl_ru_size2 ul_sub_band2 ul_start_ru2 ul_ru_size2 \
	mu_type sp_enable_value aid_idx_sorted_list tmp_param tmp_val ap_aid_index \
	is_set_low_ru_size

	interface_name=$1
	start_bw_limit=`get_plan_bw $interface_name`
	mu_type=`get_plan_dl_com_mu_type $interface_name`

	info_print "SMD: send_plan_for_2_users on $interface_name"

	# prepare static planner clish cmd file
	echo "configure wlan" > sigmaManagerCmds
	echo "start" >> sigmaManagerCmds

	# update common part (WaveHeMuOperationEnable is on by default)
	sp_enable_value=`get_sp_enable_val $glob_ssid`
	echo "set radio $interface_name WaveSPEnable $sp_enable_value" >> sigmaManagerCmds
	echo "set radio $interface_name WaveSPDlComNumOfParticipatingStations 2" >> sigmaManagerCmds

	# update 2 user plan according to BW.
	# 0-20MHz, 1-40MHz, 2-80MHz, 3-160MHz
	case "$start_bw_limit" in
		"0")
			if [ "$mu_type" = "0" ]; then
				## OFDMA ##
				#USER1
				dl_sub_band1=0;dl_start_ru1=0;dl_ru_size1=2;ul_sub_band1=0;ul_start_ru1=0;ul_ru_size1=2
				#USER2
				dl_sub_band2=0;dl_start_ru2=5;dl_ru_size2=2;ul_sub_band2=0;ul_start_ru2=5;ul_ru_size2=2
			else
				## MIMO ##
				#USER1
				dl_sub_band1=0;dl_start_ru1=0;dl_ru_size1=3;ul_sub_band1=0;ul_start_ru1=0;ul_ru_size1=2;ul_psdu_rate_per_usp1=4;spr_cr_tf_usr_psdu_rate1=4
				#USER2
				dl_sub_band2=0;dl_start_ru2=0;dl_ru_size2=3;ul_sub_band2=0;ul_start_ru2=5;ul_ru_size2=2;ul_psdu_rate_per_usp2=4;spr_cr_tf_usr_psdu_rate2=4
			fi
		;;
		"1")
			if [ "$mu_type" = "0" ]; then
				## OFDMA ##
				#USER1
				dl_sub_band1=0;dl_start_ru1=0;dl_ru_size1=3;ul_sub_band1=0;ul_start_ru1=0;ul_ru_size1=3
				#USER2
				dl_sub_band2=1;dl_start_ru2=0;dl_ru_size2=3;ul_sub_band2=1;ul_start_ru2=0;ul_ru_size2=3
			else
				## MIMO ##
				#USER1
				dl_sub_band1=0;dl_start_ru1=0;dl_ru_size1=4;ul_sub_band1=0;ul_start_ru1=0;ul_ru_size1=3;ul_psdu_rate_per_usp1=4;spr_cr_tf_usr_psdu_rate1=4
				#USER2
				dl_sub_band2=0;dl_start_ru2=0;dl_ru_size2=4;ul_sub_band2=1;ul_start_ru2=0;ul_ru_size2=3;ul_psdu_rate_per_usp2=4;spr_cr_tf_usr_psdu_rate2=4
			fi
		;;
		"2")
			if [ "$mu_type" = "0" ]; then
				## OFDMA ##
				#USER1
				dl_sub_band1=0;dl_start_ru1=0;dl_ru_size1=4;ul_sub_band1=0;ul_start_ru1=0;ul_ru_size1=4
				#USER2
				dl_sub_band2=2;dl_start_ru2=0;dl_ru_size2=4;ul_sub_band2=2;ul_start_ru2=0;ul_ru_size2=4
			else
				## MIMO ##
				#USER1
				dl_sub_band1=0;dl_start_ru1=0;dl_ru_size1=5;ul_sub_band1=0;ul_start_ru1=0;ul_ru_size1=4;ul_psdu_rate_per_usp1=4;spr_cr_tf_usr_psdu_rate1=4
				#USER2
				dl_sub_band2=0;dl_start_ru2=0;dl_ru_size2=5;ul_sub_band2=2;ul_start_ru2=0;ul_ru_size2=4;ul_psdu_rate_per_usp2=4;spr_cr_tf_usr_psdu_rate2=4
			fi
		;;
		"3")
			if [ "$mu_type" = "0" ]; then
				## OFDMA ##
				#USER1
				dl_sub_band1=0;dl_start_ru1=0;dl_ru_size1=5;ul_sub_band1=0;ul_start_ru1=0;ul_ru_size1=5
				#USER2
				dl_sub_band2=4;dl_start_ru2=0;dl_ru_size2=5;ul_sub_band2=4;ul_start_ru2=0;ul_ru_size2=5
			else
				## MIMO ##
				#USER1
				dl_sub_band1=0;dl_start_ru1=0;dl_ru_size1=6;ul_sub_band1=0;ul_start_ru1=0;ul_ru_size1=5;ul_psdu_rate_per_usp1=4;spr_cr_tf_usr_psdu_rate1=4
				#USER2
				dl_sub_band2=0;dl_start_ru2=0;dl_ru_size2=6;ul_sub_band2=4;ul_start_ru2=0;ul_ru_size2=5;ul_psdu_rate_per_usp2=4;spr_cr_tf_usr_psdu_rate2=4
			fi
		;;
	esac

	is_set_low_ru_size="0"

	for ap_aid_index in 1 2
	do
		tmp_param="dl_ru_size$ap_aid_index"
		eval tmp_val=\${$tmp_param}

		# WLANRTSYS-9745: check if lower value is needed only if it is still not needed
		if [ $is_set_low_ru_size = "0" ]; then
			is_set_low_ru_size=`is_set_low_ru_size_get $interface_name $tmp_val $ap_aid_index`
		fi
	done

	# update per-user params in DB
	for usr_index in 1 2
	do
		tmp_param="dl_sub_band$usr_index";eval tmp_val=\$$tmp_param
		[ -n "$tmp_val" ] && echo "set radio $interface_name WaveSPDlUsrSubBandPerUsp$usr_index ${tmp_val}" >> sigmaManagerCmds
		tmp_param="dl_start_ru$usr_index";eval tmp_val=\$$tmp_param
		[ -n "$tmp_val" ] && echo "set radio $interface_name WaveSPDlUsrStartRuPerUsp$usr_index ${tmp_val}" >> sigmaManagerCmds
		tmp_param="ul_sub_band$usr_index";eval tmp_val=\$$tmp_param
		[ -n "$tmp_val" ] && echo "set radio $interface_name WaveSPRcrTfUsrSubBand$usr_index ${tmp_val}" >> sigmaManagerCmds
		tmp_param="ul_start_ru$usr_index";eval tmp_val=\$$tmp_param
		[ -n "$tmp_val" ] && echo "set radio $interface_name WaveSPRcrTfUsrStartRu$usr_index ${tmp_val}" >> sigmaManagerCmds
		tmp_param="ul_ru_size$usr_index";eval tmp_val=\$$tmp_param
		[ -n "$tmp_val" ] && echo "set radio $interface_name WaveSPRcrTfUsrRuSize$usr_index ${tmp_val}" >> sigmaManagerCmds
		tmp_param="ul_psdu_rate_per_usp$usr_index";eval tmp_val=\$$tmp_param
		[ -n "$tmp_val" ] && echo "set radio $interface_name WaveSPDlUsrUlPsduRatePerUsp$usr_index ${tmp_val}" >> sigmaManagerCmds
		tmp_param="spr_cr_tf_usr_psdu_rate$usr_index";eval tmp_val=\$$tmp_param
		[ -n "$tmp_val" ] && echo "set radio $interface_name WaveSPRcrTfUsrPsduRate$usr_index ${tmp_val}" >> sigmaManagerCmds

		if [ $is_set_low_ru_size = "1" ]; then
			echo "set radio $interface_name WaveSPDlUsrRuSizePerUsp$usr_index 2" >> sigmaManagerCmds
		else
			tmp_param="dl_ru_size$usr_index";eval tmp_val=\$$tmp_param
			[ -n "$tmp_val" ] && echo "set radio $interface_name WaveSPDlUsrRuSizePerUsp$usr_index ${tmp_val}" >> sigmaManagerCmds
		fi
	done

	sp_check_ldpc_support $interface_name

	# dynamically update STA index in DB according to: 20 MHz STA first, then NSS (higher to lower NSS)
	aid_idx_sorted_list=`get_sta_aid_idx_sorted_list $interface_name`
	for ap_aid_index in 1 2
	do
		aid_index=`aid_idx_out_of_list_get $aid_idx_sorted_list $ap_aid_index`
		[ $aid_index -gt 0 ] && sta_index=$((aid_index-1))
		echo "set radio $interface_name WaveSPDlUsrUspStationIndexes${ap_aid_index} ${sta_index}" >> sigmaManagerCmds
		eval ldpc_support=\${ldpc_${aid_index}}
		if [ "$ldpc_support" != "" ]; then
			echo "set radio $interface_name WaveSPRcrTfUsrLdpc${ap_aid_index} $ldpc_support" >> sigmaManagerCmds
			echo "set radio $interface_name WaveSPRcrTfUsrCodingTypeBccOrLpdc${ap_aid_index} $ldpc_support" >> sigmaManagerCmds
		fi
	done

	# Change the length according to maximum NSS value of the connected STAs.
	[ "$mu_type" = "0" ] && ap_mu_tf_len=`sp_set_plan_tf_length $interface_name`

	tc_name=`get_tc_name $glob_ssid`
	if [ "$tc_name" = "4.56.1" ]; then
		ap_mu_tf_len="1486"
	fi

	[ -n "$ap_mu_tf_len" ] && echo "set radio $interface_name WaveSPRcrComTfLength $ap_mu_tf_len" >> sigmaManagerCmds

	# send clish cmd to activate the static plan
	echo "commit" >> sigmaManagerCmds
	echo "exit" >> sigmaManagerCmds
	clish sigmaManagerCmds > /dev/console
	mv sigmaManagerCmds sigmaManagerCmds2UsersSent
}

# send plan for 4 users.
send_plan_for_4_users()
{
	local interface_name start_bw_limit aid_list aid_index sta_index usr_index \
	dl_sub_band1 dl_start_ru1 dl_ru_size1 ul_sub_band1 ul_start_ru1 ul_ru_size1 \
	dl_sub_band2 dl_start_ru2 dl_ru_size2 ul_sub_band2 ul_start_ru2 ul_ru_size2 \
	dl_sub_band3 dl_start_ru3 dl_ru_size3 ul_sub_band3 ul_start_ru3 ul_ru_size3 \
	dl_sub_band4 dl_start_ru4 dl_ru_size4 ul_sub_band4 ul_start_ru4 ul_ru_size4 \
	mu_type sp_enable_value tc_name tmp_param tmp_val aid_idx_sorted_list \
	ap_aid_index is_set_low_ru_size

	interface_name=$1
	start_bw_limit=`get_plan_bw $interface_name`
	mu_type=`get_plan_dl_com_mu_type $interface_name`

	info_print "SMD: send_plan_for_4_users on $interface_name"

	# prepare static planner clish cmd file
	echo "configure wlan" > sigmaManagerCmds
	echo "start" >> sigmaManagerCmds

	# update common part (WaveHeMuOperationEnable is on by default)
	sp_enable_value=`get_sp_enable_val $glob_ssid`
	echo "set radio $interface_name WaveSPEnable $sp_enable_value" >> sigmaManagerCmds
	echo "set radio $interface_name WaveSPDlComNumOfParticipatingStations 4" >> sigmaManagerCmds

	# update 4 user plan according to BW.
	# 0-20MHz, 1-40MHz, 2-80MHz, 3-160MHz
	case "$start_bw_limit" in
		"0")
			if [ "$mu_type" = "0" ]; then
				## OFDMA ##
				#USER1
				dl_sub_band1=0;dl_start_ru1=0;dl_ru_size1=1;ul_sub_band1=0;ul_start_ru1=0;ul_ru_size1=1
				#USER2
				dl_sub_band2=0;dl_start_ru2=2;dl_ru_size2=1;ul_sub_band2=0;ul_start_ru2=2;ul_ru_size2=1
				#USER3
				dl_sub_band3=0;dl_start_ru3=5;dl_ru_size3=1;ul_sub_band3=0;ul_start_ru3=5;ul_ru_size3=1
				#USER4
				dl_sub_band4=0;dl_start_ru4=7;dl_ru_size4=1;ul_sub_band4=0;ul_start_ru4=7;ul_ru_size4=1
			else
				## MIMO ##
				info_print "SMD:start_bw_limit=$start_bw_limit:send_plan_for_4_users: MIMO not supported"
			fi
		;;
		"1")
			if [ "$mu_type" = "0" ]; then
				#USER1
				dl_sub_band1=0;dl_start_ru1=0;dl_ru_size1=2;ul_sub_band1=0;ul_start_ru1=0;ul_ru_size1=2
				#USER2
				dl_sub_band2=0;dl_start_ru2=5;dl_ru_size2=2;ul_sub_band2=0;ul_start_ru2=5;ul_ru_size2=2
				#USER3
				dl_sub_band3=1;dl_start_ru3=0;dl_ru_size3=2;ul_sub_band3=1;ul_start_ru3=0;ul_ru_size3=2
				#USER4
				dl_sub_band4=1;dl_start_ru4=5;dl_ru_size4=2;ul_sub_band4=1;ul_start_ru4=5;ul_ru_size4=2
			else
				## MIMO ##
				info_print "SMD:start_bw_limit=$start_bw_limit:send_plan_for_4_users: MIMO not supported"
			fi
		;;
		"2")
			if [ "$mu_type" = "0" ]; then
				#USER1
				dl_sub_band1=0;dl_start_ru1=0;dl_ru_size1=3;ul_sub_band1=0;ul_start_ru1=0;ul_ru_size1=3
				#USER2
				dl_sub_band2=1;dl_start_ru2=0;dl_ru_size2=3;ul_sub_band2=1;ul_start_ru2=0;ul_ru_size2=3
				#USER3
				dl_sub_band3=2;dl_start_ru3=0;dl_ru_size3=3;ul_sub_band3=2;ul_start_ru3=0;ul_ru_size3=3
				#USER4
				dl_sub_band4=3;dl_start_ru4=0;dl_ru_size4=3;ul_sub_band4=3;ul_start_ru4=0;ul_ru_size4=3
			else
				## MIMO ##
				info_print "SMD:start_bw_limit=$start_bw_limit:send_plan_for_4_users: MIMO not supported"
			fi
		;;
		"3")
			if [ "$mu_type" = "0" ]; then
				#USER1
				dl_sub_band1=0;dl_start_ru1=0;dl_ru_size1=4;ul_sub_band1=0;ul_start_ru1=0;ul_ru_size1=4
				#USER2
				dl_sub_band2=2;dl_start_ru2=0;dl_ru_size2=4;ul_sub_band2=2;ul_start_ru2=0;ul_ru_size2=4
				#USER3
				dl_sub_band3=4;dl_start_ru3=0;dl_ru_size3=4;ul_sub_band3=4;ul_start_ru3=0;ul_ru_size3=4
				#USER4
				dl_sub_band4=6;dl_start_ru4=0;dl_ru_size4=4;ul_sub_band4=6;ul_start_ru4=0;ul_ru_size4=4
			else
				## MIMO ##
				info_print "SMD:start_bw_limit=$start_bw_limit:send_plan_for_4_users: MIMO not supported"
			fi
		;;
	esac

	is_set_low_ru_size="0"

	for ap_aid_index in 1 2 3 4
	do
		tmp_param="dl_ru_size$ap_aid_index"
		eval tmp_val=\${$tmp_param}

		# WLANRTSYS-9745: check if lower value is needed only if it is still not needed
		if [ $is_set_low_ru_size = "0" ]; then
			is_set_low_ru_size=`is_set_low_ru_size_get $interface_name $tmp_val $ap_aid_index`
		fi
	done

	## WLANRTSYS-12035
	if [ $dl_ru_size1 -lt 2 ]; then
		echo "set radio $interface_name WaveSPDlComMaximumPpduTransmissionTimeLimit 2300" >> sigmaManagerCmds
	else
		echo "set radio $interface_name WaveSPDlComMaximumPpduTransmissionTimeLimit 2700" >> sigmaManagerCmds
	fi

	# update per-user params in DB
	for usr_index in 1 2 3 4
	do
		tmp_param="dl_sub_band$usr_index";eval tmp_val=\$$tmp_param
		[ -n "$tmp_val" ] && echo "set radio $interface_name WaveSPDlUsrSubBandPerUsp$usr_index ${tmp_val}" >> sigmaManagerCmds
		tmp_param="dl_start_ru$usr_index";eval tmp_val=\$$tmp_param
		[ -n "$tmp_val" ] && echo "set radio $interface_name WaveSPDlUsrStartRuPerUsp$usr_index ${tmp_val}" >> sigmaManagerCmds
		tmp_param="ul_sub_band$usr_index";eval tmp_val=\$$tmp_param
		[ -n "$tmp_val" ] && echo "set radio $interface_name WaveSPRcrTfUsrSubBand$usr_index ${tmp_val}" >> sigmaManagerCmds
		tmp_param="ul_start_ru$usr_index";eval tmp_val=\$$tmp_param
		[ -n "$tmp_val" ] && echo "set radio $interface_name WaveSPRcrTfUsrStartRu$usr_index ${tmp_val}" >> sigmaManagerCmds
		tmp_param="ul_ru_size$usr_index";eval tmp_val=\$$tmp_param
		[ -n "$tmp_val" ] && echo "set radio $interface_name WaveSPRcrTfUsrRuSize$usr_index ${tmp_val}" >> sigmaManagerCmds

		if [ $is_set_low_ru_size = "1" ]; then
			echo "set radio $interface_name WaveSPDlUsrRuSizePerUsp$usr_index 2" >> sigmaManagerCmds
		else
			tmp_param="dl_ru_size$usr_index";eval tmp_val=\$$tmp_param
			[ -n "$tmp_val" ] && echo "set radio $interface_name WaveSPDlUsrRuSizePerUsp$usr_index ${tmp_val}" >> sigmaManagerCmds
		fi
	done

	sp_check_ldpc_support $interface_name

	# dynamically update STA index in DB according to: 20 MHz STA first, then NSS (higher to lower NSS)
	aid_idx_sorted_list=`get_sta_aid_idx_sorted_list $interface_name`
	for ap_aid_index in 1 2 3 4
	do
		aid_index=`aid_idx_out_of_list_get $aid_idx_sorted_list $ap_aid_index`
		[ $aid_index -gt 0 ] && sta_index=$((aid_index-1))
		echo "set radio $interface_name WaveSPDlUsrUspStationIndexes${ap_aid_index} ${sta_index}" >> sigmaManagerCmds
		eval ldpc_support=\${ldpc_${aid_index}}
		if [ "$ldpc_support" != "" ]; then
			echo "set radio $interface_name WaveSPRcrTfUsrLdpc${ap_aid_index} $ldpc_support" >> sigmaManagerCmds
			echo "set radio $interface_name WaveSPRcrTfUsrCodingTypeBccOrLpdc${ap_aid_index} $ldpc_support" >> sigmaManagerCmds
		fi
	done

	# Change the length according to maximum NSS value of the connected STAs.
	[ "$mu_type" = "0" ] && ap_mu_tf_len=`sp_set_plan_tf_length $interface_name`
	[ -n "$ap_mu_tf_len" ] && echo "set radio $interface_name WaveSPRcrComTfLength $ap_mu_tf_len" >> sigmaManagerCmds

	# send clish cmd to activate the static plan
	echo "commit" >> sigmaManagerCmds
	echo "exit" >> sigmaManagerCmds
	clish sigmaManagerCmds > /dev/console
	mv sigmaManagerCmds sigmaManagerCmds4UsersSent
}

# send plan switch: 1, 2 or 4. Before sending, send plan off.
send_plan_per_nof_sta()
{
	local interface_name num_of_sta num_participating_users
	interface_name=$1

	num_of_sta=`get_num_of_connected_sta $interface_name`
	info_print "SMD: num_of_sta:$num_of_sta"

	num_participating_users=`get_plan_num_users $interface_name`
	info_print "SMD: num_participating_users:$num_participating_users"

	# use a global var to store the data for next invocation of this function.
	# at 1st entry, assign a value to smd_ofdma_num_users.
	# values: 0-no limitation, >0-num_participating_users is dictated by TC
	[ "$smd_ofdma_num_users" = "" ] && smd_ofdma_num_users=$num_participating_users

	# send plan according to the initial num_participating_users that was defined by the TC, if any.
	# num_users is an optional parameter that is forced by the UCC.
	# set the plan according to smd_ofdma_num_users only if it is equal or higher than num_of_sta.
	if [ "$smd_ofdma_num_users" != "0" ]; then
		info_print "SMD: set plan using smd_ofdma_num_users:$smd_ofdma_num_users"
		if [ "$smd_ofdma_num_users" = "$num_of_sta" ]; then
			if [ "$smd_ofdma_num_users" = "1" ]; then
				send_plan_off $interface_name
				send_plan_for_1_user $interface_name
				smd_active_plan=1 # active plan values: 0=no plan, 1=plan_of_1_sta, 2=plan_of_2_sta, 4=plan_of_4_sta
			elif [ "$smd_ofdma_num_users" = "2" ]; then
				send_plan_off $interface_name
				send_plan_for_2_users $interface_name
				smd_active_plan=2 # active plan values: 0=no plan, 1=plan_of_1_sta, 2=plan_of_2_sta, 4=plan_of_4_sta
			elif [ "$smd_ofdma_num_users" = "4" ]; then
				send_plan_off $interface_name
				send_plan_for_4_users $interface_name
				smd_active_plan=4 # active plan values: 0=no plan, 1=plan_of_1_sta, 2=plan_of_2_sta, 4=plan_of_4_sta
			else
				error_print "SMD: Unsupported value - smd_ofdma_num_users:$smd_ofdma_num_users"
				return
			fi
		else
			send_plan_off $interface_name
			smd_ofdma_num_users=""
		fi
	# otherwise, send plan according num of connected sta.
	# avoid re-plan if the same plan is already active.
	else
		info_print "SMD: set plan using num_of_sta:$num_of_sta"
		if [ $num_of_sta -lt 1 ]; then
			info_print "SMD: do not set plan using num_of_sta (enabled only for 1, 2 or 4):$num_of_sta"
			send_plan_off $interface_name
		elif [ $num_of_sta = "1" ]; then
			if [ "$smd_active_plan" != "1" ]; then
				send_plan_off $interface_name
				send_plan_for_1_user $interface_name
				smd_active_plan=1 # active plan values: 0=no plan, 1=plan_of_1_sta, 2=plan_of_2_sta, 4=plan_of_4_sta
			fi
		elif [ "$num_of_sta" = "2" ] || [ "$num_of_sta" = "3" ]; then
			if [ "$smd_active_plan" != "2" ]; then
				send_plan_off $interface_name
				send_plan_for_2_users $interface_name
				smd_active_plan=2 # active plan values: 0=no plan, 1=plan_of_1_sta, 2=plan_of_2_sta, 4=plan_of_4_sta
			fi
		elif [ $num_of_sta -ge 4 ]; then
			if [ "$smd_active_plan" != "4" ]; then
				send_plan_off $interface_name
				send_plan_for_4_users $interface_name
				smd_active_plan=4 # active plan values: 0=no plan, 1=plan_of_1_sta, 2=plan_of_2_sta, 4=plan_of_4_sta
			fi
		fi
	fi
}

# parse input cmd line to daemon.
# e.g for cmd: OFDMA_STA_DIS,WLAN,interface_name,MAC,mac_address...
parse_command_line()
{
	local params_line orig_ifs param index
	params_line=$1

	[ -e sigmaManagerParams ] && rm sigmaManagerParams

	orig_ifs=$IFS
	IFS=,
	for param in $line
	do
		debug_print "SMD: paramN=$param"
		let index=$index+1
		if [ $(( $index % 2 )) -eq 1 ]; then
			cmd=$param
		else
			echo "smd_param_${cmd}"="$param" >> sigmaManagerParams
		fi
	done
	IFS=$orig_ifs

}

###############
# Daemon init #
###############
pipe=${SMDPIPE}

trap "rm -f $pipe" EXIT

if [ ! -p $pipe ]; then
	mknod $pipe p
fi

info_print "SMD: started pipe=${SMDPIPE}"

# in case of SMD init and STA were already connected,
# force plan sending per STAs on each I/F.
force_plan_sending "wlan0"
[ "$SINGLE_INTF_SUPPORT" = "0" ] && force_plan_sending "wlan2"

# init of global params
smd_ofdma_num_users=""

###############
# Daemon loop #
###############
while true
do
	if read line <$pipe; then
		info_print "SMD: got line: $line"
		command=${line%%,*}
		debug_print "SMD: got command: $command"
		if [ "$line" = "EXIT" ]; then
			send_plan_off wlan0
			[ "$SINGLE_INTF_SUPPORT" = "0" ] && send_plan_off wlan2
			break
		fi

		case "$command" in
			FAPI_SMD_OFDMA_STA_CON)
				# remove the command from the line, handle params only.
				line=${line##*$command,}
				parse_command_line $line
				source sigmaManagerParams
				info_print "SMD: FAPI_SMD_OFDMA_STA_CON wlan_interface=$smd_param_WLAN sta_mac=$smd_param_MAC"
				# make sure there is no running clish command
				check_before_send_command
				send_plan_per_nof_sta $smd_param_WLAN
			;;
			FAPI_SMD_OFDMA_STA_DIS)
				# remove the command from the line, handle params only.
				line=${line##*$command,}
				parse_command_line $line
				source sigmaManagerParams
				info_print "SMD: FAPI_SMD_OFDMA_STA_DIS wlan_interface=$smd_param_WLAN sta_mac=$smd_param_MAC"
				# make sure there is no running clish command
				check_before_send_command
				send_plan_per_nof_sta $smd_param_WLAN
			;;
			SIGMA_SMD_OFDMA_REPLAN)
				# remove the command from the line, handle params only.
				line=${line##*$command,}
				parse_command_line $line
				source sigmaManagerParams
				info_print "SMD: SIGMA_SMD_OFDMA_REPLAN wlan_interface=$smd_param_WLAN"
				# the current plan will be replaced by new plan with the new params.
				# make sure there is no running clish command
				check_before_send_command
				send_plan_per_nof_sta $smd_param_WLAN
			;;
			*)
				error_print "SMD: Unknown command - $command"
			;;
		esac
	fi
done

info_print "SMD: exiting"
