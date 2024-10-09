import sys
import time
import argparse

import kburn

# Define constants for medium types
KBURN_MEDIUM_EMMC = "EMMC"
KBURN_MEDIUM_SDCARD = "SDCARD"
KBURN_MEDIUM_SPI_NAND = "SPINAND"
KBURN_MEDIUM_SPI_NOR = "SPINOR"
KBURN_MEDIUM_OTP = "OTP"

def get_burn_medium_type(medium_str):
    medium_map = {
        "INVALID": kburn.KBurnMediumType.INVALID,
        "EMMC": kburn.KBurnMediumType.EMMC,
        "SDCARD": kburn.KBurnMediumType.SDCARD,
        "SPINAND": kburn.KBurnMediumType.SPINAND,
        "SPINOR": kburn.KBurnMediumType.SPINOR,
        "OTP": kburn.KBurnMediumType.OTP
    }
    return medium_map.get(medium_str.upper(), kburn.KBurnMediumType.EMMC)  # Default to INVALID if not found

def get_log_level(level_str):
    level_map = {
        "TRACE": kburn.LogLevel.TRACE,
        "DEBUG": kburn.LogLevel.DEBUG,
        "INFO": kburn.LogLevel.INFO,
        "WARN": kburn.LogLevel.WARN,
        "ERROR": kburn.LogLevel.ERROR,
        "CRITICAL": kburn.LogLevel.CRITICAL,
        "OFF": kburn.LogLevel.OFF
    }
    return level_map.get(level_str.upper(), kburn.LogLevel.WARN)  # Default to INFO if not found

def valid_custom_loader_address(value):
    try:
        # Convert to integer, assuming the input is in hexadecimal
        address = int(value, 16)
    except ValueError:
        raise argparse.ArgumentTypeError(f"Invalid hex address: {value}")

    # Check if the address is within the valid range
    if not (0x80300000 <= address <= 0x80400000):
        raise argparse.ArgumentTypeError(f"Address {value} out of range. Must be between 0x80200000 and 0x80400000.")

    return address

class AddrFilenamePairAction(argparse.Action):
    """Custom action for parsing pairs of address and binary filename arguments."""

    def __init__(self, option_strings, dest, nargs="+", **kwargs):
        super(AddrFilenamePairAction, self).__init__(option_strings, dest, nargs, **kwargs)
        self.max_end_address = 0

    def __call__(self, parser, namespace, values, option_string=None):
        """Process the input values and validate address/filename pairs."""
        pairs = []

        # Process pairs of address and filename
        for i in range(0, len(values), 2):
            # Ensure we have pairs of inputs
            if i + 1 >= len(values):
                raise argparse.ArgumentError(
                    self,
                    "Each address must be followed by a corresponding binary filename."
                )

            # Validate address
            try:
                address = int(values[i], 0)
            except ValueError:
                raise argparse.ArgumentError(
                    self,
                    f'Invalid address "{values[i]}": must be a valid integer.'
                )

            # Validate filename and open the file
            filename = values[i + 1]
            try:
                with open(filename, "rb") as f:
                    pairs.append((address, filename))
            except FileNotFoundError:
                raise argparse.ArgumentError(self, f'File "{filename}" not found.')
            except IOError as e:
                raise argparse.ArgumentError(self, f'Error opening file "{filename}": {e}')

        # Sort the pairs by address and check for overlapping addresses
        self.check_for_overlaps(pairs)

        # Store the validated pairs in the namespace
        setattr(namespace, self.dest, pairs)
        setattr(namespace, 'max_end_address', self.max_end_address)  # Store max end address in namespace

    def check_for_overlaps(self, pairs):
        """Check for overlapping address ranges in the pairs."""
        end = 0
        for address, filename in sorted(pairs, key=lambda x: x[0]):
            with open(filename, "rb") as f:
                # Determine the size of the binary file
                f.seek(0, 2)  # Seek to end to get size
                size = f.tell()
                f.seek(0)  # Reset seek to start for further processing

            # Calculate sector boundaries
            sector_start = address & ~(4096 - 1)
            sector_end = ((address + size + 4096 - 1)& ~(4096 - 1)) - 1

            # Check for overlapping sectors
            if sector_start < end:
                message = f"Detected overlap at address: 0x{address:x} for file: {filename}"
                raise argparse.ArgumentError(self, message)

            end = sector_end
            self.max_end_address = max(self.max_end_address, end)  # Update max end address

