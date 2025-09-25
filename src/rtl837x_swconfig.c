#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/skbuff.h>
#include <linux/switch.h>
#include "./rtl837x_common.h"

#include <linux/printk.h>

// static struct rtk_gsw *_gsw;

#ifndef SWITCH_PORT_SPEED_10000
#define SWITCH_PORT_SPEED_10000 10000
#endif

#ifndef SWITCH_PORT_SPEED_2500
#define SWITCH_PORT_SPEED_2500 2500
#endif

#ifndef SWITCH_PORT_SPEED_5000
#define SWITCH_PORT_SPEED_5000 5000
#endif

static int rtl837x_sw_get_port_stats(struct switch_dev *dev, int port,struct switch_port_stats *stats)
{
    struct rtk_gsw *gsw = container_of(dev, struct rtk_gsw, sw_dev);

	rtk_stat_port_get(gsw->port_map[port], 0, &(stats->tx_bytes));             // tx_bytes
	rtk_stat_port_get(gsw->port_map[port], 2u, &(stats->rx_bytes));                // rx_bytes
	return 0;
}

/**
 * @brief 应用交换机配置
 * 
 * @param switch_dev 交换机设备结构指针
 * @return int 返回状态码 (0 = 成功)
 */
static int rtl837x_sw_apply_config(struct switch_dev *swdev)
{
    struct rtk_gsw *gsw = container_of(swdev, struct rtk_gsw, sw_dev);

    int ret = 0;

	printk("rtl837x Apply Config\n");
    
    // ====================== 1. 应用流控配置 ======================
    printk("rtl837x Apply flow control\n");
    for (int port = 0; port < gsw->num_ports; port++) {
        // 跳过CPU端口和特定端口
        if (port != 0 && port != swdev->cpu_port && port != 5) {
            rtk_port_phy_ability_t ana = {
                .Half_10 = 1,
                .Full_10 = 1,
                .Half_100 = 1,
                .Full_100 = 1,
                .Half_1000 = 0,
                .Full_1000 = 1,
                .adv_2_5G = 1,
                .adv_5G = 0,
                .adv_10GBase_T = 0,
                .FC = 0,
                .AsyFC = 0,
            };
            if ((gsw->flow_control_map >> port) & 1)
            {
                ana.FC = 1;
                ana.AsyFC = 1;
                printk("autoNegoAbility Port:%u Enabled",port);
            }
            // 设置端口自动协商能力
            ret = rtk_phy_autoNegoAbility_set(gsw->port_map[port], &ana);
            if (ret) {
                dev_err(gsw->dev, "端口 %d 流控配置失败: %d", port, ret);
                return ret;
            }
        }
    }
    
    // ====================== 2. 应用VLAN配置 ======================
    if (gsw->global_vlan_enable)
    {
		printk("rtl837x Apply Vlan config\n");
        // 重置VLAN配置
        rtk_vlan_reset();
        
        // 设置所有VLAN条目
        for (int vlan_id = 0; vlan_id < swdev->vlans; vlan_id++) {
            if (gsw->vlan_table[vlan_id].valid == 1) {
                rtk_vlan_entry_t vlan_cfg = {
                    .mbr.bits[0] = gsw->vlan_table[vlan_id].mbr,
                    .untag.bits[0] = gsw->vlan_table[vlan_id].untag,
					.svlan_chk_ivl_svl = 0,
					.fid_msti = 0,
					.ivl_svl = 1,
                };
				printk("rtl837x VLAN mbr:%u\tntag:%u\n",vlan_cfg.mbr.bits[0], vlan_cfg.untag.bits[0]);
                
                ret = rtk_vlan_set(vlan_id, &vlan_cfg);
                if (ret) {
                    dev_err(gsw->dev, "VLAN %d 配置失败: %d", vlan_id, ret);
                    return ret;
                }
            }
        }

		printk("rtl837x Apply PVID\n");
        // ====================== 3. 应用PVID配置 ======================
        for (int port = 0; port < swdev->ports; port++) {
            // printk("rtl837x PVID port:%u\tpvid:%u\n",gsw->port_map[port], gsw->port_pvid[port]);
            ret = rtk_vlan_portPvid_set(
                gsw->port_map[port], 
                gsw->port_pvid[port]
            );
            
            if (ret) {
                dev_err(gsw->dev, "端口 %d PVID 配置失败: %d", port, ret);
                return ret;
            }
        }
    }
    // ====================== 4. 应用端口隔离配置 ======================
    else
    {
	printk("rtl837x Apply port isolation\n");
        // 获取CPU端口的物理端口号
        rtk_uint32 cpu_phy_port = gsw->port_map[swdev->cpu_port];
        rtk_uint32 isolation_map = 0;
        
        // 构建隔离映射
        for (int port = 0; port < swdev->ports; port++) {
            // 跳过CPU端口
            if (port != swdev->cpu_port) {
                uint8_t phy_port = gsw->port_map[port];
                
                // 添加端口到隔离映射
                isolation_map |= (1 << phy_port);
                
                // 设置端口隔离
                ret = rtk_port_isolation_set(phy_port, (1 << cpu_phy_port));
                if (ret) {
                    dev_err(gsw->dev, "端口 %d 隔离配置失败: %d", port, ret);
                    return ret;
                }
            }
        }
        
        // 设置CPU端口的隔离
        ret = rtk_port_isolation_set(cpu_phy_port, isolation_map);
        if (ret) {
            dev_err(gsw->dev, "CPU端口隔离配置失败: %d", ret);
            return ret;
        }
    }
    
    return 0;
}

