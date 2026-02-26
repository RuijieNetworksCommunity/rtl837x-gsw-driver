/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2025 StarField Xu <air_jinkela@163.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/version.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/of_mdio.h>
#include <linux/of_platform.h>
#include <linux/of_gpio.h>
#include <linux/of_net.h>
#include <linux/gpio/consumer.h>
#include <linux/switch.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include "./rtl837x_common.h"

#include <linux/printk.h>

const uint8_t rtl8373_port_map[16] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, // 物理端口0-8
    0, 0, 0, 0, 0, 0, 0        // 填充
};

const uint8_t rtl8372_port_map[16] = {
    3, 4, 5, 6, 7, 8, // 物理端口3-8
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0 // 填充
};

static struct rtl837x_mib_counter rtl837x_mib_counters[] ={
	{0,"ifInOctets"},
	{2,"ifOutOctets"},
	{4,"ifInUcastPkts"},
	{6,"ifInMulticastPkts"},
	{8,"ifInBroadcastPkts"},
	{0xA,"ifOutUcastPkts"},
	{0xC,"ifOutMulticastPkts"},
	{0xE,"ifOutBroadcastPkts"},
	{0x10,"ifOutDiscards"},
	{0x19,"InPauseFrames"},
	{0x1A,"OutPauseFrames"},
	{0x1C,"TxBroadcastPkts"},
	{0x1D,"TxMulticastPkts"},
	{0x20,"TxUndersizePkts"},
	{0x21,"RxUndersizePkts"},
	{0x22,"TxOversizePkts"},
	{0x23,"RxOversizePkts"},
	{0x24,"TxFragments"},
	{0x25,"RxFragments"},
	{0x26,"TxJabbers"},
	{0x27,"RxJabbers"},
	{0x28,"TxCollisions"},
	{0x29,"Tx64Octets"},
	{0x2A,"Rx64Octets"},
	{0x2B,"Tx65to127Bytes"},
	{0x2C,"Rx65to127Bytes"},
	{0x2D,"Tx128to255Bytes"},
	{0x2E,"Rx128to255Bytes"},
	{0x2F,"Tx256to511Bytes"},
	{0x30,"Rx256to511Bytes"},
	{0x31,"Tx512to1023Bytes"},
	{0x32,"Rx512to1023Bytes"},
	{0x33,"Tx1024to1518Bytes"},
	{0x34,"Rx1024to1518Bytes"},
	{0x36,"RxUndersizedropPkts"},
	{0x37,"Tx1519toMaxBytes"},
	{0x38,"Rx1519toMaxBytes"},
	{0x39,"TxOverMaxBytes"},
	{0x3A,"RxOverMaxBytes"}
};

static int rtl837x_mdio_write(void *ctx, u32 reg, u32 val)
{
	struct rtk_gsw *priv = ctx;
	struct mii_bus *bus = priv->bus;
	int ret;

	mutex_lock(&bus->mdio_lock);

	// check busy
	ret = bus->read(bus, priv->mdio_addr, MDC_MDIO_CTRL_REG);
    if (ret & 0x4) {
		ret = RT_ERR_BUSYWAIT_TIMEOUT;
		goto out_unlock;
    }

	ret = bus->write(bus, priv->mdio_addr, MDC_MDIO_ADDR_REG, reg);
	if (ret)
		goto out_unlock;

	ret = bus->write(bus, priv->mdio_addr, MDC_MDIO_DATA_LOW, (val & 0xFFFF));
	if (ret)
		goto out_unlock;

	ret = bus->write(bus, priv->mdio_addr, MDC_MDIO_DATA_HIGH, ((val >> 16) & 0xFFFF));
	if (ret)
		goto out_unlock;

	ret = bus->write(bus, priv->mdio_addr, MDC_MDIO_CTRL_REG, MDC_MDIO_WRITE_CMD);
	if (ret)
		goto out_unlock;

	// check busy
	ret = bus->read(bus, priv->mdio_addr, MDC_MDIO_CTRL_REG);
    if (ret & 0x4) {
		ret = RT_ERR_BUSYWAIT_TIMEOUT;
		goto out_unlock;
    }
	ret = 0;
out_unlock:
	mutex_unlock(&bus->mdio_lock);
	// printk("rtl837x_mdio_write ret:%d\n", ret);
	return ret;
}

