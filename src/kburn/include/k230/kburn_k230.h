#pragma once

#include "kburn.h"
#include <fstream>

namespace Kendryte_Burning_Tool {

namespace K230 {

#define RETRY_MAX (5)
#define USB_TIMEOUT (1000)

#define KENDRYTE_OUT_ENDPOINT (0x01)
#define KENDRYTE_IN_ENDPOINT (0x81)

#define U32_HIGH_U16(addr) ((addr) >> 16)
#define U32_LOW_U16(addr) ((addr) & 0xffff)

enum USB_KENDRYTE_REQUEST_BASIC {
  EP0_GET_CPU_INFO = 0,
  EP0_SET_DATA_ADDRESS = 1,
  EP0_SET_DATA_LENGTH = 2,
  // EP0_FLUSH_CACHES        = 3, // may not support ?
  EP0_PROG_START = 4,
};

class KBURN_API K230BROMBurner : public KBurner {
public:
  K230BROMBurner(struct kburn_usb_node *node) : KBurner(node) {}

  bool boot_from(uint64_t address = 0x80360000);

  bool get_loader(const char **loader, size_t *size);

  bool write(const void *data, size_t size, uint64_t address = 0x80360000);
  bool write_stream(std::ifstream& file_stream, size_t size, uint64_t address, uint64_t max, uint64_t flag) {
    spdlog::error("brom burner, not support write stream");
    return false;
  }

private:
  bool k230_brom_set_data_addr(uint64_t address = 0x80360000);
  bool k230_brom_write_data_chunk(const uint8_t *data, size_t size);
};

struct kburn_medium_info {
  uint64_t capacity;
  uint64_t blk_size;
  uint64_t erase_size;
  uint64_t timeout_ms : 32;
  uint64_t wp : 8;
  uint64_t type : 7;
  uint64_t valid : 1;
};

struct kburn_t {
  struct kburn_usb_node *node;

  struct kburn_medium_info medium_info;

  char error_msg[128];

  int ep_in, ep_out;
  uint16_t ep_out_mps;
  uint64_t capacity;

  std::vector<uint8_t> rd_buffer;
};

class KBURN_API K230UBOOTBurner : public KBurner {
public:
  K230UBOOTBurner(struct kburn_usb_node *node);

  bool probe(void);
  bool reboot(void);

  struct kburn_medium_info *get_medium_info();

  // Getter methods for the bit-fields
  uint32_t get_timeout_ms() const {
    return static_cast<uint32_t>(kburn_.medium_info.timeout_ms);
  }
  uint8_t get_wp() const { return static_cast<uint8_t>(kburn_.medium_info.wp); }
  uint8_t get_type() const {
    return static_cast<uint8_t>(kburn_.medium_info.type);
  }
  uint8_t get_valid() const {
    return static_cast<uint8_t>(kburn_.medium_info.valid);
  }

  bool write(const void *data, size_t size, uint64_t address) {
    spdlog::error("uboot burner, not support write data");
    return false;
  }

  bool write_stream(std::ifstream& file_stream, size_t size, uint64_t address, uint64_t max, uint64_t flag);

  bool read(void *data, size_t size, uint64_t address);

  bool erase(uint64_t address, size_t size);

private:
  bool probe_succ = false;
  uint64_t out_chunk_size = 512;
  uint64_t in_chunk_size = 512;

  std::vector<uint8_t> wr_buffer;

  struct kburn_t kburn_;
};

KBURN_API bool k230_probe_device(struct kburn_usb_node *node);

KBURN_API KBurner *k230_request_burner(struct kburn_usb_node *node);

}; // namespace K230

}; // namespace Kendryte_Burning_Tool
