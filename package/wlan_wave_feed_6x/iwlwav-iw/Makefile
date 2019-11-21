#
# Openwrt Makefile for iw
#
# Simplify mode 1.0
#
#

#### Includes ###################################
include $(TOPDIR)/rules.mk
include $(INCLUDE_DIR)/kernel.mk

#### Package Info ###############################
# iwlwav-iw branch: master
PKG_NAME:=iwlwav-iw
PKG_REV:=HEAD
PKG_VERSION:=4.14
PKG_RELEASE:=$(PKG_REV)

PKG_BASE_NAME:=$(PKG_NAME)
#PKG_SOURCE:=$(PKG_BASE_NAME)-$(PKG_VERSION).tar.bz2
#PKG_SOURCE_URL:=$(TOPDIR)/../iwlwav-iw
#PKG_SOURCE_SUBDIR:=$(PKG_BASE_NAME)-$(PKG_VERSION)
#PKG_SOURCE_VERSION:=$(PKG_REV)
#PKG_SOURCE_PROTO:=git
#PKG_MD5SUM:=40b89c61036add0c2dd1fc10767d3b5f

PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_BASE_NAME)-$(PKG_VERSION)

PKG_DEFAULT_CUSTOM_SOURCE_DIR:=$(shell pwd)

SBIN_FOLDER:=usr/sbin

PKG_BUILD_DEPENDS:=libnl3 PACKAGE_iw:iw \
		   PACKAGE_ltq-wlan-wave_6x:ltq-wlan-wave_6x

include $(INCLUDE_DIR)/package.mk
#include $(INCLUDE_DIR)/package-version-override.mk

define Package/$(PKG_NAME)
  SECTION:=net
  CATEGORY:=Network
  TITLE:=iw for Wave
  DEPENDS:=+libnl-genl +libnl
  MENU:=1
endef

define Package/$(PKG_NAME)/description
  iw for Wave
endef

#### Directory Paths ############################
LIBNL_DIR:=$(wildcard $(BUILD_DIR)/libnl-3*)

IFX_CFLAGS += -I$(LIBNL_DIR)/include
IFX_LDFLAGS += -L$(LIBNL_DIR)/lib/.libs -lnl-3


#### Target Rules ###############################
define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) $(TOPDIR)/package/wav6/iwlwav-iw/* $(PKG_BUILD_DIR)/
	$(Build/Patch)
endef


define Build/Compile
	$(MAKE) -C $(PKG_BUILD_DIR) clean
	$(MAKE) $(TARGET_CONFIGURE_OPTS) -C $(PKG_BUILD_DIR) \
		IFX_CFLAGS="$(IFX_CFLAGS)" IFX_LDFLAGS="$(IFX_LDFLAGS)"
endef

define Package/$(PKG_NAME)/install
	$(INSTALL_DIR) $(1)/$(SBIN_FOLDER)
	$(INSTALL_DIR) -p $(1)/etc
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/iw $(1)/$(SBIN_FOLDER)/
	echo -e "iwlwav_iw_hash=\"$(PKG_REV)\"\niwlwav_iw_tag=\"$(PKG_REV)\"" > $(1)/etc/iwlwav_iw.ver
endef

define Build/Clean
	if [ -d $(PKG_BUILD_DIR) ]; then $(RM) -r $(PKG_BUILD_DIR)/ipkg-$(BOARD); fi
endef

$(eval $(call BuildPackage,$(PKG_NAME)))