def open_device(path=None, checkisUboot=False) -> kburn.KBurnUSBDeviceInfo:
    # Get the list of available devices
    device_list = kburn.list_device()

    # Check if a specific path is provided
    if path:
        # Attempt to open the device at the given path
        for dev in device_list:
            if dev.path == path:
                # If checkisUboot is enabled, ensure the device is of type UBOOT
                if checkisUboot:
                    if dev.type == kburn.KBurnUSBDeviceType.UBOOT:
                        return dev
                else:
                    return dev

        # If the path does not match any devices, raise an exception
        raise ValueError(f"No device found at path: {path}")

    # If no path is provided, open the first available device if it matches the criteria
    for dev in device_list:
        # If checkisUboot is enabled, only return devices of type UBOOT
        if checkisUboot:
            if dev.type == kburn.KBurnUSBDeviceType.UBOOT:
                return dev
        else:
            return dev

    # If there are no devices available, raise an exception
    raise RuntimeError("No devices available to open.")

def poll_and_open_device(path=None, checkisUboot = False, poll_interval=2, timeout=None) -> kburn.KBurnUSBDeviceInfo:
    """
    Polls the USB devices on the host at regular intervals and opens the first device found.
    If a specific `path` is given, it will open the device at that path when it becomes available.
    
    :param path: The device path to open (optional)
    :param poll_interval: The time in seconds between each poll
    :param timeout: The maximum time to poll before giving up (None for no timeout)
    :return: The opened KBurnUSBDeviceInfo object
    """
    start_time = time.time()

    while True:
        try:
            # Try to open a device (uses previously defined open_device function)
            device = open_device(path, checkisUboot)
            print(f"Device found and opened: {device}")
            return device

        except ValueError as e:
            # Raised if the path is not found
            print(f"Error: {e}")

        except RuntimeError as e:
            # Raised if no devices are present
            print("No devices found. Polling...")

        # Check if we should timeout
        if timeout and (time.time() - start_time) > timeout:
            raise TimeoutError(f"Timeout reached while polling for device at path: {path or 'any device'}")

        # Wait for the specified poll interval before trying again
        time.sleep(poll_interval)

def read_file(file) -> bytes:
    try:
        with open(file, 'rb') as f:
            data = f.read()

        return data
    except FileNotFoundError:
        print(f"Error: File {file} not found.")
    except IOError as e:
        print(f"Error reading file {file}: {e}")

    return None

progress_start_time = 0

