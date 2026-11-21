// SPDX-License-Identifier: GPL-2.0

#include <airoha_wdt.h>
#include <asm/unaligned.h>
#include <command.h>
#include <dm.h>
#include <env.h>
#include <hexdump.h>
#include <image.h>
#include <linux/libfdt.h>
#include <mapmem.h>
#include <memalign.h>
#include <ubi_uboot.h>
#undef crc32
#include <u-boot/crc.h>
#include <wdt.h>
#include <xr1710g_boot_recovery.h>

#define PARTITION "system"
#define STATE_VOLUME "xr_boot_state"
#define PROTOCOL 1
#define PROP_PROTOCOL "dictxiong,boot-recovery-protocol"
#define PROP_IMAGE "dictxiong,boot-recovery-image"
#define PROP_HANDSHAKE "dictxiong,boot-recovery"
#define PROP_ONESHOT "dictxiong,boot-recovery-oneshot"
#define PROP_DIAG "dictxiong,boot-recovery-diag"
#define NOT_RUN -256
#define BOOT_TIMEOUT_MS 24000
#define ARM64_BOOTARGS_SIZE 2048
#define WATCHDOG_BOOTARGS "watchdog.open_timeout=30"
#define MAX_NORMAL_ATTEMPTS 2
#define PRODUCTION_STEP "xr1710g_boot_production"
#define LEGACY_STEP "run boot_production"
#define DEFAULT_READ "ubi read ${loadaddr} fit"
#define DEFAULT_BOOT "run ubi_read_production && bootm ${loadaddr}#${bootconf}"

/* Set only while the dedicated UBI read is being handed to bootm. */
static const void *production_fit;
static u32 oneshot_sequence;

enum fit_check {
	FIT_OK, FIT_VERIFY_OFF, FIT_ABSENT, FIT_SOURCE, FIT_CONFIG, FIT_OS,
	FIT_FDT, FIT_COMPAT, FIT_PROTOCOL, FIT_ID,
};

enum production_source {
	SOURCE_NOT_RUN, SOURCE_ENV, SOURCE_UBI, SOURCE_VOLUME, SOURCE_SHORT,
	SOURCE_READ, SOURCE_HEADER, SOURCE_SIZE, SOURCE_BOUND,
};

static struct {
	int route, source, source_rc, pre, go, room, chosen;
	int probe, read, add, start, write, stop, restore;
} diag = {
	.source_rc = NOT_RUN, .pre = NOT_RUN, .go = NOT_RUN,
	.room = NOT_RUN, .chosen = NOT_RUN, .probe = NOT_RUN,
	.read = NOT_RUN, .add = NOT_RUN, .start = NOT_RUN,
	.write = NOT_RUN, .stop = NOT_RUN, .restore = NOT_RUN,
};

void xr1710g_boot_recovery_diag_route(bool native)
{
	diag.route = native ? 2 : 1;
}

/*
 * Four-byte magic, fourteen signed result bytes, and one BE16 slack field.
 * The register sample extends this record only when the watchdog was tried.
 */
void xr1710g_boot_recovery_diag_finish(struct bootm_headers *images,
                                      int http_result)
{
	const int results[] = {
		diag.route, diag.source, diag.source_rc, diag.pre, diag.go,
		diag.chosen, diag.probe, diag.read, diag.add, diag.start,
		diag.write, diag.stop, diag.restore, http_result,
	};
	const u32 *wdt_sample = airoha_wdt_start_sample();
	u8 record[20 + AIROHA_WDT_START_SAMPLE_WORDS * 4] = { 'X', 'R', 'D', '1' };
	void *fdt = images->ft_addr;
	int chosen, i, ret = -FDT_ERR_NOTFOUND;
	u16 room = diag.room < 0 ? U16_MAX : min_t(unsigned int, diag.room, U16_MAX - 1);

	if (images->os.os != IH_OS_LINUX || !fdt || !images->ft_len)
		return;
	chosen = fdt_path_offset(fdt, "/chosen");
	if (chosen < 0)
		return;

	for (i = 0; i < ARRAY_SIZE(results); i++) {
		int val = results[i];

		if (val == NOT_RUN)
			val = -127;
		else if (val < -126 || val > 127)
			val = -128;
		record[4 + i] = val;
	}
	put_unaligned_be16(room, record + 18);
	if (wdt_sample) {
		record[3] = '2';
		for (i = 0; i < AIROHA_WDT_START_SAMPLE_WORDS; i++)
			put_unaligned_be32(wdt_sample[i], record + 20 + i * 4);
		ret = fdt_setprop(fdt, chosen, PROP_DIAG, record, sizeof(record));
	}
	if (ret) {
		record[3] = '1';
		if (fdt_setprop(fdt, chosen, PROP_DIAG, record, 20))
			puts("XR1710G: unable to publish boot diagnostic\n");
	}
	/* Keep legacy diagnostic data when there is only room for one property. */
	if (oneshot_sequence)
		fdt_setprop_u32(fdt, chosen, PROP_ONESHOT, oneshot_sequence);
}

