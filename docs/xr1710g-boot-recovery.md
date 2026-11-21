# XR1710G 自动启动恢复

[English](xr1710g-boot-recovery.en.md) · [网页恢复](recovery-web.md)

配套的二级 U-Boot 与 XR1710G Full Legacy AP 固件通过独立 UBI 状态卷记录启动进度。启用后，启动未确认且发生复位时，可转到下一级恢复环境。

## 启动与恢复

| 阶段 | 触发条件 | 接入方式 |
| --- | --- | --- |
| 正常 OpenWrt | 由原生生产路径启动。开机后 120 秒内确认当前启动。 | 使用已有配置的管理地址。确认完成后回到常规喂狗。 |
| OpenWrt failsafe | 连续两次正常启动在确认前发生复位。自动 failsafe 的就绪窗口为开机后 60 秒。 | 直连健康网口，电脑设静态地址，通过 SSH 访问 `192.168.1.1:22`。 |
| 二级 U-Boot 网页恢复 | failsafe 在确认前也发生复位。二级先消费失败记录，再启动网页。 | 访问 `http://192.168.0.1/`，按[网页恢复指南](recovery-web.md)操作。 |

正常启动确认检查本机可写 UBIFS overlay、管理接口地址、Dropbear，以及启用 AP 时的 hostapd。确认当前启动的状态是一项本机就绪检查。它无法判定长时间运行后出现的所有应用故障，也无法代替外部网络连通性验证。

## 主动进入网页恢复

在已确认的配套 OpenWrt 中，先准备隔离管理通路，再以 root 执行一次：

```sh
reboot-to-uboot
```

命令需要本次引导提供一次性请求能力以及可写的状态卷。二级校验并消费请求后启动网页。维护结束后，点击 **Reboot to System** 回到常规引导；普通 `reboot` 沿用原来的正常启动流程。无法登录 OpenWrt 时，可按[机型指南](flashing-xr1710g.md)使用硬件恢复入口。具体刷写及正常回程见[网页恢复](recovery-web.md)。

## 兼容性和状态卷

| 配对 | 行为 |
| --- | --- |
| 新二级 U-Boot + 原始 Full Legacy AP FIT + 可写状态卷 | 启用分级恢复和一次性请求。 |
| 新二级 U-Boot + 已兼容的无标记固件 | 按原方式启动，仍可使用支持的硬件恢复入口。 |
| 旧二级 U-Boot + 已兼容的配套 Full AP FIT | 缺少运行时握手时沿用普通启动。 |
| 配对兼容但状态卷不可用 | 沿用普通启动，自动恢复和一次性请求不可用。 |

配套路径从原生 `system` 分区的 `fit` 卷读取 `config-1`。固件端把该 UBI 分区命名为 `ubi`。自动协议适用于 XR1710G Full Legacy AP。手工 `bootm`、网络临时引导、带 `gemtek,xr1710g-ubi` 兼容别名的 FIT、网关镜像和 XG2010G 不会启用这项策略。

状态卷为单个 LEB 的动态卷 `xr_boot_state`。存在空闲 LEB 时，引导器可建立它。网页用有有效协议标记的 AP FIT 重建系统 UBI 时，会在分配 `rootfs_data` 前预留状态卷。旧系统保配置迁移见[固件操作指南](https://github.com/DictXiong/ImmortalWrt-for-Gemtek-brightspeed/blob/master/docs/xr1710g-ap-operations.md#保留配置升级)。

## 网口与故障边界

OpenWrt failsafe 尝试四个网口，隔离并启用可用端口。U-Boot 的默认自动模式也覆盖 WAN、10G LAN 和两个 1G LAN。发生可隔离的 WAN DMA 初始化超时、并确认 DMA 已禁用时，U-Boot 可以继续提供 LAN 网页恢复。指定单一端口时按所选策略工作。网口有链路灯不代表已能收发数据，串口诊断和共享控制器故障仍需按实际情况处理。

在隔离样机上曾验证分级恢复、一次性入口和回到系统。四口、故障注入和镜像配对的测试覆盖以各自的验收记录为准；共享 FE、交换机、NAND 或引导链本身失效时，可能仍需串口或现场恢复。U-Boot 恢复页默认提供 DHCP，OpenWrt failsafe 接受无密码 root SSH，两种环境均应使用隔离网络。

## 协议

- 配套 FIT 的 `config-1` 带 `dictxiong,boot-recovery-protocol = <1>` 与 64 个小写十六进制字符的 `dictxiong,boot-recovery-image`。二级在启动 Linux 前写入状态，在 `/chosen` 传递本轮镜像 ID、非零序号和模式，并配置看门狗。
- `xr_boot_state` 的 LEB 0 起始存放 48 字节记录，字段依次为 `XRR1`、版本、模式、正常尝试数、状态、32 位大端序号、32 字节镜像 ID、覆盖前 44 字节的 IEEE CRC32。使用 UBI 原子 LEB 更新。一次性请求使用保留的模式和状态组合，消费后转换为普通记录。
- Linux 接管前硬件看门狗窗口为 24 秒。内核在用户空间打开设备前最多代喂 30 秒。就绪确认匹配当前镜像、序号和模式；NAND 记录已提交但内存通知丢失时，用户空间在期限处异步复核，只有当前 READY 匹配才恢复喂狗。