static int rtl837x_mdio_read(void *ctx, u32 reg, u32 *val)
{
	struct rtk_gsw *priv = ctx;
	struct mii_bus *bus = priv->bus;
	int ret, val_l, val_h;

	mutex_lock(&bus->mdio_lock);

	// check busy
	ret = bus->read(bus, priv->mdio_addr, MDC_MDIO_CTRL_REG);
    if (ret & 0x4) {
		ret = RT_ERR_BUSYWAIT_TIMEOUT;
		goto out_unlock;
    }

	ret = bus->write(bus, priv->mdio_addr, MDC_MDIO_ADDR_REG, reg);
	if (ret)
		goto out_unlock;

	ret = bus->write(bus, priv->mdio_addr, MDC_MDIO_CTRL_REG, MDC_MDIO_READ_CMD);
	if (ret)
		goto out_unlock;

	// check busy
	ret = bus->read(bus, priv->mdio_addr, MDC_MDIO_CTRL_REG);
    if (ret & 0x4) {
		ret = RT_ERR_BUSYWAIT_TIMEOUT;
		goto out_unlock;
    }


	val_l = bus->read(bus, priv->mdio_addr, MDC_MDIO_DATA_LOW);
	val_h = bus->read(bus, priv->mdio_addr, MDC_MDIO_DATA_HIGH);

    *val = val_l & 0xffff;
    *val |= (val_h & 0xffff) << 16;
	ret = 0;

out_unlock:
	mutex_unlock(&bus->mdio_lock);
	// printk("rtl837x_mdio_read ret:%d\n", ret);
	return ret;
}

static void rtl837x_mdio_lock(void *ctx)
{
	struct rtk_gsw *priv = ctx;

	mutex_lock(&priv->map_lock);
}

static void rtl837x_mdio_unlock(void *ctx)
{
	struct rtk_gsw *priv = ctx;

	mutex_unlock(&priv->map_lock);
}

static const struct regmap_config rtl837x_mdio_regmap_config = {
	.reg_bits = 16,
	.val_bits = 32,
	.reg_stride = 1,

	.max_register = 0xffff,
	.reg_format_endian = REGMAP_ENDIAN_BIG,
	.reg_read = rtl837x_mdio_read,
	.reg_write = rtl837x_mdio_write,
	.cache_type = REGCACHE_NONE,
	.lock = rtl837x_mdio_lock,
	.unlock = rtl837x_mdio_unlock,
};

static const struct regmap_config rtl837x_mdio_nolock_regmap_config = {
	.reg_bits = 16,
	.val_bits = 32,
	.reg_stride = 1,

	.max_register = 0xffff,
	.reg_format_endian = REGMAP_ENDIAN_BIG,
	.reg_read = rtl837x_mdio_read,
	.reg_write = rtl837x_mdio_write,
	.cache_type = REGCACHE_NONE,
	.disable_locking = true,
};

static char* chipid_to_chip_name(switch_chip_t id)
{
	switch (id)
	{
	case CHIP_RTL8373:
		return "RTL8373";
	case CHIP_RTL8372:
		return "RTL8372";
	case CHIP_RTL8224:
		return "RTL8224";
	case CHIP_RTL8373N:
		return "RTL8373N";
	case CHIP_RTL8372N:
		return "RTL8372N";
	case CHIP_RTL8224N:
		return "RTL8224N";
	case CHIP_RTL8366U:
		return "RTL8366U";
	default:
		return "Unknow";
	}
}

