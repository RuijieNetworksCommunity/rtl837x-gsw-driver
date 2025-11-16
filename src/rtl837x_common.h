#ifndef __RTL8372_COMMON_H__
#define __RTL8372_COMMON_H__

#include <linux/switch.h>
#include <linux/of_mdio.h>

#include "./rtk-api/rtk_error.h"
#include "./rtk-api/rtk_types.h"
#include "./rtk-api/rtk_switch.h"
#include "./rtk-api/phy.h"
#include "./rtk-api/port.h"
#include "./rtk-api/vlan.h"
#include "./rtk-api/chip.h"
#include "./rtk-api/eee.h"
#include "./rtk-api/rma.h"
#include "./rtk-api/cpuTag.h"
#include "./rtk-api/mib.h"
#include "./rtk-api/isolation.h"
#include "./rtk-api/igmp.h"
#include "./rtk-api/dal/rtl8373/rtl8373_asicdrv.h"

struct rtl837x_mib_counter {
	uint16_t	base;
	const char	*name;
};

struct rtl837x_sdsmode_map {
	rtk_sds_mode_t mode;
	const char *name;
};

struct rtk_gsw {
 	struct device *dev;
 	struct mii_bus *bus;

	int reset_pin;
	int smi_addr;

	const char *chip_name;
	switch_chip_t chip_id;
	const uint8_t *port_map;
	unsigned int num_ports;

	rtk_sds_mode_t sds0mode;
	rtk_sds_mode_t sds1mode;

	struct switch_dev sw_dev;
	unsigned int cpu_port;

    struct rtl837x_mib_counter *mib_counters;
	unsigned int num_mib_counters;

    struct {
        uint8_t valid;                 // 条目是否有效
        uint16_t vid;                // VLAN ID
        uint16_t mbr;           // 成员端口位图
        uint16_t untag;       // 未标记端口位图
    } vlan_table[4096];        // VLAN 配置表
	
	char buf[4096];

    uint16_t port_pvid[6];  // 端口PVID配置

	uint16_t flow_control_map;      // 流控配置位图    
	bool global_vlan_enable;

	int (*reset_func)(struct rtk_gsw *gsw);
};

extern int rtl837x_debug_proc_init(void);

extern int rtl837x_debug_proc_deinit(void);

extern int rtl837x_swconfig_init(struct rtk_gsw *gsw);

unsigned int mii_mgr_read(unsigned int phy_addr, 
		unsigned int phy_register, unsigned int *read_data);

unsigned int mii_mgr_write(unsigned int phy_addr, 
		unsigned int phy_register, unsigned int write_data);

#endif