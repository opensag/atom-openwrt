#!/bin/sh

debug_infrastructure=/lib/netifd/debug_infrastructure.sh
if [ ! -f $debug_infrastructure ]; then
        exit
fi
. $debug_infrastructure

uci_indexes=`uci show | grep ifname | sed 's/.ifname/ /g' | awk '{print $1}'`

while :;
do
        eventMsg=`dwpal_cli -ihostap -vwlan0 -vwlan2 -l"AP-ENABLED" -l"INTERFACE_RECONNECTED_OK"`
        sleep 1

        for uci_index in $uci_indexes ; do
                interface=`uci get ${uci_index}.ifname`
                for i in $(seq 1 $number_of_debug_configs); do
                        debug_config=`uci get -q ${uci_index}.debug_iw_post_up_$i`
                        if [ -n "$debug_config" ]; then
                                eval "iw $interface iwlwav $debug_config"
                        fi
                done
        done
done
