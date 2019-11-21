#
# Openwrt Makefile for mac80211 scripts which are based on openwrt
#

#### Includes ###################################
include $(TOPDIR)/rules.mk

#### Package Info ###############################
PKG_NAME:=swpal
PKG_REV:=HEAD
PKG_VERSION:=ER4.31
PKG_RELEASE:=$(PKG_REV)
PKG_BASE_NAME:=$(PKG_NAME)
#PKG_SOURCE:=$(PKG_BASE_NAME)-$(PKG_VERSION).tar.bz2
#PKG_SOURCE_URL:=$(TOPDIR)/../swpal
#PKG_SOURCE_SUBDIR:=$(PKG_BASE_NAME)-$(PKG_VERSION)
#PKG_SOURCE_VERSION:=$(PKG_REV)
#PKG_SOURCE_PROTO:=git

include $(INCLUDE_DIR)/package.mk
define Package/$(PKG_NAME)
  SECTION:=net
  CATEGORY:=Lantiq
  TITLE:=swpal
  MAINTAINER:=Intel Corporation
endef

define Package/$(PKG_NAME)/description
  cfg80211 is the Linux wireless LAN (802.11) configuration API.
endef

### Menuconfig ##################################
define Package/$(PKG_NAME)/config
$(call Package/$(PKG_NAME)/override_version)
$(call Package/$(PKG_NAME)/override_source_path)
endef

#### Target Rules ###############################
define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) $(TOPDIR)/package/wav6/swpal/* $(PKG_BUILD_DIR)/
	$(Build/Patch)
endef

define Build/Compile
endef

# To ipkg / Rootfs
define Package/$(PKG_NAME)/install
	$(INSTALL_DIR) $(1)/etc/wave
	$(INSTALL_DIR) $(1)/lib
	$(INSTALL_DIR) $(1)/sbin
	$(INSTALL_DIR) $(1)/etc/hotplug.d/ieee80211
	$(CP) -ar $(PKG_BUILD_DIR)/files/db/etc/wave/db $(1)/etc/wave/
	$(CP) -ar $(PKG_BUILD_DIR)/files/scripts/lib/* $(1)/lib/
	$(CP) -ar $(PKG_BUILD_DIR)/files/scripts/etc/wave/scripts $(1)/etc/wave/
	$(RM) -f $(1)/lib/netifd/debug_infrastructure.sh
	$(RM) -f $(1)/lib/netifd/debug_post_iw_up.sh
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/files/scripts/lib/netifd/wireless/mac80211.sh $(1)/lib/netifd/wireless/mac80211.sh
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/files/scripts/sbin/wifi $(1)/sbin/wifi
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/files/scripts/etc/hotplug.d/ieee80211/mac80211.hotplug $(1)/etc/hotplug.d/ieee80211/10-wifi-detect
endef

define Build/Clean
endef

define Build/InstallDev
endef

$(eval $(call BuildPackage,$(PKG_NAME)))