static int rtl837x_switch_probe(struct rtk_gsw *gsw)
{
	switch_chip_t sw_chip;

	for(int i = 0;; i++)
	{
		if(i>3)
			goto CHIP_NOT_SUPPORTED;

		if (switch_probe(&sw_chip) != RT_ERR_OK) {
			dev_warn(gsw->dev , "Error: Detect switch type failed\n");
			mdelay(50);
		}else
			break;
	}

	switch (sw_chip)
	{
	case CHIP_RTL8372:
	case CHIP_RTL8372N:
		gsw->chip_name = chipid_to_chip_name(sw_chip);
		gsw->num_ports = 6;
		gsw->port_map = rtl8372_port_map;
		goto END_DETECT_CHIP;
	case CHIP_RTL8373:
	case CHIP_RTL8373N:
		gsw->chip_name = chipid_to_chip_name(sw_chip);
		gsw->num_ports = 9;
		gsw->port_map = rtl8373_port_map;
		goto END_DETECT_CHIP;
	default:
		goto CHIP_NOT_SUPPORTED;
	}

CHIP_NOT_SUPPORTED:
	//未知芯片ID
	rtk_uint32 regValue;
	rtl8373_getAsicReg(0x4, &regValue);
	dev_err(gsw->dev, "Error: Can not support this device, devid 0x%x\n", regValue);
	return RT_ERR_CHIP_NOT_SUPPORTED;

END_DETECT_CHIP:
	gsw->chip_id = sw_chip;
	dev_info(gsw->dev, "Found Realtek RTL chip %s\n", gsw->chip_name);
	return RT_ERR_OK;
}

static int rtl837x_hw_reset(struct rtk_gsw *gsw)
{
	if (gsw->reset_pin < 0)
		return 0;
	dev_info(gsw->dev, "START HW RESET");
	gpio_direction_output(gsw->reset_pin, 0);

	gpio_set_value(gsw->reset_pin, 1);
	mdelay(50);

	gpio_set_value(gsw->reset_pin, 0);
	mdelay(50);

	gpio_set_value(gsw->reset_pin, 1);
	mdelay(50);

	dev_info(gsw->dev, "FINISH HW RESET");
	return 0;
}

static const struct rtl837x_sdsmode_map _rtl837x_sdsmode[] = {
	{ SERDES_10GQXG, "10g-qxg" },
	{ SERDES_10GUSXG, "10g-usxg" },
	{ SERDES_10GR, "10g-kr" },
	{ SERDES_HSG, "hsgmii" },
	{ SERDES_2500BASEX, "2500base-x" },
	{ SERDES_SG, "sgmii" },
	{ SERDES_1000BASEX, "1000base-x" },
	{ SERDES_100FX, "100base-fx" },
	{ SERDES_8221B, "8221b" },
};

static int rtl837x_sdsmode(const char *name, rtk_sds_mode_t *mode)
{
	int i;

	for (i=0; i<ARRAY_SIZE(_rtl837x_sdsmode); i++)
	{
		if (!strcmp(name, _rtl837x_sdsmode[i].name))
		{
			*mode = _rtl837x_sdsmode[i].mode;
			return 0;
		}
	}

	return -1;
}

static int rtl8372n_igmp_init(struct rtk_gsw *gsw)
{

	unsigned int ret;
	ret = rtk_igmp_init();
	if (ret) return ret;

	ret = rtk_igmp_state_set(TRUE);
	if (ret) return ret;

	ret = rtk_igmp_fastLeave_set(TRUE);
	if (ret) return ret;

	if(gsw->num_ports > 0)
	{
		for(int port = 0;port < gsw->num_ports;port++){
			rtk_uint32 phy_port = gsw->port_map[port];
			ret = rtk_igmp_maxGroup_set(phy_port, 255);
			if (ret)
			{
				dev_err(gsw->dev, "rtk_igmp_maxGroup_set failed, error:%d\n",ret);
				return ret;
			}
		}
		
	}
	return rtk_igmp_suppressionEnable_set(TRUE, TRUE);
}

