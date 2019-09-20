#!/bin/sh

tftp_server_ip=$1
if [ "$tftp_server_ip" = "" ] ; then
    echo "Usage: /opt/lantiq/wave/scripts/fapibeerock-update.sh <serverip>"
    exit
fi

killall fapi_wlan_beerock_cli
rm /usr/lib/Bee*
cd /usr/lib
tftp -gr libfapiwlancommon.so $tftp_server_ip
tftp -gr fapi_wlan_beerock_cli $tftp_server_ip
chmod +x fapi_wlan_beerock_cli
cd -
cd /opt/lantiq/lib
tftp -gr libfapiwave.so $tftp_server_ip
cd -
