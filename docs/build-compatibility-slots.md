# 构建特定引导器的兼容槽位

以下构建只适用于具有对应原生 NAND 槽位布局的 XR1710G。第三方引导器的上传校验和槽位格式各不相同，迁移前须确认正在运行的引导器与版本。

[普通构建](../README.md#构建)生成本仓标准槽位。本页的输入用来保持指定上传入口兼容，只替换外层 FIT 内的二级载荷。

## WIRO Recovery 2.1

从 [WIRO v2.02 发布下载 XR1710G 2.1 原始槽位](https://github.com/orangeyoo/XR1710G-OpenWrt-iStoreOS-Community/releases/download/v2.02/xr1710g-wiro-uboot-v2.1-flash-slot.bin)。其公开 SHA256 为 `c6a3742369082d21feb2b4081b94e978d06fd1e5ac77e587202b4af356912561`。[WIRO 2.1 源码](https://github.com/orangeyoo/XR1710G-OpenWrt-iStoreOS-Community/tree/a9ba7603eae0d0374f0f00d3010cb4f8a696012f) 中的前缀、shim 和控制 DTB 会原样保留，只替换二级 U-Boot 和其 FIT 哈希。

```sh
./scripts/build.sh xr1710g
mkdir -p out
python3 chainloader/build-wiro21-slot.py xr1710g-wiro-uboot-v2.1-flash-slot.bin \
  output/xr1710g/u-boot.bin out/xr1710g-wiro-2.1-compatible-slot.bin
```

脚本需要 Python 3，验证上游槽位 SHA256、目标载荷的 Git 提交标识与槽位大小，并输出原始长度及 1 MiB 擦除态填充后的各自 SHA256。不需要从设备导出原槽。将生成的原始长度槽位上传到 WIRO 恢复页的 U-Boot 上传项。通过 WIRO 2.1 校验器不代表包适用于其他引导器或布局。

## English

Use the [standard build](../README.en.md#build) unless the bootloader currently installed on your device requires a different upload format. For WIRO Recovery 2.1, use the pinned public input and command above. The tool keeps the upstream prefix, shim and control DTB and replaces only the embedded second-stage payload and its hash. Upload the raw-length compatibility slot to the WIRO U-Boot target. Validate its size and checksum first. Firmware FITs are selected separately, as described in [web recovery](recovery-web.md).