static int rtl837x_sw_get_vlan_ports(struct switch_dev *dev, struct switch_val *val)
{
    struct rtk_gsw *gsw = container_of(dev, struct rtk_gsw, sw_dev);

	val->len = 0;
    if(!(gsw->vlan_table[val->port_vlan].valid)) return 0;
	rtk_vlan_entry_t vlan_cfg;
	if (rtk_vlan_get(val->port_vlan, &vlan_cfg)) return -22;
    if (!vlan_cfg.ivl_svl) return 0; //跳过下面的多余循环
	// printk("rtl837x vid:%u\tVLAN mbr:%u\tuntag:%u\tfid:%u\n",val->port_vlan ,vlan_cfg.mbr.bits[0], vlan_cfg.untag.bits[0], vlan_cfg.fid);

	struct switch_port *port = &val->value.ports[0];
	for(int i = 0;i < gsw->num_ports;i++){
		if (!(vlan_cfg.mbr.bits[0] & BIT(gsw->port_map[i]))) continue;

		port->id = i;
		port->flags = (vlan_cfg.untag.bits[0] & BIT(gsw->port_map[i])) ? 0 : BIT(SWITCH_PORT_FLAG_TAGGED);
		val->len++;
		port++;
	}

	return 0;
}

/**
 * @brief 设置 VLAN 的端口成员
 * 
 * @param switch_dev 交换机设备结构指针
 * @param vlan_val VLAN 值结构指针
 * @return 返回状态码 (0 = 成功)
 */
static int rtl837x_sw_set_vlan_ports(struct switch_dev *dev, struct switch_val *vlan_val)
{
    // 获取 VLAN ID
    rtk_uint32 vlan_id = vlan_val->port_vlan;
    
    // 验证 VLAN ID 范围 (1-4094)
    if (vlan_id < 1 || vlan_id > 4094) return -22;
    
    // 获取设备数据
    struct rtk_gsw *gsw = container_of(dev, struct rtk_gsw, sw_dev);
    
    // 验证端口数量
    uint32_t port_count = vlan_val->len;
    
    // 初始化端口位图
    rtk_uint32 vlan_mbr = 0;      // 所有成员端口位图
    rtk_uint32 vlan_untag = 0; // 未标记端口位图
    
    // 处理每个端口
    if (port_count > 0) {
        struct switch_port *port_list = vlan_val->value.ports;
        
        for (uint32_t i = 0; i < port_count; i++) {
            // 获取物理端口号
            rtk_uint32 physical_port = port_list[i].id;
            
            // 获取逻辑端口索引
            rtk_uint32 logical_port = gsw->port_map[physical_port];
            
            // 计算端口位掩码
            rtk_uint32 port_mask = BIT(logical_port);
            
            // 添加到所有端口位图
            vlan_mbr |= port_mask;
            
            // 如果是未标记端口，添加到未标记位图
            if (!(port_list[i].flags & BIT(SWITCH_PORT_FLAG_TAGGED))) {
                vlan_untag |= port_mask;
                // printk("port:%u\tflags:%d\n",physical_port,port_list[i].flags);
            }
        }
    }
    
    // 更新 VLAN 配置
    gsw->vlan_table[vlan_id].vid = vlan_id;
    gsw->vlan_table[vlan_id].mbr = vlan_mbr;
    gsw->vlan_table[vlan_id].untag = vlan_untag;
    gsw->vlan_table[vlan_id].valid = 1;
	printk("vlanid:%u\tportmap:%016x\tuntag:%016x\tvalid:%u\n",gsw->vlan_table[vlan_id].vid, gsw->vlan_table[vlan_id].mbr, gsw->vlan_table[vlan_id].untag, gsw->vlan_table[vlan_id].valid);
	// rtl837x_apply_config(dev);
    return 0;
}

