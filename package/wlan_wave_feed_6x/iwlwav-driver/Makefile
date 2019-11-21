#
# Openwrt Makefile for wave related SW element
#
# Simplify mode 1.0
#
#

#### Includes ###################################
include $(TOPDIR)/rules.mk
include $(INCLUDE_DIR)/kernel.mk

#### Package Info ###############################
# iwlwav-dev branch: master_6x
PKG_NAME:=iwlwav-driver
PKG_REV:=HEAD
PKG_VERSION:=6.1.0
PKG_RELEASE:=$(PKG_REV)

#PKG_SOURCE:=$(PKG_NAME)-$(PKG_VERSION)-$(PKG_REV).tar.bz2
#PKG_SOURCE_URL:=$(TOPDIR)/../iwlwav-dev
#PKG_SOURCE_SUBDIR:=$(PKG_NAME)-$(PKG_VERSION)
#PKG_SOURCE_VERSION:=$(PKG_REV)
#PKG_SOURCE_PROTO:=git

PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)
KERNEL_PKG_NAME:=iwlwav-driver-kernel
CONFIG_FOLDER:=../lq-wave-300_6x/files/config
#PKG_DEFAULT_CUSTOM_SOURCE_DIR:=$(shell pwd)

#SBIN_FOLDER:=usr/sbin

PKG_BUILD_DEPENDS:=kmod-directconnect-dp

CONFIG_FILE:=GRX350_GRX550_WAVE600_CONFIG
ifeq ($(CONFIG_TARGET_lantiq_xrx500_easy350_anywan_axepoint),y)
 CONFIG_FILE:=AXEPOINT_WAVE600_CONFIG
endif

include $(INCLUDE_DIR)/package.mk
#include $(INCLUDE_DIR)/package-version-override.mk

define Package/$(PKG_NAME)
  SECTION:=net
  CATEGORY:=Network
  TITLE:=Wave Driver
  DEPENDS:=+kmod-$(KERNEL_PKG_NAME)
  MENU:=1
endef

define Package/$(PKG_NAME)/description
  Wave driver package
endef

define KernelPackage/$(KERNEL_PKG_NAME)
  SUBMENU:=Lantiq
  TITLE:=Kernel Support for Wave WLAN
  DEPENDS:=+PACKAGE_kmod-directconnect-dp:kmod-directconnect-dp
  KCONFIG:= \
    CONFIG_CRYPTO_SHA256=y \
    CONFIG_FW_LOADER=y \
    CONFIG_HOTPLUG=y \
    CONFIG_WIRELESS=y \
    CONFIG_CFG80211=m \
    CONFIG_MAC80211=m \
    CONFIG_CRYPTO_CCM=y \
    CONFIG_CRYPTO_CMAC=y

  AUTOLOAD:=$(call AutoProbe,mtlk)
endef

define KernelPackage/$(KERNEL_PKG_NAME)/description
 Enable Wlan related options in Kernel
endef
#### Directory Paths ############################
#LIBNL_DIR:=$(wildcard $(BUILD_DIR)/libnl-3*)

#IFX_CFLAGS += -I$(LIBNL_DIR)/include
#IFX_LDFLAGS += -L$(LIBNL_DIR)/lib/.libs -lnl-3

#### Target Rules ###############################
# How to build driver
# BASE_PATH=/local/iserovic/backport_integ/ugw_sw/ugw/openwrt/core/
# CROSS_COMPILE_ARG=$BASE_PATH/staging_dir/toolchain-mips_mips32_gcc-4.8-linaro_uClibc-0.9.33.2/bin/mips-openwrt-linux-uclibc-
# ARCH_ARG=mips
# KLIB_BUILD_ARG=$BASE_PATH/build_dir/target-mips_mips32_uClibc-0.9.33.2_grx350_1600_mr_eth_rt_74/linux-lantiq_xrx500/linux-3.10.104
# KLIB_ARG=$BASE_PATH/build_dir/target-mips_mips32_uClibc-0.9.33.2_grx350_1600_mr_eth_rt_74/root-lantiq/lib/modules/3.10.104
# export STAGING_DIR=""

