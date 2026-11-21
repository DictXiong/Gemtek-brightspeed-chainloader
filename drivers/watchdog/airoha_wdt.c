// SPDX-License-Identifier: GPL-2.0
/*
 * Airoha EN7581 Timer3 watchdog.
 * Register layout follows the Linux Airoha watchdog driver.
 * Copyright (c) 2024, AIROHA. All rights reserved.
 */

#include <airoha_wdt.h>
#include <asm/io.h>
#include <clk.h>
#include <dm.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <wdt.h>

#define TIMER_CTRL		0x00
#define WDT_ENABLE		BIT(25)
#define WDT_TIMER_INTERRUPT	BIT(21)
#define WDT_TIMER_ENABLE	BIT(5)
#define WDT_TIMER_LOAD		0x2c
#define WDT_TIMER_CURRENT	0x30
#define WDT_RELOAD		0x38
#define WDT_RLD			BIT(0)

struct airoha_wdt {
	void __iomem *base;
	ulong rate;
};

static int airoha_wdt_reset(struct udevice *dev)
{
	const struct airoha_wdt *wdt = dev_get_priv(dev);

	setbits_le32(wdt->base + WDT_RELOAD, WDT_RLD);
	return 0;
}

static int airoha_wdt_stop(struct udevice *dev)
{
	const struct airoha_wdt *wdt = dev_get_priv(dev);
	const u32 bits = WDT_ENABLE | WDT_TIMER_ENABLE;

	clrbits_le32(wdt->base + TIMER_CTRL, bits);
	return readl(wdt->base + TIMER_CTRL) & bits ? -EIO : 0;
}

static u32 start_sample[AIROHA_WDT_START_SAMPLE_WORDS];
static bool start_sample_valid;

const u32 *airoha_wdt_start_sample(void)
{
	return start_sample_valid ? start_sample : NULL;
}

static int airoha_wdt_start(struct udevice *dev, u64 timeout_ms, ulong flags)
{
	const struct airoha_wdt *wdt = dev_get_priv(dev);
	const u32 enabled = WDT_ENABLE | WDT_TIMER_ENABLE;
	const u64 ticks = timeout_ms * wdt->rate / 1000;
	u32 old_control, old_load, control_request, reload_before;
	u32 control, load, first, second;

	start_sample_valid = false;
	if (!ticks || ticks > U32_MAX)
		return -EINVAL;

	old_control = readl(wdt->base + TIMER_CTRL);
	old_load = readl(wdt->base + WDT_TIMER_LOAD);
	control_request = old_control | enabled | WDT_TIMER_INTERRUPT;
	writel(ticks, wdt->base + WDT_TIMER_LOAD);
	writel(control_request, wdt->base + TIMER_CTRL);
	reload_before = readl(wdt->base + WDT_RELOAD);
	writel(reload_before | WDT_RLD, wdt->base + WDT_RELOAD);

	control = readl(wdt->base + TIMER_CTRL);
	load = readl(wdt->base + WDT_TIMER_LOAD);
	first = readl(wdt->base + WDT_TIMER_CURRENT);
	/* The low-level timer delay does not run cyclic watchdog servicing. */
	__udelay(2);
	second = readl(wdt->base + WDT_TIMER_CURRENT);

	start_sample[AIROHA_WDT_CTRL_BEFORE] = old_control;
	start_sample[AIROHA_WDT_CTRL_REQUEST] = control_request;
	start_sample[AIROHA_WDT_CTRL_READBACK] = control;
	start_sample[AIROHA_WDT_LOAD_BEFORE] = old_load;
	start_sample[AIROHA_WDT_LOAD_REQUEST] = ticks;
	start_sample[AIROHA_WDT_LOAD_READBACK] = load;
	start_sample[AIROHA_WDT_RELOAD_BEFORE] = reload_before;
	start_sample[AIROHA_WDT_RELOAD_REQUEST] = reload_before | WDT_RLD;
	start_sample[AIROHA_WDT_RELOAD_READBACK] = readl(wdt->base + WDT_RELOAD);
	start_sample[AIROHA_WDT_CURRENT_FIRST] = first;
	start_sample[AIROHA_WDT_CURRENT_SECOND] = second;
	start_sample[AIROHA_WDT_CTRL_RESTORED] = U32_MAX;
	start_sample[AIROHA_WDT_LOAD_RESTORED] = U32_MAX;
	start_sample_valid = true;

	/*
	 * Linux handoff checks the two enable bits. Write the interrupt bit
	 * as before and verify the load and a running counter as well.
	 */
	if ((control & enabled) == enabled && load == ticks &&
	    first <= ticks && second && second < first)
		return 0;

	writel(old_control, wdt->base + TIMER_CTRL);
	writel(old_load, wdt->base + WDT_TIMER_LOAD);
	start_sample[AIROHA_WDT_CTRL_RESTORED] = readl(wdt->base + TIMER_CTRL);
	start_sample[AIROHA_WDT_LOAD_RESTORED] = readl(wdt->base + WDT_TIMER_LOAD);
	return -EIO;
}

static int airoha_wdt_probe(struct udevice *dev)
{
	struct airoha_wdt *wdt = dev_get_priv(dev);
	struct clk bus;
	ulong rate;
	int ret;

	wdt->base = dev_read_addr_ptr(dev);
	if (!wdt->base)
		return -EINVAL;

	ret = clk_get_by_name(dev, "bus", &bus);
	if (ret)
		return ret;

	rate = clk_get_rate(&bus);
	if (IS_ERR_VALUE(rate) || rate < 2)
		return -EINVAL;

	/* This watchdog ticks at half the bus clock. Probe only reads the rate. */
	wdt->rate = rate / 2;
	return 0;
}

static const struct wdt_ops airoha_wdt_ops = {
	.start = airoha_wdt_start,
	.stop = airoha_wdt_stop,
	.reset = airoha_wdt_reset,
};

static const struct udevice_id airoha_wdt_ids[] = {
	{ .compatible = "airoha,en7581-wdt" },
	{ }
};

U_BOOT_DRIVER(airoha_wdt) = {
	.name = "airoha_wdt",
	.id = UCLASS_WDT,
	.of_match = airoha_wdt_ids,
	.probe = airoha_wdt_probe,
	.priv_auto = sizeof(struct airoha_wdt),
	.ops = &airoha_wdt_ops,
};
