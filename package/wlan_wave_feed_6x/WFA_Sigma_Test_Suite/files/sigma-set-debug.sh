#!/bin/sh
# sigma-set-debug.sh

# source for common and debug tools
[ ! "$SIGMA_COMMON_LIB_SOURCED" ] && . /opt/lantiq/wave/scripts/sigma-common-lib.sh

if [ "$1" = "on" ]; then
	echo "Sigma debug on"
	touch $DEBUGGING_FILE
elif [ "$1" = "off" ]; then
	echo "Sigma debug off"
	rm -f $DEBUGGING_FILE
else
	echo "Usage: $0 [on|off]"
fi
