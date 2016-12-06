#
# Copyright (C) 2006-2010 OpenWrt.org
#
# This is free software, licensed under the GNU General Public License v2.
# See /LICENSE for more information.
#

include $(TOPDIR)/rules.mk

PKG_NAME:=sysrepo-plugin-dt-status
PKG_VERSION:=1
PKG_RELEASE:=1

PKG_BUILD_DIR := $(BUILD_DIR)/$(PKG_NAME)

PKG_FIXUP:=libtool
PKG_INSTALL:=1

CMAKE_OPTIONS += -DCMAKE_C_FLAGS="-Wall -g --coverage"

include $(INCLUDE_DIR)/package.mk

include $(INCLUDE_DIR)/cmake.mk

define Package/sysrepo-plugin-dt-status
  SECTION:=net
  CATEGORY:=Network
	DEPENDS:=+libuci +libubus +libjson-c +libblobmsg-json +sysrepoctl +sysrepod
  TITLE:=sysrepo-plugin-dt-status
endef

define Package/sysrepo-plugin-dt-status/description
  sysrepo-plugin-dt-status program is a client for network configuration in a straightforward way
endef

##new from nvram example
define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) ./src/* $(PKG_BUILD_DIR)/
	mkdir -p $(1)/usr/lib/sysrepo/plugins
endef

define Package/sysrepo-plugin-dt-status/install
	$(INSTALL_DIR) $(1)/usr/lib/sysrepo/plugins
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/lib/libsysrepo-plugin-dt-status.so $(1)/usr/lib/sysrepo/plugins

	$(INSTALL_DIR) $(1)/etc/sysrepo/yang
	$(INSTALL_CONF) $(PKG_BUILD_DIR)/yang/status@2015-12-1.yang $(1)/etc/sysrepo/yang

	$(INSTALL_DIR) $(1)/etc/uci-defaults
	$(INSTALL_BIN) ./files/sysrepo-plugin-dt-status.default $(1)/etc/uci-defaults/sysrepo-plugin-dt-status
endef

$(eval $(call BuildPackage,sysrepo-plugin-dt-status))
