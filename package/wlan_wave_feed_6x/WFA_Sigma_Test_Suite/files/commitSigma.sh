#!/bin/bash
param1=$1
param2=$2
stringToReplace=""
cd wlan_wave_feed/WFA_Sigma_Test_Suite/files/
stringToReplace=`sed -n '/CA_VERSION=\"/{p;q}' ./sigma-ap.sh`
echo "stringToReplace=$stringToReplace"
stringToReplace=${stringToReplace:28:2}
new_ver=$((stringToReplace + 1))
done=`sed -i "0,/$stringToReplace/s//$new_ver/" ./sigma-ap.sh`
cd -
git add .
if [ "$param1" = "-m" ] && [ "$param2" != "" ]; then
	git commit -m "$param2 Sigma-CAPI-10.2.$new_ver"
else
	git commit
fi
