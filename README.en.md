# XR1710G and W1701K second-stage U-Boot and web recovery

**English** · [简体中文](README.md)

This fork maintains U-Boot 2026.07 second-stage boot and web recovery for the Gemtek XR1710G, W1700K and W1701K. It runs in the 1 MiB slot at NAND offset `0x600000` on the stock layout, keeping the factory first stage in place. The source is based on [naoki66/Gemtek-brightspeed-chainloader](https://github.com/naoki66/Gemtek-brightspeed-chainloader). For the XG2010G, use the upstream builds and documentation.

> [!NOTE]
> **Changes from naoki upstream**
>
> - Adds a dedicated W1701K build and slot image with DHCP and web recovery on both EN8811H Ethernet ports.
> - [Watchdog-backed recovery and one-shot `reboot-to-uboot`](docs/xr1710g-boot-recovery.en.md). Repeated boot failures trigger OpenWrt failsafe. If failsafe also fails, the device enters second-stage U-Boot web recovery. A web rebuild [reserves the state volume](docs/xr1710g-boot-recovery.en.md#compatibility-and-state-volume) for matching images. These features require [firmware](https://github.com/DictXiong/ImmortalWrt-for-Gemtek-brightspeed/blob/master/README.en.md) that supports the recovery protocol.
> - The fork updates the [XR1710G WAN packet path and LAN recovery for a specific DMA failure](docs/xr1710g-boot-recovery.en.md#ethernet-and-failure-boundaries), and provides [compatibility slot packaging](docs/build-compatibility-slots.md#english).

| Model | Build target | Installation resources |
| --- | --- | --- |
| XR1710G / W1700K | `xr1710g` | [Serial installation](docs/flashing-xr1710g.md) · [Firmware](https://github.com/DictXiong/ImmortalWrt-for-Gemtek-brightspeed/blob/master/README.en.md) |
| W1701K | `w1701k` | [Serial installation](docs/flashing-xr1710g.md#w1701k) · [Firmware](https://github.com/DictXiong/ImmortalWrt-for-Gemtek-brightspeed/blob/master/README.en.md) |

## Installation and upgrades

| Starting point | Guide |
| --- | --- |
| Stock first-stage bootloader | Use the model's serial guide. Back up the device and use the matching complete slot image. |
| Compatible web recovery already works | Follow the [web recovery guide](docs/recovery-web.md) and use the **uboot** target. See [compatibility slots](docs/build-compatibility-slots.md) for other upload formats. |
| OpenWrt runs and SSH works | The [online slot update guide](docs/ssh-flash-chainloader.md) covers partition identification, layout and readback. |
| Updating OpenWrt firmware | Use the running OpenWrt installation to keep settings. Use the web **firmware** target when rebuilding the system. See the [firmware operation guide](https://github.com/DictXiong/ImmortalWrt-for-Gemtek-brightspeed/blob/master/docs/xr1710g-ap-operations.en.md) for first migration on either model. |

[This fork's releases](https://github.com/DictXiong/Gemtek-brightspeed-chainloader/releases) include XR1710G and W1701K slot images and checksums. Choose the `*-slot.bin` that matches the device and installation method, and verify it with `SHA256SUMS`. The raw U-Boot binaries and bare FIT are packaging inputs.

We recommend pairing the slot with the matching [ImmortalWrt firmware](https://github.com/DictXiong/ImmortalWrt-for-Gemtek-brightspeed/blob/master/README.en.md). Existing compatible firmware can still be used, and both projects can be upgraded independently.

`main` is the development branch. Releases use eight-digit date tags in `YYYYMMDD` format.

## Operation and recovery

- Use [web recovery](docs/recovery-web.md) at `http://192.168.0.1/` to back up partitions, flash the appropriate target, or reboot back to normal boot.
- Matching firmware on either model supports [automatic boot recovery](docs/xr1710g-boot-recovery.en.md) and the one-shot `reboot-to-uboot` command. OpenWrt failsafe provides SSH at `192.168.1.1`. Follow the firmware guide to prepare the state volume on first migration.
- If a port shows link but the page is unreachable, try a working port or a client address on the same subnet. Use a trusted isolated segment for the recovery DHCP server.

> [!WARNING]
> The first and second stages share `uenv`. Avoid saving the second-stage environment or restoring its full defaults. The model-specific serial guides cover diagnostics and restoring the factory environment. Match both the slot and OpenWrt image to the model.

## Build

Install an AArch64 cross-compiler and U-Boot build dependencies. The [build workflow](.github/workflows/build.yml) lists the dependencies and produces the standard slots for both devices. The [manual publication workflow](.github/workflows/publish-existing-artifact.yml) verifies both slots from one CI run before publication.

```sh
target=xr1710g
./scripts/build.sh "$target"
./chainloader/build-chainloader-fit.sh "output/$target/u-boot.bin" out \
  "build/$target/tools/mkimage" "build/$target/tools/dumpimage" "$target"
```

When packaging the W1701K slot, set `W1701K_PHY_FW_DIR` to the `airoha/` directory from [linux-firmware 20261019](https://gitlab.com/kernel-firmware/linux-firmware/-/tree/20261019). The packer verifies the two EN8811H firmware files, places the compressed PHY firmware in the final 64 KiB of the slot, and includes the license as a separate file.

The standard slot uses the prefix, shim and model DTB in this repository. To target a different recovery interface, use the matching [compatibility slot](docs/build-compatibility-slots.md). Build and packaging checks are separate from verification on the target board and Ethernet ports.

## Project resources

- [Boot layout, environment, ports and engineering notes](BOOT-AND-RECOVERY-OPTIMIZATION.md)
- [Upstream U-Boot documentation](README) · [License](COPYING)
- Upstream QQ group `571318173`. Thanks to the upstream maintainers and the contributors who validated each model.

## Donate to the upstream author

<img src="./c6ea388c976395326514814f80d512d5.png" width="420" alt="Donate / 捐赠二维码">
