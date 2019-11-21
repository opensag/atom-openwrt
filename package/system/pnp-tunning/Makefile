#
# Copyright (C) 2019 intel.com
#
# This is free software, licensed under the GNU General Public License v2.
# See /LICENSE for more information.
#

include $(TOPDIR)/rules.mk
include $(INCLUDE_DIR)/kernel.mk
include $(INCLUDE_DIR)/package.mk

PKG_NAME:=pnp-tunning
PKG_VERSION:=1
PKG_RELEASE:=1

PKG_BUILD_DIR := $(BUILD_DIR)/$(PKG_NAME)
LDFLAGS:="-L$(STAGING_DIR)/usr/lib -L$(STAGING_DIR)/lib"

define Package/pnp-tunning
  SECTION:=utils
  CATEGORY:=Base system
  DEPENDS:=+pciutils +librt
  TITLE:=PnP tunning utils
endef

define Package/pnp-tunning/config
	#depends on PACKAGE_kernel
endef

define Package/pnp-tunning/description
 Utils to tune power and performance on x86 platform.
endef

define Build/Prepare
	$(RM) -rf $(PKG_BUILD_DIR)/*
	$(CP) -ar $(LINUX_DIR)/Makefile $(PKG_BUILD_DIR)/
	$(CP) -ar $(LINUX_DIR)/tools $(PKG_BUILD_DIR)/
endef

define Build/Configure
endef

define Build/Compile
	$(MAKE) -C $(PKG_BUILD_DIR)/tools cpupower CROSS=$(TARGET_CROSS)
	#$(MAKE) -C $(PKG_BUILD_DIR)/tools x86_energy_perf_policy CROSS=$(TARGET_CROSS)
endef

define Package/pnp-tunning/install
	$(INSTALL_DIR) $(1)/usr/lib
	$(INSTALL_DIR) $(1)/usr/bin
	$(CP) -a $(PKG_BUILD_DIR)/tools/power/cpupower/libcpupower.so* $(1)/usr/lib
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/tools/power/cpupower/cpupower $(1)/usr/bin
	#$(INSTALL_BIN) $(PKG_BUILD_DIR)/power/x86/x86_energy_perf_policy/x86_energy_perf_policy $(1)/usr/bin
endef

$(eval $(call BuildPackage,pnp-tunning))
