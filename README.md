# K230 Flash

K230 Flash is a cli tools to program Kendryte K230 and K230D Chips, Supports program firmware to `EMMC`, `SDCARD`, `SPI-NOR`, `SPI-NAND` and `OTP`

## Usage

```bash
k230_flash_cli --help
Kendryte Burning Tool
Usage: ./k230_flash_cli [OPTIONS]

Options:
  -h,--help                   Print this help message and exit
  --auto-reboot               Enable automatic reboot after flashing.
  -l,--list-device            List connected devices
  -d,--device-address TEXT    Device address (format: 1-1 or 3-1), which is the result from '--list-device'
  -m,--medium-type ENUM:value in {EMMC->1,OTP->5,SDCARD->2,SPI_NAND->3,SPI_NOR->4} OR {1,5,2,3,4} [EMMC] 
                              Specify the medium type
  --log-level ENUM:value in {CRITICAL->5,DEBUG->1,ERROR->4,INFO->2,OFF->6,TRACE->0,WARN->3} OR {5,1,4,2,6,0,3} [WARN] 
                              Set the logging level
  -a,--address UINT:NUMBER [0] 
                              The address where write data starts
  -f,--file TEXT              The path of data write to medium
[Option Group: Custom Loader Options]
  Options related to the custom loader
  Options:
    --custom-loader             Enable use custom loader
    --load-address UINT:address between 0x80300000 - 0x80400000 [0x80360000] 
                                The address where will loader run
    --loader-file TEXT          Path to the custom loader file
[Option Group: Read data Options]
  Options related to reading data from the device
  Options:
    --read-data                 Read data from the device.
    --read-address UINT:NUMBER [0x00] 
                                The address where reading data starts
    --read-size UINT:NUMBER [4096] 
                                The size of the data to read
    --read-file TEXT [data.bin] 
                                The path where read data will be saved, Default data.bin
[Option Group: Erase Medium Options]
  Options related to the medium erase
  Options:
    --erase-medium              Erase the medium.
    --erase-address UINT:NUMBER [0x00] 
                                The address where erase medium starts
    --erase-size UINT:NUMBER [0x00] 
                                The size of the meidum to erase
```
