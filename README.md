# K230 Flash

[English](README_en.md) | [简体中文](README.md)

K230 Flash 是一个用于给 Kendryte K230 和 K230D 芯片烧录固件的命令行工具，支持
`EMMC`、`SDCARD`、`SPI-NOR`、`SPI-NAND` 和 `OTP`。

## 命令接口

```bash
k230_flash_cli devices
k230_flash_cli flash 0x1000 bootloader.bin 0x8000 firmware.bin --medium-type EMMC
k230_flash_cli flash 0x100000 image.bin --medium-type SPI_NOR --loader loader.bin
k230_flash_cli read --read-file backup.bin --address 0 --size 0x100000
k230_flash_cli erase --address 0 --size 0x20000 --medium-type SPI_NAND
```

常用选项包括 `--device-address PATH`、`--medium-type TYPE`、
`--log-level LEVEL` 和 `--auto-reboot`。介质名称包括 `EMMC`、`SDCARD`、
`SPI_NAND`、`SPI_NOR` 和 `OTP`。

烧录文件使用 `地址 文件` 参数对。数字参数支持十进制和 `0x` 前缀的十六进制格式。
使用 `--loader` 指定设备操作使用的自定义 loader，可以使用 `--loader-address`
修改其加载地址。这些选项可用于 `flash`、`read` 和 `erase` 命令。
裸镜像的地址必须按照所选介质的擦除大小对齐。成功烧录后不会自动重启，
如需自动重启请使用 `--auto-reboot`。

## 发布包

预编译发布包按平台提供，并带有对应的 `.sha256` 校验文件。普通用户直接
下载并运行适合目标平台的发布包即可。

需要从源码构建或发布版本的开发者请阅读
[构建与发布指南](BUILD.md)。