static int rtl837x_sw_get_port_pvid(struct switch_dev *dev, int port, int *val)
{
	int result; // x0
    struct rtk_gsw *gsw = container_of(dev, struct rtk_gsw, sw_dev);

	if (port > gsw->num_ports) return -22;

	result = rtk_vlan_portPvid_get(gsw->port_map[port], val);
	if ( result )
	{
		dev_err(gsw->dev, "%s: rtk_vlan_portPvid_get failed, ret=%d\n", "rtl837x_get_port_pvid", result);
		return -22;
	}
	return result;
}

static int rtl837x_sw_set_port_pvid(struct switch_dev *dev, int port, int val)
{
    struct rtk_gsw *gsw = container_of(dev, struct rtk_gsw, sw_dev);

    if (port > gsw->num_ports) return -22;

    gsw->port_pvid[port] = val;

    return 0;
}

/**
 * @brief 转换速度代码为具体速率值
 * 
 * @param speed_code 硬件速度代码
 * 
 * @return uint32_t 实际速率值 (Mbps)
 */
static uint32_t convert_speed_code(uint32_t speed_code)
{
    switch (speed_code) {
        case 0:		return SWITCH_PORT_SPEED_10;
        case 1:   	return SWITCH_PORT_SPEED_100;
        case 2:     return SWITCH_PORT_SPEED_1000;
        case 4:    	return SWITCH_PORT_SPEED_10000;
        case 5:    	return SWITCH_PORT_SPEED_2500;
        case 6:    	return SWITCH_PORT_SPEED_5000;
        default:    return SWITCH_PORT_SPEED_UNKNOWN; // 未知状态
    }
}

static int rtl837x_sw_get_port_link_status(struct switch_dev *dev, int port, struct switch_port_link *link)
{
    struct rtk_gsw *gsw = container_of(dev, struct rtk_gsw, sw_dev);
    ret_t ret;
    // 检查端口有效性
    if (port >= gsw->num_ports) {
        dev_err(gsw->dev, "无效端口号: %u", port);
        return -22;
    }
    
    // 获取物理端口号
    rtk_uint32 phy_port = gsw->port_map[port];
    
    // 获取MAC状态信息
    rtk_port_status_t port_status;
    ret = rtk_port_macStatus_get(phy_port, &port_status);
    if(ret)
	{
        dev_err(gsw->dev, 
                "获取端口 %u (物理端口 %u) MAC状态失败: %d", 
                port, phy_port, ret);
        return -EINVAL;
    }
    
    // 获取自动协商状态（特殊端口除外）
    bool auto_neg_enabled = 1;
    
    // 特殊端口：管理端口或端口5
    const bool is_special_port = (port == gsw->cpu_port) || (port == 5);
    
    if (!is_special_port) {
        rtk_uint32 auto_neg_value;
        ret = rtk_phy_common_c45_autoNegoEnable_get(phy_port, &auto_neg_value);
        if (ret != RT_ERR_OK) {
            dev_err(gsw->dev, 
                    "获取端口 %u (物理端口 %u) 自动协商状态失败: %d", 
                    port, phy_port, auto_neg_value);
            return ret;
        }
        auto_neg_enabled = (auto_neg_value != 0);
    }
    
    // 解析并填充链路状态
    struct switch_port_link result = {
        .link = (port_status.link != 0),
        .duplex = (port_status.duplex != 0),
        .aneg = auto_neg_enabled,
        .tx_flow = (port_status.txpause != 0),
        .rx_flow = (port_status.rxpause != 0),
        .speed = convert_speed_code(port_status.speed)
    };

    *link = result;
    return RT_ERR_OK;
}

static int rtl837x_sw_set_port_link(struct switch_dev *dev, int port, struct switch_port_link *link)
{
	return 0;
}

static int rtl837x_sw_reset_switch(struct switch_dev *dev)
{
	return 0;
}

static int rtl837x_sw_set_vlan_enable(struct switch_dev *dev, const struct switch_attr *attr, struct switch_val *val)
{
    struct rtk_gsw *gsw = container_of(dev, struct rtk_gsw, sw_dev);
	gsw->global_vlan_enable = val->value.i;
	return 0;
}

static int rtl837x_sw_get_flowcontrol_ports(struct switch_dev *dev, const struct switch_attr *attr, struct switch_val *val)
{
    struct rtk_gsw *gsw = container_of(dev, struct rtk_gsw, sw_dev);
    val->value.i = gsw->flow_control_map;
	return 0;
}

static int rtl837x_sw_set_flowcontrol_ports(struct switch_dev *dev, const struct switch_attr *attr, struct switch_val *val)
{
    struct rtk_gsw *gsw = container_of(dev, struct rtk_gsw, sw_dev);
	gsw->flow_control_map = val->value.i;
	return 0;
}

static int rtl837x_sw_get_vlan_enable(struct switch_dev *dev, const struct switch_attr *attr, struct switch_val *val)
{
    struct rtk_gsw *gsw = container_of(dev, struct rtk_gsw, sw_dev);
    val->value.i = gsw->global_vlan_enable;
	return 0;
}

