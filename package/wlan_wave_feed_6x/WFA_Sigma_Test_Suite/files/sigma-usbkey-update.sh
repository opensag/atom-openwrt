#!/bin/sh
# TBD - make it generic.
home_dir="$PWD"

cd /

usbdisk_path=`find . -name Disc-1`
# usbdisk_path=`find . -name sda1`
#usbdisk_path=`cd ~; find . -name Disc-1; cd -`
#usbdisk_path=${usbdisk_path%%/opt/*}
usbdisk_path=${usbdisk_path##*.}
#usbdisk_path=`echo $usbdisk_path | tr -d ' '`

echo "################ start update ver:110817T1953###############"

cd $usbdisk_path
pwd
echo "### update scripts ####"
set -x
if [ -e ./update_scripts ]; then
	cd ./update_scripts
	list_sc=`ls`
	cd -
	cd /opt/lantiq/wave/scripts/
	cp -f ${usbdisk_path}/update_scripts/* .
	echo "### update scripts done ####"
	set +x
	for script in $list_sc;do chmod 755 $script; done
	cd -
else
set +x
echo "### no oce scripts folder to update ###"
fi

if [ -e ./update_images ]; then

	echo "### update FAPI/SL ####"
	set -x
	[ -e ./update_images/libfapiwlancommon.so ] && cp -f ./update_images/libfapiwlancommon.so /usr/lib/
	[ -e ./update_images/libwlan.so ] && cp -f ./update_images/libwlan.so /usr/lib/
	[ -e ./update_images/libfapiwave.so ] && cp -f ./update_images/libfapiwave.so /opt/lantiq/lib
	set +x 
	echo "### update FAPI/SL done ####"

	echo "### update hostapd ####"
	set -x
	[ -e ./update_images/hostapd ] && cp -f ./update_images/hostapd /opt/lantiq/bin/
	[ -e ./update_images/hostapd_cli ] && cp -f ./update_images/hostapd_cli /opt/lantiq/bin/
	set +x
	echo "### update hostapd done ####"

	echo "### update driver ####"
	set -x
	[ -e ./update_images/mtlk.ko ] && cp -f ./update_images/mtlk.ko /opt/lantiq/lib/modules/3.10.104/net/
	[ -e ./update_images/mtlkroot.ko ] && cp -f ./update_images/mtlkroot.ko /opt/lantiq/lib/modules/3.10.104/net/
	set +x
	echo "### update driver done####"
else
	echo "###  no oce images folder to update ###"
fi

cd -
pwd
sync
echo "### sync done ####"
cd "$home_dir"
/opt/lantiq/wave/scripts/sigma-start.sh
