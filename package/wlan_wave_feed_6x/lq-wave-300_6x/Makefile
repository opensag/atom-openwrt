#
# Openwrt Makefile for ltq-wlan-wave_6x
####################################

# Includes
include $(TOPDIR)/rules.mk
include $(INCLUDE_DIR)/kernel.mk

PKG_NAME:=ltq-wlan-wave_6x
PKG_VERSION:=06.01.00
PKG_REV:=HEAD


#package_source
#PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)
#PKG_SOURCE_VERSION:=$(PKG_REV)
#PKG_SOURCE_URL:=$(TOPDIR)/../wave_bin
#PKG_SOURCE_PROTO:=git
#PKG_SOURCE:=$(PKG_NAME)-$(PKG_VERSION)-$(PKG_REV).tar.gz
#PKG_SOURCE_SUBDIR:=$(PKG_NAME)-$(PKG_VERSION)

FW_BIN_FOLDER:=$(PKG_BUILD_DIR)/fw/bin
GENRISC_BIN_FOLDER:=$(PKG_BUILD_DIR)/genrisc/bin
PROGMODEL_BIN_FOLDER:=$(PKG_BUILD_DIR)/progmodel/bin
PSD_BIN_FOLDER:=$(PKG_BUILD_DIR)/psd/bin
REGULATORY_BIN_FOLDER:=$(PKG_BUILD_DIR)/regulatory/bin

BIN_FOLDER:=usr/bin
BINS_FOLDER:=lib/firmware
SBIN_FOLDER:=usr/sbin
include $(INCLUDE_DIR)/package.mk
#include $(INCLUDE_DIR)/package-version-override.mk


define Package/$(PKG_NAME)
	SECTION:=utils
	CATEGORY:=Lantiq
	TITLE:=Wave components
	DEPENDS:= \
		+PACKAGE_kmod-ltq_wave500_dp_drv:kmod-ltq_wave500_dp_drv \
		+PACKAGE_kmod-lantiq_directconnect_support:kmod-lantiq_directconnect_support \
		@+PACKAGE_ltq-devm:FEATURE_DEVM_WIFILAN \
		@+BUSYBOX_CONFIG_WC \
		@+BUSYBOX_CONFIG_WHICH \
		@+BUSYBOX_CONFIG_AWK \
		@+BUSYBOX_CONFIG_FEATURE_AWK_MATH \
		@+BUSYBOX_CONFIG_FIND \
		@+BUSYBOX_CONFIG_FEATURE_FIND_MTIME \
		@+BUSYBOX_CONFIG_FEATURE_FIND_PERM \
		@+BUSYBOX_CONFIG_FEATURE_FIND_TYPE \
		@+BUSYBOX_CONFIG_FEATURE_FIND_XDEV \
		@+BUSYBOX_CONFIG_FEATURE_FIND_NEWER \
		@+BUSYBOX_CONFIG_FEATURE_FIND_INUM \
		@+BUSYBOX_CONFIG_XARGS \
		@+PACKAGE_libnl3 \
		@+PACKAGE_libopenssl \
		@+PACKAGE_libuuid \
		@+PACKAGE_uuidgen \
		@+PACKAGE_udev \
		@+PACKAGE_dwpal_6x \
		@+PACKAGE_wav_api_6x \
		@+PACKAGE_iwlwav-hostap \
		@+PACKAGE_iwlwav-iw \
		@+PACKAGE_iwlwav-tools \
		@+PACKAGE_iwlwav-driver
	URL:=http://www.intel.com
	MAINTAINER:=Intel
	MENU:=1
endef

define Package/$(PKG_NAME)/description
 This package contains wave binaries components
endef

#### Target Rules ##########################
define Package/$(PKG_NAME)/config
	menu "$(PKG_NAME) features"
	depends on PACKAGE_$(PKG_NAME)
        source "$(SOURCE)/config/Config.in"
        endmenu
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) $(TOPDIR)/package/wav6/wave_bin/* $(PKG_BUILD_DIR)/
	#$(call Build/Prepare/Default)

	### WA to apply FW patch since openwrt infrastructure does not support binary patch
	###### 	(cd $(PKG_BUILD_DIR); git init .;  git add .; git commit -m "init fw repo for applying patch"; git am $(TOPDIR)/../patch/wave_bin/*.patch)
endef

define Build/Compile
endef

define Download/$(PKG_NAME)
endef

define Package/$(PKG_NAME)/install
	$(INSTALL_DIR) -p $(1)/$(BIN_FOLDER)/
	$(INSTALL_DIR) -p $(1)/$(SBIN_FOLDER)/
	$(INSTALL_DIR) -p $(1)/$(BINS_FOLDER)/
	$(INSTALL_DIR) -p $(1)/etc/hotplug/
	$(INSTALL_DIR) -p $(1)/etc/hotplug.d/
	$(INSTALL_DIR) -p $(1)/etc/udev/rules.d/
	$(INSTALL_DIR) -p $(1)/lib/firmware/
	$(LN) /dev/null $(1)/etc/udev/rules.d/75-persistent-net-generator.rules
	$(CP) $(FW_BIN_FOLDER)/* $(1)/$(BINS_FOLDER)/
	$(CP) $(FW_BIN_FOLDER)/fw_scd_file.scd $(1)/$(BIN_FOLDER)/
	$(CP) $(GENRISC_BIN_FOLDER)/* $(1)/$(BINS_FOLDER)/
	$(CP) $(PROGMODEL_BIN_FOLDER)/* $(1)/$(BINS_FOLDER)/
	$(CP) $(PSD_BIN_FOLDER)/* $(1)/$(BINS_FOLDER)/
	$(CP) $(REGULATORY_BIN_FOLDER)/* $(1)/lib/firmware/
	$(CP) ./files/binaries/sbin/* $(1)/$(SBIN_FOLDER)/
	$(CP) ./files/etc/* $(1)/etc/
	$(CP) $(PKG_BUILD_DIR)/wave_components.ver $(1)/etc/
endef

$(eval $(call BuildPackage,$(PKG_NAME)))