CROSS_COMPILE=$(if $(CONFIG_EXTERNAL_TOOLCHAIN),$(CONFIG_TOOLCHAIN_ROOT)/bin/$(CONFIG_TOOLCHAIN_PREFIX),$(TOOLCHAIN_DIR)/bin/$(TARGET_CROSS)) # fix for non external toolchain!


define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) $(TOPDIR)/package/wav6/iwlwav-dev/* $(PKG_BUILD_DIR)/
	$(Build/Patch)
endef


define Build/Compile
	@echo "Config files use: $(CONFIG_FILE)"
	$(SED) '1s/.*/BACKPORTS_VERSION="v$(LINUX_VERSION)"/' $(PKG_BUILD_DIR)/versions
	#$(CP) $(CONFIG_FOLDER)/$(CONFIG_FILE) $(PKG_BUILD_DIR)/defconfigs/iwlwav
	#$(MAKE) -C $(PKG_BUILD_DIR) defconfig-iwlwav KLIB_BUILD=$(LINUX_DIR) KLIB=$(TARGET_DIR)/$(TARGET_MODULES_DIR)
	$(MAKE) -C $(PKG_BUILD_DIR) defconfig-x86_64 KLIB_BUILD=$(LINUX_DIR) KLIB=$(TARGET_DIR)/$(TARGET_MODULES_DIR)
	$(MAKE) -C $(PKG_BUILD_DIR) CROSS_COMPILE=$(CROSS_COMPILE) ARCH=$(ARCH) KLIB_BUILD=$(LINUX_DIR) KLIB=$(TARGET_DIR)/$(TARGET_MODULES_DIR) UGW_DRIVER_VERSION=$(PKG_REV)
endef

# NOTE: driver build produces 5 KOs 
# compat.ko, cfg80211.ko, mac80211.ko – should be placed in /lib/modules/3.10.104/
# mtlk.ko, mtlkroot.ko – should be placed in /opt/lantiq/lib/modules/3.10.104/net/

define Package/$(PKG_NAME)/install
	$(INSTALL_DIR) $(1)/lib/modules/$(LINUX_VERSION)
	$(INSTALL_DIR) $(STAGING_DIR)/usr/include/
	$(INSTALL_DIR) -p $(1)/etc
	$(CP) $(PKG_BUILD_DIR)/compat/compat.ko $(1)/lib/modules/$(LINUX_VERSION)/
	$(CP) $(PKG_BUILD_DIR)/net/mac80211/mac80211.ko $(1)/lib/modules/$(LINUX_VERSION)/
	$(CP) $(PKG_BUILD_DIR)/net/wireless/cfg80211.ko $(1)/lib/modules/$(LINUX_VERSION)/
	$(CP) $(PKG_BUILD_DIR)/drivers/net/wireless/intel/iwlwav/mtlk.ko $(1)/lib/modules/$(LINUX_VERSION)/
	$(CP) $(PKG_BUILD_DIR)/drivers/net/wireless/intel/iwlwav/tools/mtlkroot/mtlkroot.ko $(1)/lib/modules/$(LINUX_VERSION)/
	$(CP) $(PKG_BUILD_DIR)/drivers/net/wireless/intel/iwlwav/wireless/shared/wave_fapi_nl_if.h $(STAGING_DIR)/usr/include/
	echo -e "iwlwav_driver_hash=\"$(PKG_REV)\"\niwlwav_driver_tag=\"$(PKG_REV)\"" > $(1)/etc/iwlwav_driver.ver
endef


$(eval $(call KernelPackage,$(KERNEL_PKG_NAME)))
$(eval $(call BuildPackage,$(PKG_NAME)))
