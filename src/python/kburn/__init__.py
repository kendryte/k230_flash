from __future__ import annotations

import _kburn

def _initialize():
    """
    Initialize the KBurn library.
    This function is automatically called when the module is imported.
    """
    try:
        _kburn._initialize()
    except Exception as e:
        raise RuntimeError("KBurn initialization failed") from e

# Perform initialization when the module is imported
_initialize()

# --- Logging Levels ---
LogLevel = _kburn.LogLevel

# --- Logging Functions ---
def log(msg, level : LogLevel = _kburn.LogLevel.INFO):
    _kburn.log(msg, level)

def set_log_level(level: LogLevel):
    """
    Set the log level for the KBurn library.

    :param level: Log level as an integer. 
                  Use `LogLevel` enum for more clarity.
    """
    _kburn.set_log_level(level)

def get_log_level() -> LogLevel:
    """
    Get the current log level of the KBurn library.

    :return: The current log level as an integer.
    """
    return _kburn.get_log_level()

def set_custom_logger(logger_func):
    """
    Set a custom logging function that integrates with spdlog.
    
    :param logger_func: A callable logger function that takes in log messages.
    """
    _kburn.set_custom_logger(logger_func)

# --- Device Listing ---
KBurnUSBDeviceInfo = _kburn.KBurnUSBDeviceInfo
KBurnUSBDeviceInfoList = _kburn.KBurnUSBDeviceInfoList

KBurnUSBDeviceType = _kburn.KBurnUSBDeviceType

def list_device(vid: int = 0x29f1, pid: int = 0x0230) -> KBurnUSBDeviceInfoList:
    """
    List connected USB devices filtered by Vendor ID (vid) and Product ID (pid).

    :param vid: Vendor ID (default 0x29f1)
    :param pid: Product ID (default 0x0230)
    :return: A `KBurnUSBDeviceInfoList` object containing matching devices.
    """
    return _kburn.list_device(vid, pid)

def get_device_type(dev : KBurnUSBDeviceInfo) -> KBurnUSBDeviceType:
    return _kburn.get_device_type(dev)

# --- Burner ---
K230BROMBurner = _kburn.K230BROMBurner
K230UBOOTBurner = _kburn.K230UBOOTBurner

KBurnMediumType = _kburn.KBurnMediumType
KburnMediumInfo = _kburn.KburnMediumInfo

def request_burner(info : KBurnUSBDeviceInfo):
    return _kburn.request_burner(info)

# --- Clean-up Resources ---
def _cleanup():
    """
    Manually clean up the KBurn library resources.
    Typically not needed unless you want to force an immediate cleanup.
    """
    _kburn._cleanup()

# --- Exports ---
__all__ = [
    "LogLevel",
    "log",
    "set_log_level",
    "get_log_level",
    "set_custom_logger",
    "KBurnUSBDeviceInfo",
    "KBurnUSBDeviceInfoList", 
    "KBurnUSBDeviceType",
    "list_device",
    "KBurnMediumType",
    "KburnMediumInfo",
    "K230BROMBurner",
    "K230UBOOTBurner",
    "request_burner",
    "_cleanup"
]
