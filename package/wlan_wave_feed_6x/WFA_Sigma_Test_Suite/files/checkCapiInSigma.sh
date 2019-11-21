#!/bin/bash

DEBUG=0
[ "$DEBUG" = "1" ] && set -x
[ "$DEBUG" = "1" ] && set -o errtrace

debug_print()
{
	if [ "$DEBUG" = "1" ]; then
		echo "$*"
	fi
}

info_print()
{
	echo "$*"
}

check_capi_in_sigma()
{
	local capi_ucc_file
	capi_ucc_file=$1
	sigma_file=$2
	sigma_api=$3

	# parse the capi_ucc_file and copy the image with correct names
	while read -r line || [[ -n "$line" ]]
	do
		debug_print "THIS ONE NOW -> ${sigma_api}"
		retval=`echo $line | grep "AP1_control_agent!${sigma_api}"`
		if [ "$retval" != "" ]; then
			param="${line##*AP1_control_agent!${sigma_api}}"
			param="${param%%!DEFAULT*}"
			param="${param// /:}" # replace spaces with ':' as delimiters (this is currently relevant to command: Trig_ComInfo_SSAlloc_RA-RU)
			debug_print "${sigma_api}---->>>> $param"
			index=0
			for sigma_command in ${param//,/ }
			do
				let index=index+1
				if [ $(( $index % 2 )) -eq 1 ]; then
					debug_print "CMD: $sigma_api $sigma_command"
					retval=$(grep -i $sigma_command $sigma_file)
					if [ "$retval" = "" ]; then
						tmp_sigma_command=${sigma_command/"$"/}
						[ "$tmp_sigma_command" != "$sigma_command" ] && continue
						info_print "Command not exist:$test_num $sigma_command"
						test_num="${line%%\$AP1_control_agent*}" 
						echo "$test_num $sigma_command" >> ./check_results/${sigma_api}_tmp_not_supported_sigma_commands
					fi
				else
					debug_print "VAL: $sigma_command"
				fi
			done
		else
			debug_print "NO AP1_control_agent"
		fi
	done < $capi_ucc_file

	if [ -s ./check_results/${sigma_api}_tmp_not_supported_sigma_commands ]; then
		sort -u ./check_results/${sigma_api}_tmp_not_supported_sigma_commands >> ./check_results/${sigma_api}_not_supported_sigma_commands
	fi
	rm -f ./check_results/${sigma_api}_tmp_not_supported_sigma_commands
	if [ ! -s ./check_results/${sigma_api}_not_supported_sigma_commands ]; then
		rm -f ./check_results/${sigma_api}_not_supported_sigma_commands
		touch ./check_results/${sigma_api}_NO_CHANGE
	fi
}

[ "$DEBUG" = "0" ] && clear
echo "###############################################################################"
echo "####  check sigma script for supported commands (checking only ap_set_* for AP1_control_agent)"
echo "####  e.g.: ucc_path=/tmp2/${USER}/projects/repo/Wi-FiTestSuite/Wi-FiTestSuite/UCC/cmds/WTS-HE"
echo "####  e.g.: sigma_path=./sigma-ap.sh"
echo "###############################################################################"
rm -rf ./check_results
mkdir ./check_results
echo -n "Enter the path for /Wi-FiTestSuite/UCC/cmds [Press 'Enter' for default]: "
read ucc_path
[ -z "$ucc_path" ] && ucc_path=/tmp2/$USER/AX-PF-github/AX/Wi-FiTestSuite/UCC/cmds/WTS-HE/
echo "ucc_path=$ucc_path"
echo -n "Enter the path for sigma_ap.sh script [Press 'Enter' for default]: "
read sigma_path
[ -z "$sigma_path" ] && sigma_path=./sigma-ap.sh
echo "sigma_path=$sigma_path"
[ ! -f "$sigma_path" ] && echo "Error: $sigma_path not exist" && exit
echo "Press any key ..."
read p

sigma_apis="ap_set_wireless,ap_set_rfeature,ap_set_security,ap_set_staqos,ap_set_apqos,ap_set_11n_wireless,ap_set_wps_pbc,ap_set_radius,ap_set_hs2"

current_path=$PWD

if [ -n "$ucc_path" ]; then
	cd $ucc_path
	# search for 'AP1/2/3_control_agent' - AP as testbed
	grep -rs ap_set_ . | grep AP1_control_agent > ${current_path}/capi_ucc_all

	grep -rs ap_set_ . | grep AP2_control_agent >> ${current_path}/capi_ucc_all
	sed -i 's/AP2_control_agent/AP1_control_agent/g' ${current_path}/capi_ucc_all

	grep -rs ap_set_ . | grep AP3_control_agent >> ${current_path}/capi_ucc_all
	sed -i 's/AP3_control_agent/AP1_control_agent/g' ${current_path}/capi_ucc_all

	# search for 'wfa_control_agent_dut' - AP as DUT
	grep -rs "wfa_control_agent_dut!ap_set" >> ${current_path}/capi_ucc_all
	sed -i 's/wfa_control_agent_dut/AP1_control_agent/g' ${current_path}/capi_ucc_all

	# replace strings
	sed -i 's/!$Status1/!$DEFAULT/g' ${current_path}/capi_ucc_all
	sed -i 's/!ID,$Status1/!$DEFAULT/g' ${current_path}/capi_ucc_all
	cd -
else
	echo "#### using existing ./capi_ucc_all"
fi

cp -p ${current_path}/capi_ucc_all ./check_results

for sigma_api in ${sigma_apis//,/ }
do
	echo -e "Searching for $sigma_api commands ... \n"
	check_capi_in_sigma capi_ucc_all $sigma_path $sigma_api
	echo "done"
done

echo -n "Extract all param_not_supported commands from $sigma_path ... "
cat $sigma_path | grep -B2 -i "param not supported" | grep ')' > ./sigma_not_supported_tmp
rm -rf ./check_results/sigma_ap_not_supported_sigma_commands
# TODO: optimize the following code with sed
while read -r line || [[ -n "$line" ]]
do
	param_ns="${line%%)*}"
	echo "$param_ns" >> ./check_results/sigma_ap_not_supported_sigma_commands
done < ./sigma_not_supported_tmp
echo "done"

echo -n "Extract all do_nothing commands from $sigma_path ... "
cat $sigma_path | grep -B2 -i "# do nothing" | grep ')' > ./sigma_do_nothing_tmp
rm -rf ./check_results/sigma_ap_do_nothing_sigma_commands
# TODO: optimize the following code with sed
while read -r line || [[ -n "$line" ]]
do
	param_ns="${line%%)*}"
	echo "$param_ns" >> ./check_results/sigma_ap_do_nothing_sigma_commands
done < ./sigma_do_nothing_tmp
echo "done"