static const char *fit_rejected(int *result, enum fit_check reason)
{
	*result = reason;
	return NULL;
}

enum recovery_mode {
	MODE_NORMAL,
	MODE_FAILSAFE,
	MODE_HTTP,
	MODE_HTTP_REQUEST,
};

enum boot_status {
	STATUS_PENDING,
	STATUS_READY,
};

/* Fixed 48-byte on-flash ABI shared with OpenWrt. */
struct boot_state {
	u8 magic[4];
	u8 version;
	u8 mode;
	u8 normal_attempts;
	u8 status;
	u8 sequence[4];
	u8 image[32];
	u8 crc[4];
};

const char *xr1710g_production_step(void)
{
	const char *boot = env_get("boot_production");
	const char *read = env_get("ubi_read_production");
	const char *config = env_get("bootconf");
	const char *addr = env_get("loadaddr");

	if (boot && !strcmp(boot, DEFAULT_BOOT) &&
	    read && !strcmp(read, DEFAULT_READ) &&
	    config && !strcmp(config, "config-1") &&
	    addr && !strcmp(addr, "0x81800000"))
		return PRODUCTION_STEP;

	return LEGACY_STEP;
}

static struct ubi_device *system_ubi_get(void)
{
	struct ubi_device *ubi = ubi_get_device(0);

	if (!ubi)
		return NULL;
	if (ubi->mtd && !strcmp(ubi->mtd->name, PARTITION))
		return ubi;

	ubi_put_device(ubi);
	return NULL;
}

static int do_xr1710g_boot_production(struct cmd_tbl *cmdtp, int flag, int argc,
				      char *const argv[])
{
	struct ubi_volume_desc *desc;
	struct ubi_device *ubi;
	long long size;
	char command[32];
	void *load;
	int ret;

	diag.source = SOURCE_ENV;
	if (strcmp(xr1710g_production_step(), PRODUCTION_STEP))
		return run_command(LEGACY_STEP, 0);

	diag.source = SOURCE_UBI;
	ubi = system_ubi_get();
	if (!ubi)
		return run_command(LEGACY_STEP, 0);

	diag.source = SOURCE_VOLUME;
	desc = ubi_open_volume_nm(0, "fit", UBI_READONLY);
	ubi_put_device(ubi);
	if (IS_ERR(desc)) {
		diag.source_rc = PTR_ERR(desc);
		return run_command(LEGACY_STEP, 0);
	}

	diag.source = SOURCE_SHORT;
	size = desc->vol->used_bytes;
	ubi_close_volume(desc);
	if (size < sizeof(struct fdt_header))
		return run_command(LEGACY_STEP, 0);

	/* Use the standard read command, without executing the env script. */
	snprintf(command, sizeof(command), "ubi read %lx fit",
		 (ulong)CONFIG_SYS_LOAD_ADDR);
	diag.source = SOURCE_READ;
	diag.source_rc = run_command(command, 0);
	if (diag.source_rc)
		return CMD_RET_FAILURE;

	load = map_sysmem(CONFIG_SYS_LOAD_ADDR, 0);
	diag.source = SOURCE_HEADER;
	diag.source_rc = fdt_check_header(load);
	if (!diag.source_rc) {
		diag.source = SOURCE_SIZE;
		if (fdt_totalsize(load) <= size) {
			diag.source = SOURCE_BOUND;
			production_fit = load;
		}
	}

	snprintf(command, sizeof(command), "bootm %lx#config-1",
		 (ulong)CONFIG_SYS_LOAD_ADDR);
	ret = run_command(command, 0);
	production_fit = NULL;
	unmap_sysmem(load);
	return ret;
}