static int rtl8372n_hw_init(struct rtk_gsw *gsw)
{

	unsigned int ret;
	rtl837x_hw_reset(gsw);
	ret = rtl837x_switch_probe(gsw);
	if(ret){
		dev_err(gsw->dev, "rtl837x_switch_probe Fail, error:%d\n", ret);
		return -EPERM;
	}

	ret = rtk_switch_init();
	if(ret){
		dev_err(gsw->dev, "rtk_switch_init Fail, error:%d\n", ret);
		return -EPERM;
	}

	ret = rtk_vlan_reset();
	if (ret)
	{
		dev_err(gsw->dev, "rtk_vlan_reset failed, error:%d\n", ret);
		return -EPERM;
	}

	ret = rtk_vlan_init();
	if (ret)
	{
		dev_err(gsw->dev, "rtk_vlan_init failed, error:%d\n", ret);
		return -EPERM;
	}

	ret = rtl8372n_igmp_init(gsw);
	if (ret)
	{
		dev_err(gsw->dev, "rtl8372n_igmp_init failed, error:%d\n", ret);
		return -EPERM;
	}

	return 0;
}

static ret_t init_rtl837x_gsw(struct rtk_gsw *gsw)
{
	ret_t ret;

	rtk_rmaParam_t pRmacfg;
	ret = rtk_rma_get(2, &pRmacfg);
	if ( ret )
	{
		dev_err(gsw->dev, "rtk_rma_get get rma failed, error:%d\n", ret);
	return -EPERM;
	}

	pRmacfg.operation = RMAOP_FORWARD; // 清零配置
	ret = rtk_rma_set(2, &pRmacfg);
	if ( ret )
	{
		dev_err(gsw->dev, "rtk_rma_get set rma failed, error:%d\n", ret);
		return -EPERM;
	}

	for(int port = 0;port < gsw->num_ports;port++){
		rtk_uint32 phy_port = gsw->port_map[port];
		ret = rtk_eee_portTxRxEn_set(phy_port, 0u, 0u);
		if (ret)
		{
			dev_err(gsw->dev, "rtk_eee_portTxRxEn_set failed, error:%d\n",ret);
			return -EPERM;
		}
	}

	ret = rtk_sdsMode_set(0, gsw->sds0mode);
	if (ret)
		return -EPERM;

	ret = rtk_sdsMode_set(1, gsw->sds1mode);
	if (ret)
		return -EPERM;

	ret = rtk_cpu_externalCpuPort_set(gsw->port_map[gsw->cpu_port]);
	if (ret)
	{
		dev_err(gsw->dev, "rtk_cpu_externalCpuPort_set failed, error:%d\n",ret);
		return -EPERM;
	}

	// TODO
	// res = rtl8372n_igrAcl_init();
	// if (res != RT_ERR_OK){
	// 	dev_err(gsw->dev, "ACL init failed, ret=%d\n", res);
	// 	return res;
	// }

	// TODO
	// res = rtl837x_acl_add_u(a1);
	// if (res != RT_ERR_OK){
	// 	dev_err(gsw->dev, "rtl837x_acl_add failed, ret=%d\n", res);
	// 	return res;
	// }

	return 0;
}

// below are platform driver
static const struct of_device_id rtk_gsw_match[] = {
	{ .compatible = "realtek,rtl837x" },
	{},
};

MODULE_DEVICE_TABLE(of, rtk_gsw_match);

