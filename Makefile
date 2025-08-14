include $(TOPDIR)/rules.mk
include $(INCLUDE_DIR)/kernel.mk

PKG_NAME:=rtl837x_gsw
PKG_VERSION:=0.0.1
PKG_RELEASE:=1
PKG_MAINTAINER:=air jinkela (air_jinkela@163.com)

include $(INCLUDE_DIR)/package.mk

define KernelPackage/$(PKG_NAME)
	SUBMENU:=Other modules
	TITLE:=$(PKG_NAME)
	FILES:=$(PKG_BUILD_DIR)/rtl837x_gsw.ko
	AUTOLOAD:=$(call AutoLoad, 99, rtl837x_gsw)
	DEPENDS:=+kmod-swconfig
endef

EXTRA_KCONFIG:= \
	CONFIG_RTL837x_GSW=m

EXTRA_CFLAGS:= \
	$(patsubst CONFIG_%, -DCONFIG_%=1, $(patsubst %=m,%,$(filter %=m,$(EXTRA_KCONFIG)))) \
	$(patsubst CONFIG_%, -DCONFIG_%=1, $(patsubst %=y,%,$(filter %=y,$(EXTRA_KCONFIG)))) \
	-DVERSION=$(PKG_RELEASE) \
	-I$(PKG_BUILD_DIR)/include \

MAKE_OPTS:=$(KERNEL_MAKE_FLAGS) \
	M="$(PKG_BUILD_DIR)" \
	EXTRA_CFLAGS="$(EXTRA_CFLAGS)" \
	$(EXTRA_KCONFIG)

define Build/Compile
	$(MAKE) -C "$(LINUX_DIR)" $(MAKE_OPTS) modules
endef

$(eval $(call KernelPackage,rtl837x_gsw))
