# K230 Flash

[English](README_en.md) | [简体中文](README.md)

K230 Flash 是一个用于给 Kendryte K230 和 K230D 芯片烧录固件的命令行工具，支持
`EMMC`、`SDCARD`、`SPI-NOR`、`SPI-NAND` 和 `OTP`。

## 命令接口

```bash
k230_flash_cli --version
k230_flash_cli devices
k230_flash_cli flash 0x1000 bootloader.bin 0x8000 firmware.bin --medium-type EMMC
k230_flash_cli flash 0 image.img --medium-type SDCARD --verify
k230_flash_cli flash 0x100000 image.bin --medium-type SPI_NOR --loader loader.bin
k230_flash_cli read --read-file backup.bin --address 0 --size 0x100000
k230_flash_cli erase --address 0 --size 0x20000 --medium-type SPI_NAND
```

使用 `--version` 可显示 CLI 版本和构建对应的 commit ID。常用选项包括
`--device-address PATH`、`--medium-type TYPE`、`--log-level LEVEL` 和
`--auto-reboot`。`flash` 命令还支持 `--verify`。介质名称包括 `EMMC`、
`SDCARD`、`SPI_NAND`、`SPI_NOR` 和 `OTP`。

`devices` 会显示每块开发板的完整物理 USB 端口路径。直连设备的路径类似
`1-1`，经过 USB Hub 的设备路径类似 `1-5.3.2`。选择开发板时，请将该完整
路径传给 `--device-address`。
如果系统无法提供设备的物理 USB 路径，`devices` 会跳过该设备，因为设备重新
枚举后无法可靠地再次选择它。

烧录文件使用 `地址 文件` 参数对。数字参数支持十进制和 `0x` 前缀的十六进制格式。
使用 `--loader` 指定设备操作使用的自定义 loader，可以使用 `--loader-address`
修改其加载地址。这些选项可用于 `flash`、`read` 和 `erase` 命令。
裸镜像的地址必须按照所选介质的擦除大小对齐。成功烧录后不会自动重启，
如需自动重启请使用 `--auto-reboot`。

使用 `--verify` 时，主机会计算实际发送数据（包括对齐填充）的 SHA-256，
loader 会从介质完整回读烧录范围并计算 SHA-256，只有摘要一致才会报告成功或
执行自动重启。该功能会增加一次完整介质读取，因此默认关闭。包含显式 OOB
数据的 SPI NAND 写入暂不支持此模式。

## 发布包

预编译发布包按平台提供，并带有对应的 `.sha256` 校验文件。普通用户直接
下载并运行适合目标平台的发布包即可。

需要从源码构建或发布版本的开发者请阅读
[构建与发布指南](BUILD.md)。
