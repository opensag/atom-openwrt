ifneq ($(CROSS_COMPILE),)
CROSS-COMPILE:=$(CROSS_COMPILE)
endif
#CROSS-COMPILE:=/workspace/buildroot/buildroot-qemu_arm_vexpress_defconfig/output/host/usr/bin/arm-buildroot-linux-uclibcgnueabi-
#CROSS-COMPILE:=/workspace/buildroot-git/qemu_mips64_malta/output/host/usr/bin/mips-gnu-linux-
CC:=$(CROSS-COMPILE)gcc
LD:=$(CROSS-COMPILE)ld

release: clean qmi-proxy
	$(CC) -Wall -s QmiWwanCM.c GobiNetCM.c main.c MPQMUX.c QMIThread.c util.c udhcpc.c -o quectel-CM -lpthread -ldl

debug: clean
	$(CC) -Wall -g QmiWwanCM.c GobiNetCM.c main.c MPQMUX.c QMIThread.c util.c udhcpc.c -o quectel-CM -lpthread -ldl

qmi-proxy:
	$(CC) -Wall -s quectel-qmi-proxy.c  -o quectel-qmi-proxy -lpthread -ldl

clean:
	rm -rf quectel-CM *~
	rm -rf quectel-qmi-proxy
