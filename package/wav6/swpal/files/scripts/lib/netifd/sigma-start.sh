#!/bin/sh

listenPort=9000
procName=$0

# clean unused process
ownPid=$$
sigmaStartPid=`ps | grep "$0" | grep -v "grep" | grep -v "$ownPid" | awk '{ print $1 }'`
kill $sigmaStartPid

killall sigma-ap.sh

ncPid=`ps | grep "busybox nc -l -p $listenPort" | grep -v "grep" | awk '{ print $1 }'`
kill "$ncPid"

#power managment util
#pm_util -x 0

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

while [ `hostapd_cli status | sed -n 's/state=//p'` != "ENABLED" ]; do echo sigma-start.sh: Waiting for interface up; sleep 3; done
while true; do /nvram/busybox nc -l -p $listenPort < sigma-pipe  | "./sigma-ap.sh" > ./sigma-pipe; done &
