/* SPDX-License-Identifier: GPL-2.0+ */

#ifndef __AIROHA_ETH_H__
#define __AIROHA_ETH_H__

#include <stdbool.h>
#include <stddef.h>

struct udevice;

unsigned long airoha_recovery_get_lan_activity_ms(void);
bool airoha_recovery_dhcp_rx_allowed(struct udevice *dev);
int airoha_recovery_last_rx_fport(struct udevice *dev);
int airoha_recovery_xpon_json(char *buf, size_t size);
int airoha_recovery_wan_netdiag(struct udevice *dev, char *buf, size_t size);
void airoha_recovery_restart_links(struct udevice *dev);
void airoha_recovery_poll_link(struct udevice *dev);

#endif /* __AIROHA_ETH_H__ */
