# K230 Flash

K230 Flash is a python tools to program Kendryte K230 and K230D Chips, Supports program firmware to `EMMC`, `SDCARD`, `SPI-NOR`, `SPI-NAND` and `OTP`

## Usage

```bash
k230_flash.exe --help
usage: k230_flash [-h] [-m {EMMC,SDCARD,SPINAND,SPINOR,OTP}] [-l] [-d DEVICE_ADDRESS] [--auto-reboot] [--log-level {TRACE,DEBUG,INFO,WARN,ERROR,CRITICAL,OFF}] [--custom-loader] [-la LOAD_ADDRESS] [-lf LOADER_FILE]  
                  [<address> <filename> ...]

Kendryte Burning Tool

positional arguments:
  <address> <filename>  Pairs of addresses followed by binary filenames, separated by space

options:
  -h, --help            show this help message and exit
  -m {EMMC,SDCARD,SPINAND,SPINOR,OTP}, --medium-type {EMMC,SDCARD,SPINAND,SPINOR,OTP}
                        Specify the medium type (choices: EMMC, SDCARD, SPI_NAND, SPI_NOR, OTP)
  -l, --list-device     List devices
  -d DEVICE_ADDRESS, --device-address DEVICE_ADDRESS
                        Device address (format: 1-1 or 3-1), which is the result get from '--list-device'
  --auto-reboot         Enable automatic reboot.
  --log-level {TRACE,DEBUG,INFO,WARN,ERROR,CRITICAL,OFF}
                        Set the logging level, Default is WARN

Custom Loader Options:
  Options related to the custom loader

  --custom-loader       If set, pass a file for the custom loader
  -la LOAD_ADDRESS, --load-address LOAD_ADDRESS
                        Hexadecimal load address (must be between 0x80300000 and 0x80400000), Default is 0x80360000
  -lf LOADER_FILE, --loader-file LOADER_FILE
                        Path to the custom loader file (required if --custom-loader is set)

# Usually just specify the medium type and file address and file path
k230_flash -m EMMC 0 file_0x00.img
```