U_BOOT_CMD(xr1710g_boot_production, 1, 0, do_xr1710g_boot_production,
	   "boot the XR1710G production UBI FIT", "");

static bool recovery_fdt_supported(const void *fdt)
{
	/* The WIRO conversion changes the DTB but keeps the FIT image ID. */
	return !fdt_node_check_compatible(fdt, 0, "gemtek,xr1710g") &&
	       fdt_node_check_compatible(fdt, 0, "gemtek,xr1710g-ubi") > 0;
}

static const char *recovery_fit_image_id(const void *fit, int config,
				       int *result)
{
	const fdt32_t *protocol;
	const char *id;
	int len, i;

	if (config < 0)
		return fit_rejected(result, FIT_CONFIG);

	protocol = fdt_getprop(fit, config, PROP_PROTOCOL, &len);
	if (!protocol || len != sizeof(*protocol) ||
	    fdt32_to_cpu(*protocol) != PROTOCOL)
		return fit_rejected(result, FIT_PROTOCOL);

	id = fdt_getprop(fit, config, PROP_IMAGE, &len);
	if (!id || len != 65 || id[64])
		return fit_rejected(result, FIT_ID);

	for (i = 0; i < 64; i++)
		if (hex_to_bin(id[i]) < 0 || (id[i] >= 'A' && id[i] <= 'F'))
			return fit_rejected(result, FIT_ID);

	*result = FIT_OK;
	return id;
}

static const char *recovery_image(const struct bootm_headers *images, int *result)
{
	const void *fit = images->fit_hdr_os;

	if (!images->verify)
		return fit_rejected(result, FIT_VERIFY_OFF);
	if (!fit)
		return fit_rejected(result, FIT_ABSENT);
	if (fit != production_fit)
		return fit_rejected(result, FIT_SOURCE);
	if (!images->fit_uname_cfg)
		return fit_rejected(result, FIT_CONFIG);
	if (images->os.os != IH_OS_LINUX)
		return fit_rejected(result, FIT_OS);
	if (strcmp(images->fit_uname_cfg, "config-1"))
		return fit_rejected(result, FIT_CONFIG);

	if (!images->ft_len || !images->ft_addr)
		return fit_rejected(result, FIT_FDT);
	if (!recovery_fdt_supported(images->ft_addr))
		return fit_rejected(result, FIT_COMPAT);

	return recovery_fit_image_id(fit, fit_conf_get_node(fit, "config-1"),
				     result);
}

/* 1 requests a state volume, 0 preserves legacy upload handling. */
int xr1710g_recovery_fit_state(const void *fit, size_t size)
{
	static const char *const external_props[] = {
		"data-position", "data-offset", "data-size",
	};
	const void *data, *dtb = NULL;
	const char *default_config, *os;
	size_t data_size, dtb_size = 0, offset;
	int config, configs, images, kernel, fdt, node, result;
	int protocol_len, image_len, len, i;

	/* Non-FIT uploads keep the original web recovery behavior. */
	if (size < sizeof(fdt32_t) || fdt_magic(fit) != FDT_MAGIC)
		return 0;
	if (fdt_check_full(fit, size))
		return -EINVAL;

	config = fit_conf_get_node(fit, "config-1");
	if (config == -FDT_ERR_NOTFOUND)
		return 0;
	if (config < 0)
		return -EINVAL;

	fdt_getprop(fit, config, PROP_PROTOCOL, &protocol_len);
	fdt_getprop(fit, config, PROP_IMAGE, &image_len);
	if (protocol_len == -FDT_ERR_NOTFOUND && image_len == -FDT_ERR_NOTFOUND)
		return 0;
	if (!recovery_fit_image_id(fit, config, &result))
		return -EINVAL;

	configs = fdt_path_offset(fit, FIT_CONFS_PATH);
	default_config = fdt_getprop(fit, configs, FIT_DEFAULT_PROP, &len);
	if (!default_config || len != sizeof("config-1") ||
	    memcmp(default_config, "config-1", sizeof("config-1")))
		return -EINVAL;

	kernel = fit_conf_get_prop_node(fit, config, FIT_KERNEL_PROP, IH_PHASE_NONE);
	if (kernel < 0)
		return -EINVAL;
	os = fdt_stringlist_get(fit, kernel, FIT_OS_PROP, 0, NULL);
	if (!os || strcmp(os, "linux"))
		return -EINVAL;

	images = fdt_path_offset(fit, FIT_IMAGES_PATH);
	fdt = fit_conf_get_prop_node(fit, config, FIT_FDT_PROP, IH_PHASE_NONE);
	if (images < 0 || fdt < 0)
		return -EINVAL;

	/* Verify the actual upload extent before any FIT hash reads image data. */
	fdt_for_each_subnode(node, fit, images) {
		for (i = 0; i < ARRAY_SIZE(external_props); i++)
			if (fdt_getprop(fit, node, external_props[i], &len) &&
			    len != sizeof(fdt32_t))
				return -EINVAL;

		if (fit_image_get_data(fit, node, &data, &data_size) ||
		    (uintptr_t)data < (uintptr_t)fit)
			return -EINVAL;
		offset = (uintptr_t)data - (uintptr_t)fit;
		if (offset > size || data_size > size - offset)
			return -EINVAL;
		if (node == fdt) {
			dtb = data;
			dtb_size = data_size;
		}
	}

	if (!dtb || fdt_check_full(dtb, dtb_size) || !recovery_fdt_supported(dtb))
		return -EINVAL;
	if (fit_config_verify(fit, config) || !fit_all_image_verify(fit))
		return -EBADMSG;

	return 1;
}

