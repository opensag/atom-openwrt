#
# Openwrt Makefile for Wireless tools
#
# Simplify mode 1.0
#
#

#### Includes ###################################
include $(TOPDIR)/rules.mk
include $(INCLUDE_DIR)/kernel.mk

#### Package Info ###############################
# iwlwav-tools branch: master_6x
PKG_NAME:=iwlwav-tools
PKG_REV:=HEAD
PKG_VERSION:=6.1.0
PKG_RELEASE:=1

PKG_BASE_NAME:=$(PKG_NAME)
#PKG_SOURCE:=$(PKG_BASE_NAME)-$(PKG_VERSION).tar.bz2
#PKG_SOURCE_URL:=$(TOPDIR)/../iwlwav-tools
#PKG_SOURCE_SUBDIR:=$(PKG_BASE_NAME)-$(PKG_VERSION)
#PKG_SOURCE_VERSION:=$(PKG_REV)
#PKG_SOURCE_PROTO:=git
 
PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_BASE_NAME)-$(PKG_VERSION)

PKG_BUILD_DEPENDS:=libnl3 librt pthread

include $(INCLUDE_DIR)/package.mk
#include $(INCLUDE_DIR)/package-version-override.mk

define Package/$(PKG_NAME)
  SECTION:=net
  CATEGORY:=Network
  TITLE:=wave SW toolset
  URL:=http://www.intel.com/
  DEPENDS:=+libnl-genl +libnl
  MENU:=1
endef

define Package/$(PKG_NAME)/description
  Lantiq toolset for wave HW
endef

OUTPUT_DEST=usr/sbin/

#### Toolchain defenitions ###############################
CROSS_COMPILE=$(if $(CONFIG_EXTERNAL_TOOLCHAIN),$(CONFIG_TOOLCHAIN_ROOT)/bin/$(CONFIG_TOOLCHAIN_PREFIX)gcc,$(TOOLCHAIN_DIR)/bin/$(TARGET_CROSS)gcc) # fix for non external toolchain!
CROSS_AR=$(if $(CONFIG_EXTERNAL_TOOLCHAIN),$(CONFIG_TOOLCHAIN_ROOT)/bin/$(CONFIG_TOOLCHAIN_PREFIX)ar,$(TOOLCHAIN_DIR)/bin/$(TARGET_CROSS)ar) # fix for non external toolchain!
CROSS_RANLIB=$(if $(CONFIG_EXTERNAL_TOOLCHAIN),$(CONFIG_TOOLCHAIN_ROOT)/bin/$(CONFIG_TOOLCHAIN_PREFIX)ranlib,$(TOOLCHAIN_DIR)/bin/$(TARGET_CROSS)ranlib) # fix for non external toolchain!
CROSS_LD=$(if $(CONFIG_EXTERNAL_TOOLCHAIN),$(CONFIG_TOOLCHAIN_ROOT)/bin/$(CONFIG_TOOLCHAIN_PREFIX)ld,$(TOOLCHAIN_DIR)/bin/$(TARGET_CROSS)ld) # fix for non external toolchain!

#### Libnl defenition ###############################
LIBNL_DIR:=$(wildcard $(STAGING_DIR)/usr/include/libnl3)

CFLAGS += -I$(LIBNL_DIR)
LDFLAGS += -L$(TOOLCHAIN_DIR)/lib -L$(STAGING_DIR)/usr/lib -lnl-genl-3 -lnl-3 -pthread -lpthread

#### Target Rules ###############################
define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) $(TOPDIR)/package/wav6/iwlwav-tools/* $(PKG_BUILD_DIR)/
	$(Build/Patch)
endef

define Build/Compile
	@echo LDFLAGS: "$(LDFLAGS)"
	$(MAKE) -C $(PKG_BUILD_DIR) clean
	$(MAKE) CC=$(CROSS_COMPILE) AR=$(CROSS_AR) RANLIB=$(CROSS_RANLIB) LD=$(CROSS_LD) -C $(PKG_BUILD_DIR) CFLAGS="$(CFLAGS)" LDFLAGS="$(LDFLAGS)"
endef

define Package/$(PKG_NAME)/install
	$(INSTALL_DIR) $(1)/$(OUTPUT_DEST)
	$(INSTALL_DIR) -p $(1)/etc
	$(CP) $(PKG_BUILD_DIR)/tools/dutserver/linux/dutserver $(1)/$(OUTPUT_DEST)
	$(CP) $(PKG_BUILD_DIR)/tools/rtlogger/logserver/linux/logserver $(1)/$(OUTPUT_DEST)
	$(CP) $(PKG_BUILD_DIR)/tools/mttools/drvhlpr/drvhlpr $(1)/$(OUTPUT_DEST)
	$(CP) $(PKG_BUILD_DIR)/tools/mttools/mtdump/mtdump $(1)/$(OUTPUT_DEST)
	$(CP) $(PKG_BUILD_DIR)/tools/mttools/mtdump/mtdump.mtidlc $(1)/$(OUTPUT_DEST)
	$(CP) $(PKG_BUILD_DIR)/tools/mttools/mtlk_cli/mtlk_cli $(1)/$(OUTPUT_DEST)
	$(CP) $(PKG_BUILD_DIR)/tools/mttools/mtlk_cli_dbg_srv/mtlk_cli_dbg_server $(1)/$(OUTPUT_DEST)
	$(CP) $(PKG_BUILD_DIR)/tools/BCLSockServer/BCLSockServer $(1)/$(OUTPUT_DEST)
	$(CP) $(PKG_BUILD_DIR)/tools/dump_handler/dump_handler $(1)/$(OUTPUT_DEST)
	echo -e "iwlwav_tools_hash=\"$(PKG_REV)\"\niwlwav_tools_tag=\"$(PKG_REV)\"" > $(1)/etc/iwlwav_tools.ver
endef

define Build/Clean
	if [ -d $(PKG_BUILD_DIR) ]; then $(RM) -r $(PKG_BUILD_DIR)/ipkg-$(BOARD); $(MAKE) -C $(PKG_BUILD_DIR) clean; fi
	
endef

$(eval $(call BuildPackage,$(PKG_NAME)))
