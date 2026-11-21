/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __XR1710G_BOOT_RECOVERY_H
#define __XR1710G_BOOT_RECOVERY_H

struct bootm_headers;

const char *xr1710g_production_step(void);
unsigned int xr1710g_boot_recovery_fdt_pad(const struct bootm_headers *images);
bool xr1710g_boot_recovery_handoff(struct bootm_headers *images);
int xr1710g_boot_recovery_http(void);
void xr1710g_boot_recovery_diag_route(bool native);
void xr1710g_boot_recovery_diag_finish(struct bootm_headers *images,
                                      int http_result);

#endif
