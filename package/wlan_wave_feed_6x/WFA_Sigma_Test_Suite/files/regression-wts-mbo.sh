# Copy script to Windows server running UCC, in /c/UCC_6_3/bin
# Edit configurable parameters so that UCC is pointed properly - full or relative path in Unix convention
# Edit AllInitConfig_MBO-AP.txt and AllInitConfig_MBO-STA.txt  (IPs etc)
# Run in git-bash: /c/WFA_Sigma_test_Suite/regression-wts-mbo.sh


###########################
## Configurable parameters

source /c/WFA_Sigma_Test_Suite/files/vendors_list.conf

# Special handling e.g. when uploading to TMS: break on first failed test, so that not everything turns RED
break_on_fail=0

# Set UCC_path to the path to WFA test suite root folder
#UCC_path=..
UCC_path=/c/MBO-PF/UCC

# Set tests_path to path under UCC_path that contains the certification tests. For MBO: cmds/WTS-MBO
tests_path=$UCC_path/cmds/WTS-MBO

# Tests to run - default all found in WTS-MBO folder, or replace with specific list
test_list=""

# Tests to skip  (already passed or known problematic)
skipped_tests=""

# Network mode - WAN/LAN: Select WAN to configure separate networks for control and test, use LAN for single network for control+test.
# WFA convention is 192.165.100.xx for test network, 192.168.250.xx for control network
network_mode=LAN

# Setup number: 1 or 2. Change this to allow multiple setups connected to the same control subnet
setup_number=1

###########################

if [ -z "$test_list" ]
then
	eval test_list=\$${1}_test_list
fi

if [ -z "$skipped_tests" ]
then
	eval skipped_tests=\$${1}_skipped_tests
fi

eval sta_ip=\$${1}_sta_ip

#########################

REGRESSION_PATH=`dirname $0`
CONFIG_TMS=$REGRESSION_PATH/TmsClient.conf
CONFIG_CURRENT=$tests_path/AllInitConfig_MBO.txt

if [ "$1" != "" ]
then
	regression_name=${1}_
fi
OUTFILE=${REGRESSION_PATH}/logs/regression_${regression_name}`date +%Y%m%d_%H%M`.log
echo "==== Saving regression logs to: $OUTFILE ===="
mkdir -p ${REGRESSION_PATH}/logs


#####################################
# Definitions for formatted printing - use with echo -e
RED='\033[0;31m'
YELLOW='\033[1;33m'
GREEN='\033[0;32m'
CYAN='\033[0;36m'
NOCOLOR='\033[0m' 
#####################################


# Configuration that must be done for all tests
config_allinit() {
	# Setup WTS configuration files for APUT or STAUT
	# Use APUT or STAUT configurations, based on test name
	# Pass the setup number to set different IPs automatically
	if [ "${testname:4:1}" == "4" ]; then
		# AP test
		$REGRESSION_PATH/AllInitConfig_MBO_modify.sh $CONFIG_CURRENT $network_mode AP $setup_number $sta_ip
	else
		# STA test
		$REGRESSION_PATH/AllInitConfig_MBO_modify.sh $CONFIG_CURRENT $network_mode STA $setup_number $sta_ip
	fi
	# TODO: Don't copy TMS, sed like for all init.
	cp $CONFIG_TMS $UCC_path/config/
	
	# Reset skipme flag - will be set if this test is in skipme list
	skipme=0
}

# Special configuration that must be done per test name
config_special() {
	#for test 5.2.8 need to change DUTInfo.txt - WNM notifications to 1
	if [ "$testname" = "MBO-5.2.8" ] || [ "$testname" = "MBO-5.2.9" ]
	then
		sed -i 's/define!$Send_WNM_Notification_Request!0!/define!$Send_WNM_Notification_Request!1!/' $tests_path/DUTInfo.txt
	else
		sed -i 's/define!$Send_WNM_Notification_Request!1!/define!$Send_WNM_Notification_Request!0!/' $tests_path/DUTInfo.txt
	fi
	
	if [ "$testname" = "MBO-4.2.6E" ] || [ "$testname" = "MBO-5.2.6E" ]
	then
		sed -i 's/DUTType!WPA2-Personal!/DUTType!WPA2-Enterprise!/' $tests_path/DUTInfo.txt
	else
		sed -i 's/DUTType!WPA2-Enterprise!/DUTType!WPA2-Personal!/' $tests_path/DUTInfo.txt
	fi
	

	# Special handling for 4.2.3/5.2.3 tests - rename to _5G (the _24G versions must be added manually to the test list)
	if [ "$testname" = "MBO-4.2.3" ] || [ "$testname" = "MBO-5.2.3" ]
	then
		testname="${testname}_5G"
	fi
	
	# Skip tests in preconfigured (or TODO: already passed) list
	for skiptest in $skipped_tests; do
		if [ "$testname" = "$skiptest" ]
		then
			skipme=1
			break
		fi
	done
}

### UCC preparation - make sure we are on the latest commit from WFA
cd $UCC_path
git reset --hard && git pull
if [ $? != 0 ]
then
	echo -e $RED ERROR!!! git pull from WFA failed. Please check connectivity. 
	echo -e $YELLOW Continuing testing with existing UCC scripts. $NOCOLOR
fi
cd - > /dev/null



### Main loop
prev_fail_count=0
break_test=0
skipme=0
failed_list=""
passed_list=""
for f in `eval echo $test_list`
do
	[ $break_test -eq 1 ] && break
	testname="${f##$tests_path/}"
	testname=${testname%.txt}
	
	config_allinit
	config_special
	
	echo -e $CYAN "\n====== `date +%H:%M:%S` $testname ======" $NOCOLOR 1>&2
	echo -e "\n====== `date +%H:%M:%S` $testname ======"
	
	if [ "$skipme" = "1" ]; then
		echo Skipping test 1>&2
		echo Skipping test
		continue
	fi
	
	# wts.exe must be run from the UCC/bin folder
	cd $UCC_path/bin
	./wts.exe -p MBO -t $testname 2>&1
	cd - > /dev/null
	
	test_count=`grep -c "FINAL TEST RESULT" $OUTFILE`
	fail_count=`grep -c "FINAL TEST RESULT  --->            FAIL" $OUTFILE`
	if [ $prev_fail_count != $fail_count ]; then
		echo -e $RED "==== Test $testname FAILED ==== " $NOCOLOR 1>&2
		failed_list="$failed_list $testname"
		if [ "$break_on_fail" = "1" ]; then
			break_test=1
		fi
	else 
		# TODO: Tests that have no FINAL TEST RESULT will appear in the passed list --> explicitly grep for passed string
		passed_list="$passed_list $testname"
	fi 
	prev_fail_count=$fail_count
	let pass_count=$test_count-$fail_count
	echo "===== Pass count: $pass_count / $test_count ===== " 1>&2
	echo "===== Pass count: $pass_count / $test_count ===== "
done >> $OUTFILE


### Regression summary
echo "============ Regression done. Final Pass count: $pass_count / $test_count ============ " >> $OUTFILE
echo "============ Regression done. Final Pass count: $pass_count / $test_count ============ "
[ $break_test -eq 1 ] && echo "Regression terminated before all tests executed"
echo "failed_list=\"$failed_list\" " >> $OUTFILE
echo "failed_list=\"$failed_list\" "
echo "passed_list=\"$passed_list\" " >> $OUTFILE
echo "passed_list=\"$passed_list\" "
echo "  Details in $OUTFILE  "

