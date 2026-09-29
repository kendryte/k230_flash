# K230 Flash

[English](README_en.md) | [简体中文](README.md)

K230 Flash is a command-line tool for programming Kendryte K230 and K230D
chips. It supports programming firmware to `EMMC`, `SDCARD`, `SPI-NOR`,
`SPI-NAND`, and `OTP`.

## Command interface

```bash
k230_flash_cli --version
k230_flash_cli devices
k230_flash_cli flash 0x1000 bootloader.bin 0x8000 firmware.bin --medium-type EMMC
k230_flash_cli flash 0 image.img --medium-type SDCARD --verify
k230_flash_cli flash 0x100000 image.bin --medium-type SPI_NOR --loader loader.bin
k230_flash_cli read --read-file backup.bin --address 0 --size 0x100000
k230_flash_cli erase --address 0 --size 0x20000 --medium-type SPI_NAND
```

Use `--version` to print the CLI version and build commit ID. Common options
are `--device-address PATH`, `--medium-type TYPE`, `--log-level LEVEL`, and
`--auto-reboot`. The `flash` command also accepts `--verify`. Medium names are
`EMMC`, `SDCARD`, `SPI_NAND`, `SPI_NOR`, and `OTP`.

`devices` prints each board's full physical USB port path. Directly connected
devices use paths such as `1-1`; devices behind hubs use paths such as
`1-5.3.2`. Pass that complete path to `--device-address` when selecting a board.
Devices without an available physical USB path are skipped because they cannot
be selected reliably across re-enumeration.

Flash inputs use `ADDRESS FILE` pairs. Numeric values accept decimal or `0x`
notation. `--loader` selects a custom loader for the device operation;
`--loader-address` may be used to change its load address. These options can be
used with `flash`, `read`, and `erase`. Raw image addresses must be aligned to
the selected medium's erase size. Automatic reboot after a successful write is
opt-in with `--auto-reboot`.

Pass `--verify` to hash the exact padded bytes sent by the host, read the
written range back on the device, and compare SHA-256 digests before reporting
success or rebooting. Verification is optional because it adds a complete
medium read pass. SPI NAND writes containing explicit OOB data do not support
this mode.

## Release packages

Prebuilt packages are provided for each platform with matching `.sha256`
checksum files. End users can download and run the package for their platform.

Developers who need to build or publish a release should see the
[build guide](BUILD.md).
