#include "k230/kburn_k230.h"

namespace Kendryte_Burning_Tool {

namespace K230 {

KBurner *k230_request_burner(struct kburn_usb_node *node) {
  KBurner *burner = nullptr;

  if (KBURN_USB_DEV_BROM == node->info.type) {
    burner = new K230BROMBurner(node);
  } else if(KBURN_USB_DEV_UBOOT == node->info.type) {
    burner = new K230UBOOTBurner(node);
  }

  if (!burner) {
    spdlog::error("request burner for vid 0x{:04x} pid 0x{:04x} path {} failed",
                  node->info.vid, node->info.pid, node->info.path);
  }

  return burner;
}

} // namespace K230

}; // namespace Kendryte_Burning_Tool
