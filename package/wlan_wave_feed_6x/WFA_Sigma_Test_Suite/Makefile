#
# Copyright (C) 2009-2012 OpenWrt.org
#
# This is free software, licensed under the GNU General Public License v2.
# See /LICENSE for more information.
#

include $(TOPDIR)/rules.mk
include $(INCLUDE_DIR)/kernel.mk

PKG_NAME:=WFA_Sigma_Test_Suite
PKG_VERSION:=1.1.1
PKG_RELEASE:=1
PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)
PKG_DEFAULT_CUSTOM_SOURCE_DIR:=$(shell pwd)/

include $(INCLUDE_DIR)/package.mk

define Package/$(PKG_NAME)
	SECTION:=base
	CATEGORY:=Network
	TITLE:=WFA_Sigma_Test_Suite
endef

define Package/$(PKG_NAME)/description
	WFA_Sigma_Test_Suite
endef

#### Target Rules ###############################
define Build/Prepare
	$(call Build/Prepare/Default)
	$(if $(CONFIG_$(PKG_NAME)_USE_CUSTOM_SOURCE_DIR),,$(CP) -L ./files/ $(PKG_BUILD_DIR)/)
endef

#define Build/Prepare
#	mkdir -p $(PKG_BUILD_DIR)
#	$(CP) ./src/* $(PKG_BUILD_DIR)/
#endef

define Build/Configure
endef

define Build/Compile
endef

define Package/$(PKG_NAME)/install
	$(INSTALL_DIR) $(1)/opt/lantiq/wave/scripts/
	$(INSTALL_BIN) ./files/sigma-ap.sh $(1)/opt/lantiq/wave/scripts/
	$(INSTALL_BIN) ./files/sigma-update.sh $(1)/opt/lantiq/wave/scripts/
	$(INSTALL_BIN) ./files/sigma-start.sh $(1)/opt/lantiq/wave/scripts/
	$(INSTALL_BIN) ./files/sigma-usbkey-update.sh $(1)/opt/lantiq/wave/scripts/
	$(INSTALL_BIN) ./files/sigmaManagerDaemon.sh $(1)/opt/lantiq/wave/scripts/
	$(INSTALL_BIN) ./files/sigma-common-lib.sh $(1)/opt/lantiq/wave/scripts/
endef

$(eval $(call BuildPackage,$(PKG_NAME)))
