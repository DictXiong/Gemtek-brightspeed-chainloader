# XR1710G 与 W1701K 二级 U-Boot 和网页恢复

**简体中文** · [English](README.en.md)

本项目维护 Gemtek XR1710G、W1700K 和 W1701K 的 U-Boot 2026.07 二级引导和网页恢复。它使用原厂 NAND 布局，在 `0x600000` 的 1 MiB 槽位运行，保留原厂一级引导。源码基于 [naoki66/Gemtek-brightspeed-chainloader](https://github.com/naoki66/Gemtek-brightspeed-chainloader)。XG2010G 请使用上游资料与构建。

> [!NOTE]
> **相对 naoki 上游的改动**
>
> - 新增 W1701K 的独立构建目标和槽位镜像。两个 EN8811H 网口均提供 DHCP 和网页恢复。
> - 配套 XR1710G Full Legacy AP 支持[看门狗分级恢复和一次性 `reboot-to-uboot`](docs/xr1710g-boot-recovery.md)。网页重建会为配套镜像[预留状态卷](docs/xr1710g-boot-recovery.md#兼容性和状态卷)。
> - XR1710G 更新了 [WAN 收发路径及特定 DMA 故障时的 LAN 恢复](docs/xr1710g-boot-recovery.md#网口与故障边界)，并提供[兼容槽位封装](docs/build-compatibility-slots.md)。

| 机型 | 构建目标 | 安装资料 |
| --- | --- | --- |
| XR1710G / W1700K | `xr1710g` | [串口首次安装](docs/flashing-xr1710g.md) · [Full 固件](https://github.com/DictXiong/ImmortalWrt-for-Gemtek-brightspeed) |
| W1701K | `w1701k` | [串口安装](docs/flashing-xr1710g.md#w1701k) |

## 安装与升级

| 设备现状 | 操作 |
| --- | --- |
| 从原厂一级引导首次安装二级 U-Boot | 使用对应机型的串口指南，先备份，再写匹配的完整槽位。 |
| 已能进入兼容的网页恢复 | 按[网页恢复指南](docs/recovery-web.md)使用 **uboot** 项升级槽位。第三方入口的格式要求见[兼容槽位](docs/build-compatibility-slots.md)。 |
| OpenWrt 正常运行且 SSH 可用 | 可用[在线槽位升级指南](docs/ssh-flash-chainloader.md)，先确认分区名、布局和回读。 |
| 更新 OpenWrt 固件 | 保留配置走正在运行的 OpenWrt；需要重建系统时使用网页的 **firmware** 项。XR1710G AP 的首次迁移见[固件操作指南](https://github.com/DictXiong/ImmortalWrt-for-Gemtek-brightspeed/blob/master/docs/xr1710g-ap-operations.md)。 |

[本仓 Releases](https://github.com/DictXiong/Gemtek-brightspeed-chainloader/releases)同时提供 XR1710G 和 W1701K 槽位镜像及校验文件。根据机型和安装入口选择 `*-slot.bin`，并用 `SHA256SUMS` 核对下载。编译中间产物 `u-boot.bin`、`u-boot.img` 及裸 FIT 用于构建和封装。

推荐与同机型的 [Full ImmortalWrt 固件](https://github.com/DictXiong/ImmortalWrt-for-Gemtek-brightspeed)配套使用。已有兼容固件可以继续使用，两个项目均可独立升级。

正式版先将已验收的源码合入 `main`，再用北京时间的发布日期创建八位 `YYYYMMDD` 标签。发布标题采用 `XR1710G and W1701K Secondary U-Boot YYYYMMDD`。发行说明记录镜像内嵌的实际构建提交。

## 日常维护与恢复

- 用[网页恢复](docs/recovery-web.md)连接 `http://192.168.0.1/`，备份分区、刷写正确的目标或直接重启回正常引导。
- 配套 XR1710G Full Legacy AP 另支持[自动分级恢复](docs/xr1710g-boot-recovery.md)及一次性 `reboot-to-uboot`。OpenWrt failsafe 通过 SSH 接入 `192.168.1.1`。首次迁移和状态卷按固件指南准备。
- 看到网口链路但网页不通时，可尝试另一健康端口或给电脑设同网段静态地址。恢复服务提供 DHCP，请使用可信隔离网络。

> [!WARNING]
> 二级和原厂一级共用 `uenv`。在二级 U-Boot 中请避免保存环境或恢复整套默认环境，否则可能改掉原厂启动配方。串口诊断和环境恢复见对应机型指南。XR1710G 和 XG2010G 的槽位及固件须与各自机型匹配。

## 构建

安装 AArch64 交叉编译工具链和常用 U-Boot 构建依赖。[构建流程](.github/workflows/build.yml)列出依赖并生成两种机型的标准槽位。[手工发布流程](.github/workflows/publish-existing-artifact.yml)核对同一次 CI 构建的两个槽位再发布。

```sh
target=xr1710g
./scripts/build.sh "$target"
./chainloader/build-chainloader-fit.sh "output/$target/u-boot.bin" out \
  "build/$target/tools/mkimage" "build/$target/tools/dumpimage" "$target"
```

打包 W1701K 时，将 `W1701K_PHY_FW_DIR` 指向 [linux-firmware 20261019](https://gitlab.com/kernel-firmware/linux-firmware/-/tree/20261019) 的 `airoha/` 目录。打包脚本会校验 EN8811H 的两个固件文件，把压缩后的 PHY 固件放在槽位末尾 64 KiB，并单独附上许可文件。

普通槽位使用本仓的前缀、shim 和机型 DTB。需要匹配其他上传入口时，按[兼容槽位](docs/build-compatibility-slots.md)封装。源码和槽位校验通过后，仍要按需在对应机型、引导方式和网口上验收。

## 项目资料

- [引导布局、环境、网口策略与开发笔记](BOOT-AND-RECOVERY-OPTIMIZATION.md)
- [上游 U-Boot 文档](README) · [许可证](COPYING)
- 原项目技术交流 QQ 群 `571318173`。感谢上游维护者及各机型的验证贡献。

## 赞赏

<img src="./c6ea388c976395326514814f80d512d5.png" width="420" alt="Donate / 捐赠二维码">
