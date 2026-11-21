# XR1710G / XG2010G second-stage U-Boot and web recovery

**English** · [简体中文](README.md)

This fork provides U-Boot 2026.07 second-stage boot and web recovery for the Brightspeed/Gemtek XR1710G (W1700K / W1701K) and XG2010G. It runs in the 1 MiB slot at NAND offset `0x600000` on the stock layout, keeping the factory first stage in place. The source is based on [naoki66/Gemtek-brightspeed-chainloader](https://github.com/naoki66/Gemtek-brightspeed-chainloader).

> [!NOTE]
> **Changes from naoki upstream**
>
> - Paired XR1710G Full Legacy AP images support [watchdog-backed recovery and one-shot `reboot-to-uboot`](docs/xr1710g-boot-recovery.en.md). A web rebuild [reserves the state volume](docs/xr1710g-boot-recovery.en.md#compatibility-and-state-volume) for those images.
> - The fork updates the [XR1710G WAN packet path and LAN recovery for a specific DMA failure](docs/xr1710g-boot-recovery.en.md#ethernet-and-failure-boundaries), and provides [compatibility slot packaging](docs/build-compatibility-slots.md#english).

| Model | Build target | Installation resources |
| --- | --- | --- |
| XR1710G (W1700K / W1701K) | `xr1710g` | [First installation via serial](docs/flashing-xr1710g.md) · [Full firmware](https://github.com/DictXiong/ImmortalWrt-for-Gemtek-brightspeed/blob/master/README.en.md) |
| XG2010G | `xg2010g` | [First installation via serial](docs/flashing-xg2010g.md) |

## Installation and upgrades

| Starting point | Guide |
| --- | --- |
| Stock first-stage bootloader | Use the model's serial guide. Back up the device and use the matching complete slot image. |
| Compatible web recovery already works | Follow the [web recovery guide](docs/recovery-web.md) and use the **uboot** target. See [compatibility slots](docs/build-compatibility-slots.md) for other upload formats. |
| OpenWrt runs and SSH works | The [online slot update guide](docs/ssh-flash-chainloader.md) covers partition identification, layout and readback. |
| Updating OpenWrt firmware | Use the running OpenWrt installation to keep settings. Use the web **firmware** target when rebuilding the system. See the [firmware operation guide](https://github.com/DictXiong/ImmortalWrt-for-Gemtek-brightspeed/blob/master/docs/xr1710g-ap-operations.en.md) for XR1710G AP migration. |

Use [this fork's releases](https://github.com/DictXiong/Gemtek-brightspeed-chainloader/releases) for published fork artifacts and [upstream releases](https://github.com/naoki66/Gemtek-brightspeed-chainloader/releases) for upstream builds. Verify the checksum and select the `*-slot.bin` for the model and upload method. The raw U-Boot binaries and bare FIT are packaging inputs.

## Operation and recovery

- Use [web recovery](docs/recovery-web.md) at `http://192.168.0.1/` to back up partitions, flash the appropriate target, or reboot back to normal boot.
- Compatible XR1710G Full Legacy AP installations also support [automatic boot recovery](docs/xr1710g-boot-recovery.en.md) and the one-shot `reboot-to-uboot` command. OpenWrt failsafe provides SSH at `192.168.1.1`. Follow the firmware guide to prepare the state volume on first migration.
- If a port shows link but the page is unreachable, try a working port or a client address on the same subnet. Use a trusted isolated segment for the recovery DHCP server.

> [!WARNING]
> The first and second stages share `uenv`. Avoid saving the second-stage environment or restoring its full defaults. The model-specific serial guides cover diagnostics and restoring the factory environment. Match both the slot and OpenWrt image to the model.

## Build

Install an AArch64 cross-compiler and U-Boot build dependencies. The [two-model GitHub Actions workflow](.github/workflows/build.yml) lists the dependencies and checks the standard slot images.

```sh
target=xr1710g  # Use xg2010g for XG2010G.
./scripts/build.sh "$target"
./chainloader/build-chainloader-fit.sh "output/$target/u-boot.bin" out \
  "build/$target/tools/mkimage" "build/$target/tools/dumpimage" "$target"
```

The standard slot uses the prefix, shim and model DTB in this repository. To target a different recovery interface, use the matching [compatibility slot](docs/build-compatibility-slots.md). Build and packaging checks are separate from verification on the target board and Ethernet ports.

## Project resources

- [Boot layout, environment, ports and engineering notes](BOOT-AND-RECOVERY-OPTIMIZATION.md)
- [Upstream U-Boot documentation](README) · [License](COPYING)
- Upstream QQ group `571318173`. Thanks to the upstream maintainers and the contributors who validated each model.

## Donate

<img src="./c6ea388c976395326514814f80d512d5.png" width="420" alt="Donate / 捐赠二维码">
