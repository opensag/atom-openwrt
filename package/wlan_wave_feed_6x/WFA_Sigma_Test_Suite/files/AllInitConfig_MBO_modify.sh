##########################################################################
# Configure test bed IPs based on whether this is AP or STA test, 
# and whether there are one or two networks for control and test
##########################################################################

if [ -z "$1" ]
then
	echo Usage: $0 '<AllInitConfig_MBO.txt path> [WAN|LAN] [AP|STA] [<setup number>] [STA IP OFFSET]'
	exit
fi
#FILENAME="AllInitConfig_MBO.txt"
FILENAME=$1
network_mode=$2
ap_sta=$3
setup_number=$4
sta_ip=$5

if [ -z "$sta_ip" ]
then
	sta_ip=2
fi

if [ "$network_mode" = "LAN" ]
then
	# LAN mode configuration (use the same IPs/subnets for IP and WIRELESS_IP):
	SUBNET_TEST=192.168.1
	SUBNET_CTRL=$SUBNET_TEST
else
	# WAN mode configuration (use different IPs/subnets for IP and WIRELESS_IP, i.e. for control and test networks):
	SUBNET_TEST=192.168.1
	SUBNET_CTRL=192.168.250
fi

baseip=0
if [ "$setup_number" = "2" ]
then
	baseip=200
fi

##################################################################
# IPs for the devices - edit per setup
# Preferably don't change unless a vendor needs hardcoded values.
##################################################################
DUT_NAME=IntelAP
AP1_IP=$SUBNET_CTRL.$((baseip + 1))
AP2_IP=$SUBNET_CTRL.$((baseip + 3))
AP3_IP=$SUBNET_CTRL.$((baseip + 4))
AP1_WIRELESS_IP=$SUBNET_TEST.$((baseip + 1))
AP2_WIRELESS_IP=$SUBNET_TEST.$((baseip + 3))
AP3_WIRELESS_IP=$SUBNET_TEST.$((baseip + 4))
AP_PORT=9000
STA1_IP=$SUBNET_CTRL.$((baseip + ${sta_ip}))
STA1_WIRELESS_IP=$SUBNET_TEST.$((baseip + 38))
STA1_PORT=9000
SNIFFER_IP=$SUBNET_CTRL.$((baseip + 10))
SNIFFER_PORT=9999
SNIFFER_CAPTURE_PORT=9905
TG_IP=$SNIFFER_IP
TG_WIRELESS_IP=$SUBNET_TEST.$((baseip + 10))
TG_CONSOLE_PORT=9003
# RADIUS server is shared for all test setups, so no need to calculate IP from baseip
RADIUS_IP=$SUBNET_TEST.40
RADIUS_PORT=1817
RADIUS_SECRET=1234567890123456789012345678901234567890123456789012345678901234
RADIUS_LOGIN_USERNAME=mbohost

######################################
# Select IPs based on AP or STA tests
######################################
if [ "$ap_sta" != "STA" ]
then
	DUT_IP=$AP1_IP
	DUT_PORT=$AP_PORT
	DUT_WIRELESS_IP=$AP1_WIRELESS_IP
	AP1MBO_IP=$AP2_IP
	AP1MBO_WIRELESS_IP=$AP2_WIRELESS_IP
	AP2MBO_IP=$AP3_IP
	AP2MBO_WIRELESS_IP=$AP3_WIRELESS_IP
	AP3MBO_IP="UNUSED"
	AP3MBO_WIRELESS_IP="UNUSED"
else
	DUT_IP=$STA1_IP
	DUT_PORT=$STA1_PORT
	DUT_WIRELESS_IP=$STA1_WIRELESS_IP
	AP1MBO_IP=$AP1_IP
	AP1MBO_WIRELESS_IP=$AP1_WIRELESS_IP
	AP2MBO_IP=$AP2_IP
	AP2MBO_WIRELESS_IP=$AP2_WIRELESS_IP
	AP3MBO_IP=$AP3_IP
	AP3MBO_WIRELESS_IP=$AP3_WIRELESS_IP
fi

######################################
# Comment or uncomment lines based on AP/STA mode
######################################
if [ "$ap_sta" != "STA" ]
then
	# Strip comments on testbed STA configs
	# Add comments to testbed AP3 configs
	sed -i \
	-e 's/^#\(sta1mbo_sta_wireless_ip!.*\)/\1/' \
	-e 's/^#\(wfa_control_agent_sta1mbo_sta!.*\)/\1/' \
	-e 's/^wfa_control_agent_ap3mbo_ap!.*/#&/' \
	-e 's/^define!$ap3mbo_ap_wireless_ip!.*/#&/' \
	$FILENAME