unsigned int xr1710g_boot_recovery_fdt_pad(const struct bootm_headers *images)
{
	/* The actual handoff may append to bootargs and insert the handshake. */
	return recovery_image(images, &diag.pre) ? 256 : 0;
}

static struct ubi_volume_desc *state_open(int mode)
{
	struct ubi_volume_desc *desc;
	struct ubi_device *ubi = system_ubi_get();
	bool available;

	if (!ubi)
		return ERR_PTR(-ENODEV);

	available = !ubi->ro_mode;
	ubi_put_device(ubi);
	if (!available)
		return ERR_PTR(-ENODEV);

	desc = ubi_open_volume_nm(0, STATE_VOLUME, mode);
	if (PTR_ERR_OR_ZERO(desc) == -ENODEV)
		return ERR_PTR(-ENOENT);
	if (IS_ERR(desc))
		return desc;

	if (desc->vol->vol_type != UBI_DYNAMIC_VOLUME ||
	    desc->vol->reserved_pebs != 1 || desc->vol->upd_marker) {
		ubi_close_volume(desc);
		return ERR_PTR(-EINVAL);
	}

	return desc;
}

static int state_read(struct boot_state *state)
{
	ALLOC_CACHE_ALIGN_BUFFER(struct boot_state, buf, 1);
	struct ubi_volume_desc *desc = state_open(UBI_READONLY);
	int ret;

	if (IS_ERR(desc))
		return PTR_ERR(desc);

	ret = ubi_read(desc, 0, (char *)buf, 0, sizeof(*buf));
	ubi_close_volume(desc);
	if (!ret)
		*state = *buf;

	return ret;
}

int xr1710g_recovery_create_empty_state(void)
{
	struct boot_state state;
	int ret = state_read(&state);

	if (ret == -ENOENT) {
		ret = ubi_create_vol(STATE_VOLUME, sizeof(state), true,
				     UBI_VOL_NUM_AUTO, false);
		if (!ret)
			ret = state_read(&state);
	}
	if (ret)
		return ret;

	return memchr_inv(&state, 0xff, sizeof(state)) ? -EBADMSG : 0;
}

static int state_write(const struct boot_state *state, bool create,
		       bool *attempted)
{
	struct ubi_volume_desc *desc = state_open(UBI_READWRITE);
	struct boot_state readback;
	int ret, size, write_ret;
	void *buf;

	if (PTR_ERR_OR_ZERO(desc) == -ENOENT && create) {
		/* A read on the selected, writable system UBI found no such volume. */
		ret = ubi_create_vol(STATE_VOLUME, sizeof(*state), true,
				     UBI_VOL_NUM_AUTO, false);
		if (ret)
			return ret;

		desc = state_open(UBI_READWRITE);
	}
	if (IS_ERR(desc))
		return PTR_ERR(desc);

	size = ALIGN(sizeof(*state), desc->vol->ubi->min_io_size);
	if (size > desc->vol->usable_leb_size) {
		ret = -EINVAL;
		goto close;
	}
	buf = malloc_cache_aligned(size);
	if (!buf) {
		ret = -ENOMEM;
		goto close;
	}

	memset(buf, 0xff, size);
	memcpy(buf, state, sizeof(*state));
	if (attempted)
		*attempted = true;
	write_ret = ubi_leb_change(desc, 0, buf, size);
	free(buf);
	ubi_close_volume(desc);

	/* An old-PEB erase scheduling error can follow a successful remap. */
	ret = state_read(&readback);
	if (!ret && !memcmp(state, &readback, sizeof(readback)))
		return 0;

	return write_ret ?: ret ?: -EIO;

close:
	ubi_close_volume(desc);
	return ret;
}

