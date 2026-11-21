# XR1710G boot recovery

The XR1710G chainloader uses automatic boot recovery when the selected
`config-1` of an OpenWrt FIT includes `dictxiong,boot-recovery-protocol = <1>`
and `dictxiong,boot-recovery-image`, a 64-character lowercase hex image ID.
The standard production path must read the FIT from the `fit` volume in
`system` on the stock layout. This keeps ordinary `bootm`, network-loaded
images, custom recipes and WIRO-converted FITs on their usual boot paths.
Older images use the normal boot path. The firmware opts in to recovery only
when the chainloader supplies the matching runtime handshake in `/chosen`.
RESET button recovery, the vendor environment and HTTP recovery network
settings keep their usual behavior.

The state uses one logical eraseblock in a dedicated dynamic UBI volume named
`xr_boot_state` on the existing `system` partition. The chainloader can
create the volume if at least one logical eraseblock is free. Existing
installations whose `rootfs_data` takes all available space need one LEB
reserved before recovery can be enabled. A full UBI rebuild can remove that
reservation. Without writable state the chainloader boots normally, leaving
the existing volumes in place.

Two normal attempts that restart before the firmware confirms management
readiness lead to a failsafe attempt. If failsafe also restarts before it is
ready, the chainloader consumes the failure record and starts the existing
HTTP recovery server. A subsequent reboot can try the normal path again.
When recovery is armed at the Linux handoff, the Airoha watchdog starts with
a 24-second hardware timeout, and the kernel command line permits automatic
watchdog feeding for at most 30 seconds before user space opens the device.
An ordinary boot uses its usual watchdog settings.

The record is 48 bytes at the start of LEB 0. It contains ASCII `XRR1`, one
byte each for version (1), mode (0 normal, 1 failsafe), normal attempt count
(0–2), and status (0 pending, 1 ready), a nonzero 32-bit big-endian sequence
number, the 32 raw bytes of the image ID, and a big-endian IEEE CRC32 of the
first 44 bytes. Updates use UBI atomic LEB replacement. The runtime handshake
is a NUL-terminated string, `1:<image>:<sequence>:<normal|failsafe>`, in the
`dictxiong,boot-recovery` property. A ready acknowledgment applies to the
image, sequence and mode of the current boot.