static int rtl837x_sw_reset_mibs(struct switch_dev *dev, const struct switch_attr *attr, struct switch_val *val)
{
    // struct rtk_gsw *gsw = container_of(dev, struct rtk_gsw, sw_dev);
    rtk_stat_global_reset();
	return 0;
}

static int rtl837x_sw_reset_port_mibs(struct switch_dev *dev,const struct switch_attr *attr,struct switch_val *val)
{
	rtk_uint32 port;
    struct rtk_gsw *gsw = container_of(dev, struct rtk_gsw, sw_dev);

	port = val->port_vlan;
	if (port >= 9) return -EINVAL;

	return rtk_stat_port_reset(gsw->port_map[port]);
}

static int rtl837x_sw_get_port_mib(struct switch_dev *dev, const struct switch_attr *attr, struct switch_val *val)
{	
    struct rtk_gsw *gsw = container_of(dev, struct rtk_gsw, sw_dev);

	int i, len = 0;
	rtk_stat_counter_t counter = 0;
	char *buf = gsw->buf;

	if (val->port_vlan >= gsw->num_ports)
		return -EINVAL;

	len += snprintf(buf + len, sizeof(gsw->buf) - len, "Port %d MIB counters\n", val->port_vlan);

	for (i = 0; i < gsw->num_mib_counters; ++i) {
		len += snprintf(buf + len, sizeof(gsw->buf) - len, "%-36s: ", gsw->mib_counters[i].name);

		if (!rtk_stat_port_get(val->port_vlan, gsw->mib_counters[i].base, &counter))
			len += snprintf(buf + len, sizeof(gsw->buf) - len, "%llu\n", counter);
		else
			len += snprintf(buf + len, sizeof(gsw->buf) - len, "%s\n", "error");
	}

	val->value.s = buf;
	val->len = len;
	return 0;
}

static struct switch_attr rtl832n_globals[] = {
	{
		.type = SWITCH_TYPE_INT,
		.name = "enable_vlan",
		.description = "Enable VLAN mode",
		.set = rtl837x_sw_set_vlan_enable,
		.get = rtl837x_sw_get_vlan_enable,
		.max = 1,
	}, {
		.type = SWITCH_TYPE_NOVAL,
		.name = "reset_mibs",
		.description = "Reset all MIB counters",
		.set = rtl837x_sw_reset_mibs,
	}, {
		.type = SWITCH_TYPE_INT,
		.name = "enable_flowcontrol",
		.description = "set hw flow control of port mask (1f: all ports)",
		.set = rtl837x_sw_set_flowcontrol_ports,
		.get = rtl837x_sw_get_flowcontrol_ports,
    }
};

static struct switch_attr rtl837x_port[] = {
	{
		.type = SWITCH_TYPE_NOVAL,
		.name = "reset_mib",
		.description = "Reset single port MIB counters",
		.set = rtl837x_sw_reset_port_mibs,
	}, {
		.type = SWITCH_TYPE_STRING,
		.name = "mib",
		.description = "Get MIB counters for port",
		.set = NULL,
		.get = rtl837x_sw_get_port_mib,
	},
};

static const struct switch_dev_ops rtl8372n_sw_ops = {
    .attr_global = { .attr = rtl832n_globals, .n_attr = ARRAY_SIZE(rtl832n_globals)},
    .attr_port = { .attr = rtl837x_port, .n_attr = ARRAY_SIZE(rtl837x_port) },
    .attr_vlan = { .attr = NULL, .n_attr = 0 },

	.get_vlan_ports = rtl837x_sw_get_vlan_ports,
	.set_vlan_ports = rtl837x_sw_set_vlan_ports,

	.get_port_pvid = rtl837x_sw_get_port_pvid,
	.set_port_pvid = rtl837x_sw_set_port_pvid,
	
	.apply_config = rtl837x_sw_apply_config,
	.reset_switch = rtl837x_sw_reset_switch,

	.get_port_link = rtl837x_sw_get_port_link_status,
	.set_port_link = rtl837x_sw_set_port_link,

	.get_port_stats = rtl837x_sw_get_port_stats,
};

int rtl837x_swconfig_init(struct rtk_gsw *gsw)
{   

	struct switch_dev *dev = &gsw->sw_dev;
	int err;

	dev->name = "RTL8372n";
	dev->cpu_port = gsw->cpu_port;
	dev->ports = gsw->num_ports;
	dev->vlans = 4096;
	dev->ops = &rtl8372n_sw_ops;
	dev->alias = dev_name(gsw->dev);

	err = register_switch(dev, NULL);
	if (err)
		dev_err(gsw->dev, "switch registration failed\n");

	gsw->global_vlan_enable = true; 
    gsw->flow_control_map = 0x1f; //启用所有端口的autoNegoAbility

	return err;
}