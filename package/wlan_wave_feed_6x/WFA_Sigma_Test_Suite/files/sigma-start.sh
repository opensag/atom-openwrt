#!/bin/sh
# sigma-start.sh

# source for common and debug tools
[ ! "$SIGMA_COMMON_LIB_SOURCED" ] && . /opt/lantiq/wave/scripts/sigma-common-lib.sh

sigma_debugging=$1
stringToReplace=`sed -n '/export DEBUGGING=/{p;q}' /opt/lantiq/wave/scripts/sigma-common-lib.sh`
if [ "$sigma_debugging" = "DEBUGON" ]; then
	sed -i "s/$stringToReplace/export DEBUGGING=1/g" /opt/lantiq/wave/scripts/sigma-common-lib.sh
else
	sed -i "s/$stringToReplace/export DEBUGGING=0/g" /opt/lantiq/wave/scripts/sigma-common-lib.sh
fi

ps | awk '/sigma-run.sh/ {if ($0 !~ "awk") system("kill "$1)}'
ps | awk '/sigma-ap.sh/ {if ($0 !~ "awk") system("kill "$1)}'
killall nc

pm_util -x 0

dirname() {
	full=$1
	file=`basename $full`
	path=${full%%$file}
	[ -z "$path" ] && path=./
	echo $path
}
thispath=`dirname $0`

cp $thispath/sigma-ap.sh /tmp/
cd /tmp
[ ! -e sigma-pipe ] && mknod sigma-pipe p

# set the platform max interface name
[ "$SINGLE_INTF_SUPPORT" = "0" ] && intf_name=wlan2 || intf_name=wlan0

[ -e ${SMDPIPE} ] && echo "EXIT" > ${SMDPIPE}

# set certification to on - this flag is used in vendor
# In order to return to default system use: rm $SIGMA_CERTIFICATION_ON
[ ! -e $SIGMA_CERTIFICATION_ON ] && touch $SIGMA_CERTIFICATION_ON

cat > sigma-run.sh << EOF
while [ "\`hostapd_cli -i $intf_name status | sed -n 's/state=//p'\`" != "ENABLED" ]; do
	echo sigma-start.sh: Waiting for interface up ...
	sleep 3
done

while true; do nc -l -p 9000 < sigma-pipe | "./sigma-ap.sh" > ./sigma-pipe; done &
EOF
chmod +x sigma-run.sh
./sigma-run.sh&

#/usr/lib/fapi_wlan_beerock_cli HOSTAPD_RECV &