else
	# Comment out testbed STA configs
	# Strip comments from testbed AP3 configs
	sed -i \
	-e 's/^sta1mbo_sta_wireless_ip!.*/#&/' \
	-e 's/^wfa_control_agent_sta1mbo_sta!.*/#&/' \
	-e 's/^#\(wfa_control_agent_ap3mbo_ap!.*\)/\1/' \
	-e 's/^#\(define!$ap3mbo_ap_wireless_ip!.*\)/\1/' \
	$FILENAME
fi


######################################
# Set values in output file
######################################
sed -i \
-e 's/^wfa_control_agent_dut!.*/wfa_control_agent_dut!ipaddr='$DUT_IP',port='$DUT_PORT'!/' \
-e 's/^define!$DUT_wireless_ip!.*/define!$DUT_wireless_ip!'$DUT_WIRELESS_IP'!/' \
-e 's/^define!$DUT_Name!.*/define!$DUT_Name!'$DUT_NAME'!/' \
-e 's/^wfa_control_agent_sta1mbo_sta!.*/wfa_control_agent_sta1mbo_sta!ipaddr='$STA1_IP',port='$STA1_PORT'!/' \
-e 's/^sta1mbo_sta_wireless_ip!.*/sta1mbo_sta_wireless_ip!'$STA1_WIRELESS_IP'!/' \
-e 's/^wfa_control_agent_ap1mbo_ap!.*/wfa_control_agent_ap1mbo_ap!ipaddr='$AP1MBO_IP',port='$AP_PORT'!/' \
-e 's/^wfa_control_agent_ap2mbo_ap!.*/wfa_control_agent_ap2mbo_ap!ipaddr='$AP2MBO_IP',port='$AP_PORT'!/' \
-e 's/^wfa_control_agent_ap3mbo_ap!.*/wfa_control_agent_ap3mbo_ap!ipaddr='$AP3MBO_IP',port='$AP_PORT'!/' \
-e 's/^define!$ap1mbo_ap_wireless_ip!.*/define!$ap1mbo_ap_wireless_ip!'$AP1MBO_WIRELESS_IP'!/' \
-e 's/^define!$ap2mbo_ap_wireless_ip!.*/define!$ap2mbo_ap_wireless_ip!'$AP2MBO_WIRELESS_IP'!/' \
-e 's/^define!$ap3mbo_ap_wireless_ip!.*/define!$ap3mbo_ap_wireless_ip!'$AP3MBO_WIRELESS_IP'!/' \
-e 's/^wfa_sniffer!.*/wfa_sniffer!ipaddr='$SNIFFER_IP',port='$SNIFFER_PORT'!/' \
-e 's/^wfa_control_agent_capture_sta!.*/wfa_control_agent_capture_sta!ipaddr='$SNIFFER_IP',port='$SNIFFER_CAPTURE_PORT'!/' \
-e 's/^sniffer_enable!.*/sniffer_enable!1!/' \
-e 's/^define!$AP1_support_multi_BSSIDs!.*/define!$AP1_support_multi_BSSIDs!1!/' \
-e 's/^define!$AP2_support_multi_BSSIDs!.*/define!$AP2_support_multi_BSSIDs!1!/' \
-e 's/^wfa_console_ctrl!.*/wfa_console_ctrl!ipaddr='$TG_IP',port='$TG_CONSOLE_PORT'!/' \
-e 's/^wfa_console_tg!.*/wfa_console_tg!'$TG_WIRELESS_IP'!/' \
-e 's/^HostAPDIPAddress!.*/HostAPDIPAddress!'$RADIUS_IP'!/' \
-e 's/^HostAPDPort!.*/HostAPDPort!'$RADIUS_PORT'!/' \
-e 's/^HostAPDSharedSecret!.*/HostAPDSharedSecret!'$RADIUS_SECRET'!/' \
-e 's/^define!$serloginusr!.*/define!$serloginusr!'$RADIUS_LOGIN_USERNAME'!/' \
$FILENAME

# Workaround because WTS requires Windows newlines when doing validation
unix2dos $FILENAME