static int rtl837x_gsw_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device *dev =&pdev->dev;
	struct device_node *mdio;
	struct mii_bus *mdio_bus;
	struct rtk_gsw *gsw;
	const char *sdsmode_name;
	rtk_sds_mode_t sdsmode;
	struct regmap_config rc;
	
	int ret;
	dev_info(dev,"start rtl837x_gsw_probe");

	mdio = of_parse_phandle(np, "rtl837x,mdio", 0);

	if (!mdio)
		return -EINVAL;

	mdio_bus = of_mdio_find_bus(mdio);
	if (!mdio_bus)
		return -EPROBE_DEFER;

	gsw = devm_kzalloc(dev, sizeof(struct rtk_gsw), GFP_KERNEL);
	if (!gsw)
		return -ENOMEM;	

	mutex_init(&gsw->map_lock);
	
	rc = rtl837x_mdio_regmap_config;
	rc.lock_arg = gsw;
	gsw->map = devm_regmap_init(dev, NULL, gsw, &rc);
	if (IS_ERR(gsw->map)) {
		ret = PTR_ERR(gsw->map);
		dev_err(dev, "regmap init failed: %d\n", ret);
		return -EINVAL;
	}

	rc = rtl837x_mdio_nolock_regmap_config;
	gsw->map_nolock = devm_regmap_init(dev, NULL, gsw, &rc);
	if (IS_ERR(gsw->map_nolock)) {
		ret = PTR_ERR(gsw->map_nolock);
		dev_err(dev, "regmap init failed: %d\n", ret);
		return -EINVAL;
	}

	gsw->dev = dev;
	gsw->bus = mdio_bus;
	gsw->reset_func = init_rtl837x_gsw;
	gsw->sds0mode = SERDES_OFF;
	gsw->sds1mode = SERDES_OFF;

	gsw->reset_pin = of_get_named_gpio(np, "rtl837x,reset-pin", 0);
	if (gsw->reset_pin >= 0) {
		ret = devm_gpio_request(gsw->dev, gsw->reset_pin, "rtl837x,reset-pin");
		if (ret) {
			dev_err(gsw->dev, "failed to request reset gpio\n");
			devm_kfree(dev, gsw);
			return ret;
		}
	}

	if (of_property_read_u32(np, "rtl837x,cpu-port", &gsw->cpu_port)) {
		dev_err(gsw->dev, "failed to get cpu port\n");
		devm_kfree(dev, gsw);
		return ret;
	}

	if (of_property_read_u32(np, "rtl837x,smi-addr", &gsw->mdio_addr))
		gsw->mdio_addr = 0x1d;

	if (!of_property_read_string(np, "rtl837x,sds0mode", &sdsmode_name) &&
			!rtl837x_sdsmode(sdsmode_name, &sdsmode))
		gsw->sds0mode = sdsmode;

	if (!of_property_read_string(np, "rtl837x,sds1mode", &sdsmode_name) &&
			!rtl837x_sdsmode(sdsmode_name, &sdsmode))
		gsw->sds1mode = sdsmode;

	gsw->mib_counters = rtl837x_mib_counters;
	gsw->num_mib_counters = ARRAY_SIZE(rtl837x_mib_counters);

	dev_info(gsw->dev, "rtl837x dev info:smi-addr:%d\tcpu_port:%d\tserdes-mode:%d\n", gsw->mdio_addr, gsw->cpu_port, gsw->sds0mode);

	platform_set_drvdata(pdev, gsw);
	gsw_regmap = gsw->map;
	ret = rtl8372n_hw_init(gsw);
	if (ret)
	{
		dev_err(gsw->dev, "rtl8372n_hw_init failed, ret=%d\n",ret);
		devm_kfree(dev, gsw);
		return -ENODEV;
	}

	ret = init_rtl837x_gsw(gsw);
	if (ret){
		dev_err(gsw->dev, "init_rtl837x_gsw failed, ret=%d\n", ret);
		devm_kfree(dev, gsw);
		return ret;
	}

	ret = rtl837x_swconfig_init(gsw);
	if (ret){
		dev_err(gsw->dev, "rtl837x_swconfig_init failed, ret=%d\n", ret);
		devm_kfree(dev, gsw);
		return ret;
	}

	rtl837x_debug_proc_init();
	return 0;
}

static void _rtl837x_gsw_remove(struct platform_device *pdev)
{
	struct rtk_gsw *gsw = platform_get_drvdata(pdev);
	unregister_switch(&gsw->sw_dev);
	rtl837x_debug_proc_deinit();
	platform_set_drvdata(pdev, NULL);
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(6,10,0)
static int rtl837x_gsw_remove(struct platform_device *pdev)
{
	_rtl837x_gsw_remove(pdev);
	return 0;
}
#else
static void rtl837x_gsw_remove(struct platform_device *pdev)
{
	_rtl837x_gsw_remove(pdev);
}
#endif



static struct platform_driver gsw_driver = {
	.probe = rtl837x_gsw_probe,
	.remove = rtl837x_gsw_remove,
	.driver = {
		.name = "rtl837x-gsw",
		.of_match_table = rtk_gsw_match,
	},
};

module_platform_driver(gsw_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("StarField Xu <air_jinkela@163.com>");
MODULE_DESCRIPTION("rtl8372n switch driver for MT7988");
