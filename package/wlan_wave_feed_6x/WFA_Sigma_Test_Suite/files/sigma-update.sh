#!/bin/sh

tftp_server_ip=$1
if [ "$tftp_server_ip" = "" ] ; then
    echo "Usage: /opt/lantiq/wave/scripts/sigma-update.sh <serverip>"
    exit
fi

tftp -gr sigma-ap.sh $tftp_server_ip
tftp -gr sigma-start.sh $tftp_server_ip
tftp -gr sigma-update.sh $tftp_server_ip
chmod +x sigma-ap.sh
chmod +x sigma-start.sh
chmod +x sigma-update.sh

./sigma-start.sh

