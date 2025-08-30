include $(TOPDIR)/rules.mk
include $(INCLUDE_DIR)/kernel.mk

PKG_NAME:=rtl837x_gsw
PKG_VERSION:=0.0.1
PKG_RELEASE:=1
PKG_MAINTAINER:=air jinkela (air_jinkela@163.com)

include $(INCLUDE_DIR)/package.mk

define KernelPackage/$(PKG_NAME)
	TITLE:=$(PKG_NAME)
	FILES:=$(PKG_BUILD_DIR)/rtl837x_gsw.ko
	AUTOLOAD:=$(call AutoLoad, 99, rtl837x_gsw)
	DEPENDS:=+kmod-swconfig
	SUBMENU:=Drivers
	MENU:=1
endef

define KernelPackage/$(PKG_NAME)/config
	source "$(SOURCE)/config.in"
endef

PKG_KCONFIG:= \
	SDS_0_PN_SWAP_RX_64B66B \
	SDS_1_PN_SWAP_RX_64B66B \
	SDS_0_PN_SWAP_RX_8B10B \
	SDS_1_PN_SWAP_RX_8B10B \
	PHY_MDI_SWAP_RX_TX \
	PHY_TX_POLARITY_SWAP

PKG_CONFIG_DEPENDS:=$(foreach c, $(PKG_KCONFIG),$(if $(CONFIG_RTL837x_$c),CONFIG_$(c)))

EXTRA_KCONFIG:= \
	CONFIG_RTL837x_GSW=m \
	$(foreach c, $(PKG_KCONFIG),$(if $(CONFIG_RTL837x_$c),CONFIG_$(c)=$(CONFIG_RTL837x_$(c))))

EXTRA_CFLAGS:= \
	$(patsubst CONFIG_%, -DCONFIG_%=y, $(patsubst %=m,%,$(filter %=m,$(EXTRA_KCONFIG)))) \
	$(patsubst CONFIG_%, -DCONFIG_%=y, $(patsubst %=y,%,$(filter %=y,$(EXTRA_KCONFIG)))) \
	-DVERSION=$(PKG_RELEASE) \
	-I$(PKG_BUILD_DIR)/include \

MAKE_OPTS:=$(KERNEL_MAKE_FLAGS) \
	M="$(PKG_BUILD_DIR)" \
	EXTRA_CFLAGS="$(EXTRA_CFLAGS)" \
	$(EXTRA_KCONFIG)

define Build/Compile
	$(info EXTRA_CFLAGS: $(EXTRA_CFLAGS))
	$(MAKE) -C "$(LINUX_DIR)" $(MAKE_OPTS) modules
endef

$(eval $(call KernelPackage,rtl837x_gsw))
