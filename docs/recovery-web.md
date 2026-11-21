# U-Boot 网页恢复 / U-Boot web recovery

[简体中文](#使用恢复页) · [English](#use-the-recovery-page)

## 使用恢复页

1. 在隔离网络或直连环境下，按机型指南选择硬件恢复入口。[配套 XR1710G AP](xr1710g-boot-recovery.md#主动进入网页恢复) 也可从已确认的 OpenWrt 发起一次性请求。
2. 电脑使用 DHCP 或静态 `192.168.0.10/24`（无需网关），打开 `http://192.168.0.1/`。遇到链路正常但不可达的端口时，换用另一健康端口。
3. 核对机型、当前二级 U-Boot 版本和准备使用的镜像校验值。**uboot** 接受当前恢复入口支持的完整槽位，**firmware** 接受同机型和分区布局的 OpenWrt 固件 FIT。
4. 等待写入完成。若未刷写且只想回到正常引导，点击 **重启到系统**；若刷写后页面正在自动重启，等待系统回来。

网页重建系统 UBI 会清除旧配置和 overlay 数据。保留设置请优先走正在运行的 OpenWrt 自带升级，并按固件指南检查迁移状态。恢复服务可发 DHCP，且页面可写入设备，请保持网络隔离。

### 只读备份

在页面的备份区选择具体 MTD 分区或 UBI 卷。请将备份和校验值存到设备之外，保留机型、版本和布局信息。备份可能含设备标识、校准数据和配置。恢复备份前，应根据机型和分区分别确认写回方式。

## Use the recovery page

1. Connect the device on a trusted isolated network or directly to your computer and follow the hardware entry instructions for the model. A confirmed [compatible XR1710G AP](xr1710g-boot-recovery.en.md#request-web-recovery-from-openwrt) can also request one-shot entry from OpenWrt.
2. Use DHCP or a static client address of `192.168.0.10/24` with no gateway. Open `http://192.168.0.1/`. If a port shows carrier but does not pass traffic, use another working port.
3. Check the model, second-stage build ID and image checksums. **uboot** accepts the complete slot supported by the running recovery interface. **firmware** accepts an OpenWrt FIT for the same model and partition layout.
4. Let a write finish. To exit without uploading, select **Reboot to System**. If the page starts an automatic reboot after flashing, wait for the system to return.

A web UBI rebuild erases the old configuration and overlay. Use the running OpenWrt upgrader to preserve settings and check migration status in the firmware guide. The recovery page can write to the device and can provide DHCP, so keep the recovery network isolated.

### Read-only backups

Select the MTD partition or UBI volume in the backup section. Store backups and checksums outside the device together with the model, version and layout. Backups can contain device identifiers, calibration or configuration data. Confirm model-specific restore procedures before writing any of them.

## 备份 API / Backup API

The web page uses these read-only endpoints. Use the returned size and identifier when requesting segments of up to 256 KiB.

```sh
curl http://192.168.0.1/api/storage
curl http://192.168.0.1/api/backup/mtd/spi-nand0
curl "http://192.168.0.1/api/download/<id>/0/262144" -o spi-nand0.part1.bin
```
