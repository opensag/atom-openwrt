#
# Openwrt Makefile for hostapd
#
# Simplify mode 1.0
#
#

#### Includes ###################################
include $(TOPDIR)/rules.mk
include $(INCLUDE_DIR)/kernel.mk

#### Package Info ###############################
# iwlwav-hostap branch: iwlwav_intel_ip_ax
PKG_NAME:=iwlwav-hostap
PKG_REV:=HEAD
PKG_VERSION:=2.6
PKG_RELEASE:=$(PKG_REV)

PKG_BASE_NAME:=$(PKG_NAME)
#PKG_SOURCE:=$(PKG_BASE_NAME)-$(PKG_VERSION).tar.bz2
#PKG_SOURCE_URL:=$(TOPDIR)/../iwlwav-hostap
#PKG_SOURCE_SUBDIR:=$(PKG_BASE_NAME)-$(PKG_VERSION)
#PKG_SOURCE_VERSION:=$(PKG_REV)
#PKG_SOURCE_PROTO:=git
#PKG_MD5SUM:=40b89c61036add0c2dd1fc10767d3b5f
 
stam:
	@echo the BUILD_VARIANT is
	@echo $(BUILD_VARIANT)

PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_BASE_NAME)-$(PKG_VERSION)

PKG_DEFAULT_CUSTOM_SOURCE_DIR:=$(shell pwd)

BIN_FOLDER:=bin
ifeq ($(CONFIG_NEW_FRAMEWORK),y)
 BIN_FOLDER:=opt/lantiq/bin
endif

ifeq ($(CONFIG_TARGET_ARCH_PACKAGES), "x86_64")
 BIN_FOLDER:=usr/sbin
endif

PKG_BUILD_DIR_HAPD:=$(PKG_BUILD_DIR)/hostapd
PKG_BUILD_DIR_WPAS:=$(PKG_BUILD_DIR)/wpa_supplicant

PKG_BUILD_DEPENDS:=libopenssl libnl3 librt libnl \
		   PACKAGE_ltq-wlan-wave_6x:ltq-wlan-wave_6x

PKG_CONFIG_DEPENDS:=\
	CONFIG_WIRELESS_STA

CONFIG_WIRELESS_STA:=y

include $(INCLUDE_DIR)/package.mk
#include $(INCLUDE_DIR)/package-version-override.mk

define Package/$(PKG_NAME)
  SECTION:=net
  CATEGORY:=Network
  TITLE:=hostapd for Wave
  DEPENDS:=+libopenssl +libnl-genl +librt +libnl
  MENU:=1
endef

define Package/$(PKG_NAME)/description
  IEEE 802.11 AP, IEEE 802.1X/WPA/WPA2/EAP/RADIUS Authenticator
endef

define Package/$(PKG_NAME)/config
$(call Package/$(PKG_NAME)/override_version)
$(call Package/$(PKG_NAME)/override_source_path)
	config WIRELESS_STA
	  depends on PACKAGE_$(PKG_NAME)
	  bool 'Linux WPA/WPA2/IEEE 802.1X Supplicant support'
	  help
	      Linux WPA/WPA2/IEEE 802.1X Supplicant
endef

#### Directory Paths ############################
OPENSSL_DIR:=$(wildcard $(BUILD_DIR)/openssl-*)
LIBNL_DIR:=$(wildcard $(BUILD_DIR)/libnl-3*)
LIBRT_DIR:=$(wildcard $(BUILD_DIR)/toolchain/ipkg-lantiq/librt)

IFX_CFLAGS += -I$(OPENSSL_DIR)/include -I$(LIBNL_DIR)/include
IFX_LDFLAGS += -L$(OPENSSL_DIR)/ -L$(LIBNL_DIR)/lib/.libs -lnl-3 -L$(LIBRT_DIR)/lib -lrt


#### Target Rules ###############################
define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) $(TOPDIR)/package/wav6/iwlwav-hostap/* $(PKG_BUILD_DIR)/
	$(Build/Patch)
endef


define Build/Compile
	$(MAKE) -C $(PKG_BUILD_DIR)/src clean
	$(CP) $(PKG_BUILD_DIR_HAPD)/defconfig $(PKG_BUILD_DIR_HAPD)/.config
	$(MAKE) $(TARGET_CONFIGURE_OPTS) -C $(PKG_BUILD_DIR_HAPD) \
		IFX_CFLAGS="$(IFX_CFLAGS)" IFX_LDFLAGS="$(IFX_LDFLAGS)"
	$(if $(CONFIG_WIRELESS_STA), \
		$(MAKE) -C $(PKG_BUILD_DIR)/src clean; \
		$(CP) $(PKG_BUILD_DIR_WPAS)/defconfig $(PKG_BUILD_DIR_WPAS)/.config; \
		$(MAKE) $(TARGET_CONFIGURE_OPTS) -C $(PKG_BUILD_DIR_WPAS) \
		    IFX_CFLAGS="$(IFX_CFLAGS)" IFX_LDFLAGS="$(IFX_LDFLAGS)" \
	)
endef

define Build/InstallDev
	$(INSTALL_DIR) $(1)/usr/include
	$(CP) $(PKG_BUILD_DIR)/src/common/wpa_ctrl.h $(1)/usr/include
endef

define Package/$(PKG_NAME)/install
	$(INSTALL_DIR) $(1)/$(BIN_FOLDER)
	$(INSTALL_DIR) -p $(1)/etc
	$(INSTALL_BIN) $(PKG_BUILD_DIR_HAPD)/hostapd $(1)/$(BIN_FOLDER)
	$(INSTALL_BIN) $(PKG_BUILD_DIR_HAPD)/hostapd_cli $(1)/$(BIN_FOLDER)
	$(INSTALL_BIN) $(PKG_BUILD_DIR_HAPD)/hostapd.eap_user $(1)/$(BIN_FOLDER)
	$(if $(CONFIG_WIRELESS_STA), \
		$(INSTALL_BIN) $(PKG_BUILD_DIR_WPAS)/wpa_supplicant $(1)/$(BIN_FOLDER); \
		$(INSTALL_BIN) $(PKG_BUILD_DIR_WPAS)/wpa_cli $(1)/$(BIN_FOLDER); \
		$(INSTALL_BIN) $(PKG_BUILD_DIR_WPAS)/wpa_passphrase $(1)/$(BIN_FOLDER) \
	)
	echo -e "iwlwav_hostap_hash=\"$(PKG_REV)\"\niwlwav_hostap_tag=\"$(PKG_REV)\"" > $(1)/etc/iwlwav_hostap.ver
endef

define Build/Clean
	if [ -d $(PKG_BUILD_DIR_HAPD) ]; then $(MAKE) -C $(PKG_BUILD_DIR_HAPD) clean; fi
	if [ -d $(PKG_BUILD_DIR_WPAS) ]; then $(MAKE) -C $(PKG_BUILD_DIR_WPAS) clean; fi
	if [ -d $(PKG_BUILD_DIR) ]; then $(RM) -r $(PKG_BUILD_DIR)/ipkg-$(BOARD); fi
endef

$(eval $(call BuildPackage,$(PKG_NAME)))
