#include "k230/kburn_k230.h"

#include <cstddef>

extern "C" {
  #include <generated.k230_loader_mmc.h>
  #include <generated.k230_loader_spi_nand.h>
  #include <generated.k230_loader_spi_nor.h>
}

namespace Kendryte_Burning_Tool {

namespace K230 {

bool K230BROMBurner::boot_from(uint64_t address) {
  spdlog::info("boot from {:#x}", address);

  uint32_t addr = static_cast<uint32_t>(address);

  int r = libusb_control_transfer(/* dev_handle    */   dev_node->handle,
                                    /* bmRequestType */ (uint8_t)(LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE),
                                    /* bRequest      */ EP0_PROG_START,
                                    /* wValue        */ U32_HIGH_U16(addr),
                                    /* wIndex        */ U32_LOW_U16(addr),
                                    /* Data          */ 0,
                                    /* wLength       */ 0,
                                    /* timeout       */ USB_TIMEOUT);

  if(LIBUSB_SUCCESS != r) {
    spdlog::error("usb control boot from address failed, {}({})", r, libusb_error_name(r));

    return false;
  }

  return true;
}

bool K230BROMBurner::get_loader(const char **loader, size_t *size) {
  size_t loader_size = 0;
  const char *loader_buffer = NULL;

  switch(_medium_type) {
    case KBURN_MEDIUM_OTP:
    case KBURN_MEDIUM_EMMC:
    case KBURN_MEDIUM_SDCARD: {
      loader_buffer = k230_loader_mmc;
      loader_size = k230_loader_mmc_size;
    } break;
    case KBURN_MEDIUM_SPI_NAND: {
      loader_buffer = k230_loader_spi_nand;
      loader_size = k230_loader_spi_nand_size;
    } break;
    case KBURN_MEDIUM_SPI_NOR: {
      loader_buffer = k230_loader_spi_nor;
      loader_size = k230_loader_spi_nor_size;
    } break;
    case KBURN_MEDIUM_INVAILD: {
      loader_buffer = NULL;
      loader_size = 0;
    } break;
  }

  *loader = loader_buffer;
  *size = loader_size;

  return 0x00 != loader_size;
}

bool K230BROMBurner::k230_brom_set_data_addr(uint64_t address)
{
  uint32_t addr = static_cast<uint32_t>(address);

  int r = libusb_control_transfer(/* dev_handle    */ dev_node->handle,
                                  /* bmRequestType */ LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE,
                                  /* bRequest      */ EP0_SET_DATA_ADDRESS,
                                  /* wValue        */ U32_HIGH_U16(addr),
                                  /* wIndex        */ U32_LOW_U16(addr),
                                  /* Data          */ 0,
                                  /* wLength       */ 0,
                                  /* timeout       */ USB_TIMEOUT);

  if(LIBUSB_SUCCESS != r) {
    spdlog::error("usb control set data address failed, {}({})", r, libusb_error_name(r));

    return false;
  }

  return true;
}

bool K230BROMBurner::k230_brom_write_data_chunk(const uint8_t *data, size_t size)
{
  int r, transferred_size, completed_transfer_size;

  transferred_size = static_cast<int>(size);

  r = libusb_bulk_transfer(/* dev_handle       */  dev_node->handle,
                            /* endpoint         */ KENDRYTE_OUT_ENDPOINT,
                            /* bulk data        */ const_cast<uint8_t *>(data),
                            /* bulk data length */ transferred_size,
                            /* transferred      */ &completed_transfer_size,
                            /* timeout          */ USB_TIMEOUT);

  if((LIBUSB_SUCCESS != r) || (completed_transfer_size != transferred_size)) {
    spdlog::error("usb bulk write data failed, {}({}), or {} != {}", \
      r, libusb_error_name(r), completed_transfer_size, transferred_size);

    return false;
  }

  return true;
}

bool K230BROMBurner::write(const void *data, size_t size, uint64_t address) {
#define K230_SRAM_PAGE_SIZE         (1000)

  uint32_t chunk_addr, chunk_size = K230_SRAM_PAGE_SIZE;
  uint8_t chunk_data[K230_SRAM_PAGE_SIZE];

  const uint8_t *buffer = static_cast<const uint8_t *>(data);
  const uint32_t pages = (size + K230_SRAM_PAGE_SIZE - 1) / K230_SRAM_PAGE_SIZE;

  spdlog::info("write {} to {:#x}, size {}", data, address, size);

  if (false == k230_brom_set_data_addr(address)) {
    return false;
  }

  for (uint32_t page = 0; page < pages; page++) {
    uint32_t offset = page * K230_SRAM_PAGE_SIZE;

    chunk_addr = static_cast<uint32_t>(address) + offset;

    if (offset + K230_SRAM_PAGE_SIZE > size) {
      chunk_size = size % K230_SRAM_PAGE_SIZE;
    } else {
      chunk_size = K230_SRAM_PAGE_SIZE;
    }

    memcpy(chunk_data, buffer + offset, chunk_size);

    if (false == k230_brom_write_data_chunk(chunk_data, chunk_size)) {
      return false;
    }

    log_progress(offset, size);
  }

  log_progress(size, size);

  return true;
}

} // namespace K230

}; // namespace Kendryte_Burning_Tool