static u32 state_crc(const struct boot_state *state)
{
	return crc32(0, (const u8 *)state, offsetof(struct boot_state, crc));
}

static bool state_valid(const struct boot_state *state)
{
	return !memcmp(state->magic, "XRR1", sizeof(state->magic)) &&
	       state->version == PROTOCOL && state->mode <= MODE_FAILSAFE &&
	       state->status <= STATUS_READY &&
	       state->normal_attempts <= MAX_NORMAL_ATTEMPTS &&
	       get_unaligned_be32(state->sequence) &&
	       get_unaligned_be32(state->crc) == state_crc(state);
}

static enum recovery_mode state_next(const struct boot_state *old,
				     struct boot_state *state, const char *image)
{
	enum recovery_mode http = MODE_HTTP;
	u32 sequence = 1;

	memcpy(state->magic, "XRR1", sizeof(state->magic));
	state->version = PROTOCOL;
	state->mode = MODE_NORMAL;
	state->normal_attempts = 1;
	state->status = STATUS_PENDING;
	hex2bin(state->image, image, sizeof(state->image));

	if (old) {
		sequence = get_unaligned_be32(old->sequence) + 1;
		if (!sequence)
			sequence = 1;
	}

	if (old && !memcmp(old->image, state->image, sizeof(state->image))) {
		/* v1 never produces a ready failsafe with zero normal attempts. */
		if (old->mode == MODE_FAILSAFE &&
		    (old->status == STATUS_PENDING || !old->normal_attempts)) {
			if (old->status == STATUS_READY)
				http = MODE_HTTP_REQUEST;
			/* Consume the request or failed attempt before entering HTTP. */
			state->status = STATUS_READY;
			state->normal_attempts = 0;
		} else if (old->status == STATUS_PENDING) {
			if (old->normal_attempts == MAX_NORMAL_ATTEMPTS) {
				state->mode = MODE_FAILSAFE;
				state->normal_attempts = old->normal_attempts;
			} else {
				state->normal_attempts = old->normal_attempts + 1;
			}
		}
	}

	put_unaligned_be32(sequence, state->sequence);
	put_unaligned_be32(state_crc(state), state->crc);
	return state->status == STATUS_READY ? http : state->mode;
}

static void restore_bootargs(void *fdt, int chosen, const char *old_args)
{
	if (old_args)
		fdt_setprop_string(fdt, chosen, "bootargs", old_args);
	else
		fdt_delprop(fdt, chosen, "bootargs");
}

static int add_handshake(void *fdt, int chosen, const char *old_args,
			 const char *image, const struct boot_state *state)
{
	const char *mode = state->mode == MODE_FAILSAFE ? "failsafe" : "normal";
	const char *failsafe = state->mode == MODE_FAILSAFE ? " failsafe=1" : "";
	char handshake[96];
	size_t size = (old_args ? strlen(old_args) : 0) +
		      sizeof(WATCHDOG_BOOTARGS) + sizeof(" failsafe=1");
	char *new_args;
	int ret;

	if (size > ARM64_BOOTARGS_SIZE)
		return -EINVAL;

	new_args = malloc(size);
	if (!new_args)
		return -ENOMEM;

	snprintf(new_args, size, "%s%s%s%s", old_args ? old_args : "",
		 old_args && *old_args ? " " : "", WATCHDOG_BOOTARGS, failsafe);
	ret = fdt_setprop_string(fdt, chosen, "bootargs", new_args);
	free(new_args);
	if (ret)
		return ret;

	snprintf(handshake, sizeof(handshake), "%u:%s:%u:%s", PROTOCOL, image,
		 get_unaligned_be32(state->sequence), mode);
	ret = fdt_setprop_string(fdt, chosen, PROP_HANDSHAKE, handshake);
	if (ret)
		restore_bootargs(fdt, chosen, old_args);

	return ret;
}