def print_progress(iteration, total):
    global progress_start_time

    if iteration == 0:
        progress_start_time = time.time()

    # Calculate percentage completion
    percent = (iteration / total) * 100

    # Create the progress bar
    bar = '█' * int(percent // 2) + '-' * (50 - int(percent // 2))
    
    # Calculate elapsed time
    elapsed_time = time.time() - progress_start_time

    # Calculate speed in iterations per second
    speed = iteration / 1024 / elapsed_time if elapsed_time > 0 else 0
    
    # Display the progress bar, percent complete, and speed
    sys.stdout.write(f'\r|{bar}| {percent:.2f}% Complete - Speed: {speed:.2f} KB/s')
    sys.stdout.flush()

    # Check if the iteration is complete
    if iteration >= total:
        sys.stdout.write("\r\n")
        sys.stdout.flush()

def main():
    # Create an ArgumentParser object
    parser = argparse.ArgumentParser(description="Kendryte Burning Tool")

    # Add arguments with short names
    parser.add_argument(
        '-m', '--medium-type',
        choices=[
            KBURN_MEDIUM_EMMC,
            KBURN_MEDIUM_SDCARD,
            KBURN_MEDIUM_SPI_NAND,
            KBURN_MEDIUM_SPI_NOR,
            KBURN_MEDIUM_OTP,
        ],
        default="EMMC",
        help="Specify the medium type (choices: EMMC, SDCARD, SPI_NAND, SPI_NOR, OTP)"
    )

    parser.add_argument(
        '-l', '--list-device',
        action='store_true',
        help="List devices"
    )

    parser.add_argument(
        '-d', '--device-address',
        type=str,
        default=None,
        help="Device address (format: 1-1 or 3-1), which is the result get from '--list-device'"
    )

    parser.add_argument(
        '--auto-reboot', 
        action='store_true',
        default=True,
        help="Enable automatic reboot. Default is True."
    )

    parser.add_argument(
        "--log-level",
        default="WARN",
        choices=["TRACE", "DEBUG", "INFO", "WARN", "ERROR", "CRITICAL", "OFF"],
        help="Set the logging level, Default is WARN"
    )

    # Create a group for custom loader options
    custom_loader_group = parser.add_argument_group(
        'Custom Loader Options',
        'Options related to the custom loader'
    )

    # Add the three options to the custom loader group
    custom_loader_group.add_argument(
        '--custom-loader',
        action='store_true',
        help="If set, pass a file for the custom loader"
    )

    custom_loader_group.add_argument(
        '-la', '--load-address',
        type=valid_custom_loader_address,
        default=0x80360000,
        help="Hexadecimal load address (must be between 0x80300000 and 0x80400000), Default is 0x80360000"
    )

    custom_loader_group.add_argument(
        '-lf', '--loader-file',
        type=str,
        help="Path to the custom loader file (required if --custom-loader is set)"
    )

    parser.add_argument(
        'addr_filename',
        metavar='<address> <filename>',
        help='Pairs of addresses followed by binary filenames, separated by space',
        action=AddrFilenamePairAction,
        nargs='*'  # Allow zero or more pairs
    )

    # Parse the arguments
    args = parser.parse_args()

    medium_type = get_burn_medium_type(args.medium_type)

    log_level = get_log_level(args.log_level)
    kburn.set_log_level(log_level)

    if args.list_device:
        dev_list = kburn.list_device()

        print(f"Available Device: {dev_list.size()}")
        for dev in dev_list:
            print(f"\t{dev}")

        sys.exit()

    if len(args.addr_filename) == 0:
        parser.error("the following arguments are required: <address> <filename>")

    # Validate loader file if custom loader is specified
    if args.custom_loader:
        valid_custom_loader_address(args.load_address)

        if not args.loader_file:
            parser.error("--loader-file is required when --custom-loader is set")

    # start
    dev = poll_and_open_device(args.device_address)

    print(f"use device {dev}")

    if dev.type == kburn.KBurnUSBDeviceType.BROM:
        brom_burner = kburn.request_burner(dev)

        if not brom_burner:
            print("fatal error")
            sys.exit()
        brom_burner.set_custom_progress(print_progress)

        loader = None
        if args.custom_loader and args.loader_file:
            loader = read_file(args.loader_file)
        else:
            brom_burner.set_medium_type(medium_type)
            loader = brom_burner.loader

        if loader is None:
            print("fatal error")
            sys.exit()

        if False == brom_burner.write(loader, args.load_address):
            print("fatal error, write loader failed.")
            sys.exit()

        brom_burner.boot(args.load_address)
        del brom_burner

        time.sleep(1) # wait device reboot
        try:
            dev_path = dev.path

            dev = poll_and_open_device(dev_path, True, timeout = 30)
        except TimeoutError as e:
            print("wait device enter uboot stage timeout")
            sys.exit()

    if dev.type == kburn.KBurnUSBDeviceType.UBOOT:
        uboot_burner = kburn.request_burner(dev)

        if not uboot_burner:
            print("fatal error")
            sys.exit()
        uboot_burner.set_custom_progress(print_progress)

        uboot_burner.set_medium_type(medium_type)

        if False == uboot_burner.probe():
            print("Can't probe medium as configure")
            sys.exit()

        if uboot_burner.medium_info.capacity < args.max_end_address:
            print("fatal error, files excees the medium capacity")
            sys.exit()

        for address, file in args.addr_filename:
            print(f'Write File: {file} to Address: {hex(address)}')

            data = read_file(file)
            if data is None:
                sys.exit()

            uboot_burner.write(data, address)

        if args.auto_reboot:
            uboot_burner.reboot()

        del uboot_burner

        print("Write done.")

if __name__ == "__main__":
    main()
