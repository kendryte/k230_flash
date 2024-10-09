#include "k230/kburn_k230.h"

namespace Kendryte_Burning_Tool {

namespace K230 {

#define USB_TIMEOUT (1000)

static int usb_control_get_chip_info(libusb_device_handle *dev_hndl, char info[32])
{
    memset(info, 0, 32);

    int r = libusb_control_transfer(/* dev_handle    */ dev_hndl,
                                    /* bmRequestType */ (uint8_t)(LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE),
                                    /* bRequest      */ 0 /* ISP_STAGE1_CMD_GET_CPU_INFO */,
                                    /* wValue        */ (uint16_t)(((0) << 8) | 0x00),
                                    /* wIndex        */ 0,
                                    /* Data          */ (unsigned char *)info,
                                    /* wLength       */ 32,
                                    /* timeout       */ USB_TIMEOUT);

    if (r < LIBUSB_SUCCESS) {
        spdlog::error("read cpu info failed, {}({})", r, libusb_error_name(r));

        return -1;
    }

    return r;
}

bool k230_probe_device(struct kburn_usb_node *node)
{
    char info[32];
    int size = 0, retry = 5;

    node->info.type = KBURN_USB_DEV_INVALID;

    do {
        if(0 < (size = usb_control_get_chip_info(node->handle, info))) {
            break;
        } else {
            spdlog::error("read chip info failed, device vid 0x{:04x} pid 0x{:04x} path {}", node->info.vid, node->info.pid, node->info.path);
            do_sleep(100);
        }
    } while(--retry);

    spdlog::debug("get chip info '{}', device vid 0x{:04x} pid 0x{:04x} path {}", info, node->info.vid, node->info.pid, node->info.path);

    if(0x00 == memcmp(info, "Uboot Stage for K230", size)) {
        node->info.type = KBURN_USB_DEV_UBOOT;

        spdlog::debug("chip is uboot device");

        return true;
    } else if(0x00 == memcmp(info, "K230", size)) {
        node->info.type = KBURN_USB_DEV_BROM;

        spdlog::debug("chip is brom device");

        return true;
    }

    spdlog::debug("unknown chip mode");

    return false;
}

}
};
