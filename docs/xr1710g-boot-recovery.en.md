# XR1710G automatic boot recovery

[简体中文](xr1710g-boot-recovery.md) · [Web recovery](recovery-web.md)

The matching second-stage U-Boot and XR1710G Full Legacy AP firmware track boot progress in a dedicated UBI state volume. A reset before boot readiness can move the device to the next recovery stage.

## Recovery stages

| Stage | When it starts | Access |
| --- | --- | --- |
| Normal OpenWrt | The second stage follows its native production boot path. The system has 120 seconds from kernel boot to confirm readiness. | Use the configured management address. The watchdog resumes normal feeding after confirmation. |
| OpenWrt failsafe | Two normal boots reset before confirmation. Automatic failsafe has 60 seconds from kernel boot to confirm readiness. | Connect directly to a usable port, set a static client address and use SSH at `192.168.1.1:22`. |
| Second-stage web recovery | Failsafe also resets before confirmation. U-Boot consumes the failed-attempt record, then starts the recovery page. | Open `http://192.168.0.1/` and follow the [web recovery guide](recovery-web.md). |

Normal readiness checks writable UBIFS overlay, a configured management address, Dropbear and hostapd when wireless APs are enabled. This is a local boot checkpoint. External network access and failures occurring after a successful boot have separate checks.

## Request web recovery from OpenWrt

On a confirmed compatible OpenWrt boot, prepare an isolated management link, then run as root:

```sh
reboot-to-uboot
```

The command requires a one-shot capability in the current handoff and a writable state volume. The second stage verifies and consumes the request before opening web recovery. When finished, select **Reboot to System**. A regular `reboot` continues through the normal boot path. If OpenWrt is inaccessible, use the hardware recovery entry in the [model guide](flashing-xr1710g.md). See [web recovery](recovery-web.md) for flashing or returning to normal boot.

## Compatibility and state volume

| Combination | Behavior |
| --- | --- |
| New second-stage U-Boot + original Full Legacy AP FIT + writable state volume | Enables automatic recovery and the one-shot request. |
| New U-Boot + a previously compatible unmarked firmware | Boots normally. Available hardware recovery entry still applies. |
| Old U-Boot + a compatible recovery-enabled Full AP FIT | Boots normally without the matching runtime handoff. |
| Compatible pair with unavailable state storage | Boots normally without automatic recovery or one-shot requests. |

The native production path reads `config-1` from the `fit` volume of the `system` partition. Linux calls that UBI partition `ubi`. The protocol covers XR1710G Full Legacy AP. Manual `bootm`, network boot, FITs with the `gemtek,xr1710g-ubi` alias, gateway images and XG2010G do not enter the automatic protocol.

`xr_boot_state` is a one-LEB dynamic UBI volume. The loader can create it when a free LEB is available. A web rebuild with a correctly marked AP FIT reserves it before allocating `rootfs_data`. To keep settings when migrating from an older upgrader, follow the [firmware upgrade guide](https://github.com/DictXiong/ImmortalWrt-for-Gemtek-brightspeed/blob/master/docs/xr1710g-ap-operations.en.md#keep-settings-during-an-upgrade).

## Ethernet and failure boundaries

OpenWrt failsafe attempts all four sockets, isolating each usable port before bringing it up. U-Boot auto mode covers WAN, 10G LAN and both 1G LAN sockets. If a supported WAN DMA initialization timeout can be isolated and DMA disable is verified, web recovery can continue on LAN. An explicit port selection serves the selected path. Link indication alone cannot verify packet delivery.

Automatic escalation, one-shot entry and normal return have been exercised on an isolated XR1710G. See the corresponding verification records for exact images, ports and injected failures. A failed shared switch, FE, NAND or boot chain may still require serial or local recovery. Isolate the recovery network. U-Boot provides DHCP by default, and OpenWrt failsafe accepts root SSH without a password.

## Protocol

- The selected FIT `config-1` has `dictxiong,boot-recovery-protocol = <1>` and a 64-character lowercase hex `dictxiong,boot-recovery-image`. U-Boot records the attempt, sets up the watchdog, and passes the image ID, nonzero sequence and mode to Linux in `/chosen`.
- The first 48 bytes of `xr_boot_state` LEB 0 contain `XRR1`, version, mode, normal attempt count, status, a big-endian 32-bit sequence, the raw 32-byte image ID and IEEE CRC32 over the first 44 bytes. UBI atomic LEB replacement preserves record consistency. A reserved mode and status combination carries the one-shot request until consumption.
- The hardware handoff timeout is 24 seconds. Linux may feed it for up to 30 seconds while waiting for userspace to open it. READY must identify the current image, sequence and mode. If NAND is confirmed but the temporary notification is lost, userspace verifies the record asynchronously at the deadline and resumes feeding only for the current READY.