bool xr1710g_boot_recovery_handoff(struct bootm_headers *images)
{
	const char *image = recovery_image(images, &diag.go);
	struct boot_state old, state;
	enum recovery_mode mode;
	struct udevice *wdt;
	void *fdt = images->ft_addr;
	const char *bootargs;
	char *old_args = NULL;
	bool create, attempted = false;
	int chosen, len, ret;

	oneshot_sequence = 0;
	if (!image || !fdt || !images->ft_len)
		return false;

	diag.chosen = chosen = fdt_path_offset(fdt, "/chosen");
	if (chosen < 0)
		return false;
	if (fdt_getprop(fdt, chosen, PROP_HANDSHAKE, NULL)) {
		diag.chosen = -EEXIST;
		return false;
	}
	diag.chosen = 0;
	diag.room = fdt_totalsize(fdt) - fdt_off_dt_strings(fdt) -
		    fdt_size_dt_strings(fdt);

	diag.probe = ret = uclass_get_device_by_driver(UCLASS_WDT,
						     DM_DRIVER_GET(airoha_wdt), &wdt);
	if (ret)
		return false;

	diag.read = ret = state_read(&old);
	create = ret == -ENOENT;
	if (create)
		memset(&old, 0xff, sizeof(old));
	else if (ret)
		return false;

	if (memchr_inv(&old, 0xff, sizeof(old)) && !state_valid(&old)) {
		diag.read = -EBADMSG;
		printf("XR1710G: invalid boot recovery state, booting normally\n");
		return false;
	}

	mode = state_next(state_valid(&old) ? &old : NULL, &state, image);
	if (mode == MODE_HTTP_REQUEST) {
		/* Never recreate a manual request, even after an ambiguous write. */
		diag.write = state_write(&state, false, NULL);
		diag.stop = wdt_stop(wdt);
		if (diag.write)
			puts("XR1710G: recovery request consumption unverified, booting normally\n");
		if (diag.stop)
			puts("XR1710G: cannot stop boot watchdog, booting normally\n");
		return !diag.write && !diag.stop;
	}
	if (mode == MODE_HTTP) {
		diag.stop = ret = wdt_stop(wdt);
		if (ret) {
			printf("XR1710G: cannot stop boot watchdog (%d), booting normally\n",
			       ret);
			return false;
		}
		diag.write = ret = state_write(&state, false, &attempted);
		if (ret) {
			if (attempted && (diag.restore = state_write(&old, false, NULL)))
				puts("XR1710G: failed to restore prior recovery state\n");
			return false;
		}

		return true;
	}

	bootargs = fdt_getprop(fdt, chosen, "bootargs", &len);
	if (bootargs) {
		if (len <= 0 || strnlen(bootargs, len) != len - 1) {
			diag.add = -EINVAL;
			return false;
		}
		old_args = strdup(bootargs);
		if (!old_args) {
			diag.add = -ENOMEM;
			return false;
		}
	} else if (len != -FDT_ERR_NOTFOUND) {
		diag.add = len;
		return false;
	}

	diag.add = ret = add_handshake(fdt, chosen, old_args, image, &state);
	if (ret)
		goto out;

	diag.start = ret = wdt_start(wdt, BOOT_TIMEOUT_MS, 0);
	if (!ret) {
		diag.write = ret = state_write(&state, create, &attempted);
		if (ret && (diag.stop = wdt_stop(wdt)))
			puts("XR1710G: failed to stop boot watchdog\n");
	}

	if (ret) {
		if (attempted && (diag.restore = state_write(&old, false, NULL)))
			puts("XR1710G: failed to restore prior recovery state\n");
		fdt_delprop(fdt, chosen, PROP_HANDSHAKE);
		restore_bootargs(fdt, chosen, old_args);
		printf("XR1710G: recovery handoff unavailable (%d), booting normally\n",
		       ret);
	} else if (mode == MODE_NORMAL) {
		/* Optional capability scoped to this successful normal handoff. */
		oneshot_sequence = get_unaligned_be32(state.sequence);
	}

out:
	free(old_args);
	return false;
}
