#include "k230/kburn_k230.h"
#include <memory>

namespace Kendryte_Burning_Tool {

namespace K230 {

#define CMD_FLAG_DEV_TO_HOST (0x8000)

enum kburn_pkt_cmd {
  KBURN_CMD_NONE = 0,
  KBURN_CMD_REBOOT = 0x01,

  KBURN_CMD_DEV_PROBE = 0x10,
  KBURN_CMD_DEV_GET_INFO = 0x11,

  KBURN_CMD_WRITE_LBA = 0x20,
  KBURN_CMD_ERASE_LBA = 0x21,

  KBURN_CMD_MAX,
};

enum kburn_pkt_result {
  KBURN_RESULT_NONE = 0,

  KBURN_RESULT_OK = 1,
  KBURN_RESULT_ERROR = 2,

  KBURN_RESULT_ERROR_MSG = 0xFF,

  KBURN_RESULT_MAX,
};

#define KBUNR_USB_PKT_SIZE (64)

#pragma pack(push, 1)

struct kburn_usb_pkt {
  uint16_t cmd;
  uint16_t result; /* only valid in csw */
  uint8_t data_size;
};

struct kburn_usb_pkt_wrap {
  struct kburn_usb_pkt hdr;
  uint8_t data[KBUNR_USB_PKT_SIZE - sizeof(struct kburn_usb_pkt)];
};

#pragma pack(pop)

uint64_t round_down(uint64_t value, uint64_t multiple) {
    return value - (value % multiple);
}

uint64_t round_up(uint64_t value, uint64_t multiple) {
    return (value + multiple - 1) / multiple * multiple;
}

static int __get_endpoint(kburn_t *kburn) {
  const struct libusb_interface_descriptor *setting;
  const struct libusb_endpoint_descriptor *ep;
  struct libusb_config_descriptor *config;
  const struct libusb_interface *intf;
  int if_idx, set_idx, ep_idx, ret;
  struct libusb_device *udev;
  uint8_t trans, dir;

  udev = libusb_get_device(kburn->node->handle);

  if (LIBUSB_SUCCESS != (ret = libusb_get_active_config_descriptor(udev, &config))) {
    spdlog::error("libusb_get_active_config_descriptor failed {}({})", ret, libusb_strerror(ret));

    return ret;
  }

  for (if_idx = 0; if_idx < config->bNumInterfaces; if_idx++) {
    intf = config->interface + if_idx;

    for (set_idx = 0; set_idx < intf->num_altsetting; set_idx++) {
      setting = intf->altsetting + set_idx;

      for (ep_idx = 0; ep_idx < setting->bNumEndpoints; ep_idx++) {
        ep = setting->endpoint + ep_idx;
        trans = (ep->bmAttributes & LIBUSB_TRANSFER_TYPE_MASK);
        if (trans != LIBUSB_TRANSFER_TYPE_BULK)
          continue;

        dir = (ep->bEndpointAddress & LIBUSB_ENDPOINT_DIR_MASK);
        if (dir == LIBUSB_ENDPOINT_IN)
          kburn->ep_in = ep->bEndpointAddress;
        else
          kburn->ep_out = ep->bEndpointAddress;
      }
    }
  }

  libusb_free_config_descriptor(config);

  return LIBUSB_SUCCESS;
}

static bool kburn_write_data(kburn_t *kburn, void *data, int length) {
  int rc = -1, size = 0;

  // if(length <= 64) {
  //     print_buffer(KBURN_LOG_ERROR, "usb write", data, length);
  // }

  rc = libusb_bulk_transfer(
      /* dev_handle       */ kburn->node->handle,
      /* endpoint         */ kburn->ep_out,
      /* bulk data        */ reinterpret_cast<uint8_t *>(data),
      /* bulk data length */ length,
      /* transferred      */ &size,
      /* timeout          */ kburn->medium_info.timeout_ms);

  if ((rc != LIBUSB_SUCCESS) || (size != length)) {
    spdlog::error("usb bulk write data failed, {}({}), or {} != {}", rc,
                  libusb_error_name(rc), size, length);

    return false;
  }

  return true;
}

static bool kburn_read_data(kburn_t *kburn, void *data, int length,
                            int *is_timeout) {
  int rc = -1, size = 0;

  rc = libusb_bulk_transfer(
      /* dev_handle       */ kburn->node->handle,
      /* endpoint         */ kburn->ep_in,
      /* bulk data        */ reinterpret_cast<uint8_t *>(data),
      /* bulk data length */ length,
      /* transferred      */ &size,
      /* timeout          */ kburn->medium_info.timeout_ms);

  if (is_timeout && (LIBUSB_ERROR_TIMEOUT == rc)) {
    *is_timeout = rc;
  }

  if ((rc != LIBUSB_SUCCESS) || (size != length)) {
    spdlog::error("usb bulk read data failed, {}({}), or {} != {}", rc,
                  libusb_error_name(rc), size, length);

    return false;
  }

  // if(length <= 64) {
  //     print_buffer(KBURN_LOG_ERROR, "usb recv", data, length);
  // }

  return true;
}

static bool kburn_parse_resp(struct kburn_usb_pkt_wrap *csw, kburn_t *kburn,
                             enum kburn_pkt_cmd cmd, void *result,
                             int *result_size) {
  if (csw->hdr.cmd != (cmd | CMD_FLAG_DEV_TO_HOST)) {
    spdlog::error("command recv error resp cmd");
    strncpy(kburn->error_msg, "cmd recv resp error", sizeof(kburn->error_msg));

    return false;
  }

  if (KBURN_RESULT_OK != csw->hdr.result) {
    spdlog::error("command recv error resp result");

    strncpy(kburn->error_msg, "cmd recv resp error", sizeof(kburn->error_msg));

    if (KBURN_RESULT_ERROR_MSG == csw->hdr.result) {
      csw->data[csw->hdr.data_size] = 0;

      spdlog::error("command recv error resp, error msg {}", reinterpret_cast<char *>(csw->data));

      // strncpy(kburn->error_msg, (char *)csw->data, sizeof(kburn->error_msg));
      strncpy(kburn->error_msg, reinterpret_cast<char *>(csw->data), sizeof(kburn->error_msg));
    }

    return false;
  }

  if ((NULL == result) || (NULL == result_size) || (0x00 == *result_size)) {
    spdlog::trace("user ignore result data");

    return true;
  }

  if (csw->hdr.data_size > *result_size) {
    spdlog::error("command result buffer too small, {} > {}",
                  csw->hdr.data_size, *result_size);

    csw->hdr.data_size = *result_size;
  }

  *result_size = csw->hdr.data_size;
  memcpy(result, &csw->data[0], csw->hdr.data_size);

  return true;
}

static bool kburn_send_cmd(kburn_t *kburn, enum kburn_pkt_cmd cmd, void *data,
                           int size, void *result, int *result_size) {
  struct kburn_usb_pkt_wrap cbw, csw;

  memset(&cbw, 0, sizeof(cbw));
  memset(&csw, 0, sizeof(csw));

  if (size > (int)sizeof(cbw.data)) {
    spdlog::error("command data size too large {}", size);

    return false;
  }

  cbw.hdr.cmd = cmd;
  cbw.hdr.data_size = size;
  if ((0x00 != size) && (NULL != data)) {
    memcpy(&cbw.data[0], data, size);
  }

  if (false == kburn_write_data(kburn, &cbw, sizeof(cbw))) {
    spdlog::error("command send data failed");

    strncpy(kburn->error_msg, "cmd send failed", sizeof(kburn->error_msg));

    return false;
  }

  if (false == kburn_read_data(kburn, &csw, sizeof(csw), NULL)) {
    spdlog::error("command recv data failed");

    strncpy(kburn->error_msg, "cmd recv failed", sizeof(kburn->error_msg));

    return false;
  }

  return kburn_parse_resp(&csw, kburn, cmd, result, result_size);
}

void kburn_nop(struct kburn_t *kburn) {
  spdlog::debug("issue a nop command, clear device error status");

  // issue a command, clear device state
  spdlog::level::level_enum old_level = spdlog::get_level();

  spdlog::set_level(spdlog::level::level_enum::off);

  /* read last packet */
  struct kburn_usb_pkt_wrap csw;
  kburn_read_data(kburn, &csw, sizeof(csw), NULL);

  kburn_send_cmd(kburn, KBURN_CMD_NONE, NULL, 0, NULL, NULL);

  spdlog::set_level(old_level);
}

bool kburn_parse_erase_config(struct kburn_t *kburn, uint64_t *offset,
                              uint64_t *size) {
  uint64_t o, s;

  o = *offset;
  s = *size;

  if ((o + s) > kburn->medium_info.capacity) {
    return false;
  }

  o = round_down(o, kburn->medium_info.erase_size);
  s = round_up(s, kburn->medium_info.erase_size);

  *offset = o;
  *size = s;

  return true;
}

char *kburn_get_error_msg(kburn_t *kburn) { return kburn->error_msg; }

void kburn_reset_chip(kburn_t *kburn) {
#define REBOOT_MARK (0x52626F74)

  struct kburn_usb_pkt_wrap cbw;
  const uint64_t reboot_mark = REBOOT_MARK;

  cbw.hdr.cmd = KBURN_CMD_REBOOT;
  cbw.hdr.data_size = sizeof(uint64_t);

  memcpy(&cbw.data[0], &reboot_mark, sizeof(uint64_t));

  if (false == kburn_write_data(kburn, &cbw, sizeof(cbw))) {
    spdlog::error("command send data failed");

    strncpy(kburn->error_msg, "cmd send failed", sizeof(kburn->error_msg));
  }
}

bool kburn_probe(kburn_t *kburn, enum KBurnMediumType target,
                 uint64_t *chunk_size) {
  uint8_t data[2];
  uint64_t result[1];
  int result_size = sizeof(result);

  data[0] = target;
  data[1] = 0xFF;

  spdlog::trace("probe target {}", static_cast<int>(target));

  if (false == kburn_send_cmd(kburn, KBURN_CMD_DEV_PROBE, data, 2, &result[0],
                              &result_size)) {
    spdlog::error("kburn probe medium failed");
    return false;
  }

  if (result_size != sizeof(result)) {
    spdlog::error("kburn probe medium failed, get result size error");
    return false;
  }

  if (chunk_size) {
    *chunk_size = result[0];

    spdlog::info("kburn probe, chunksize {}", *chunk_size);
  }

  return true;
}

uint64_t kburn_get_capacity(kburn_t *kburn) {
  struct kburn_medium_info info;
  int info_size = sizeof(info);

  if (false == kburn_send_cmd(kburn, KBURN_CMD_DEV_GET_INFO, NULL, 0, &info,
                              &info_size)) {
    spdlog::error("kburn get medium info failed");
    return 0;
  }

  if (info_size != sizeof(info)) {
    spdlog::error("kburn get medium info error result size. {} != {}",
                  info_size, sizeof(info));
    return 0;
  }

  memcpy(&kburn->medium_info, &info, sizeof(info));

  spdlog::info(
      "medium info, capacty {}, blk_sz {} erase_size {}, write protect {}",
      info.capacity, info.blk_size, info.erase_size, static_cast<uint8_t>(info.wp));

  return info.capacity;
}

bool kburn_erase(struct kburn_t *kburn, uint64_t offset, uint64_t size,
                 int max_retry) {
  struct kburn_usb_pkt_wrap cbw, csw;
  int retry_times = 0;
  int is_timeout = 0;

  uint64_t cfg[2] = {offset, size};

  spdlog::info("kburn erase medium, offset {}, size {}", offset, size);

  if ((offset + size) > kburn->medium_info.capacity) {
    spdlog::error("kburn erase medium exceed");

    strncpy(kburn->error_msg, "kburn erase medium exceed",
            sizeof(kburn->error_msg));
    return false;
  }

  if (0x01 == kburn->medium_info.wp) {
    spdlog::error("kburn erase medium failed, wp enabled");

    strncpy(kburn->error_msg, "kburn erase medium failed, wp enabled",
            sizeof(kburn->error_msg));
    return false;
  }

  /*************************************************************************/

  memset(&cbw, 0, sizeof(cbw));
  memset(&csw, 0, sizeof(csw));

  cbw.hdr.cmd = KBURN_CMD_ERASE_LBA;
  cbw.hdr.data_size = sizeof(cfg);
  memcpy(&cbw.data[0], &cfg[0], sizeof(cfg));

  if (false == kburn_write_data(kburn, &cbw, sizeof(cbw))) {
    spdlog::error("command send data failed");

    strncpy(kburn->error_msg, "cmd send failed", sizeof(kburn->error_msg));

    return false;
  }

  do {
    is_timeout = 0;

    if (true == kburn_read_data(kburn, &csw, sizeof(cbw), &is_timeout)) {
      break;
    }

    if (LIBUSB_ERROR_TIMEOUT != is_timeout) {
      spdlog::info("kburn erase medium read resp failed");

      return false;
    }

    do_sleep(3000);

  } while ((retry_times++) < max_retry);

  return true == kburn_parse_resp(&csw, kburn, KBURN_CMD_ERASE_LBA, NULL, NULL);
}

bool kburn_write_start(struct kburn_t *kburn, uint64_t offset, uint64_t size) {
  uint64_t cfg[2] = {offset, size};

  if ((offset + size) > kburn->medium_info.capacity) {
    spdlog::error("kburn write medium exceed");

    strncpy(kburn->error_msg, "kburn write medium exceed",
            sizeof(kburn->error_msg));
    return false;
  }

  if (0x01 == kburn->medium_info.wp) {
    spdlog::error("kburn write medium failed, wp enabled");

    strncpy(kburn->error_msg, "kburn write medium failed, wp enabled",
            sizeof(kburn->error_msg));
    return false;
  }

  if(offset % kburn->medium_info.erase_size) {
    spdlog::error("kburn write medium failed, write start address {} is not align to erase_size {}", offset, kburn->medium_info.erase_size);

    strncpy(kburn->error_msg, "kburn write medium failed, write start address is not align to erase_size",
            sizeof(kburn->error_msg));
    return false;
  }

  if (false == kburn_send_cmd(kburn, KBURN_CMD_WRITE_LBA, &cfg[0], sizeof(cfg),
                              NULL, NULL)) {
    spdlog::error("kburn write medium cfg failed");
    return false;
  }

  spdlog::info("kburn write medium cfg succ");

  return true;
}

bool kburn_write_chunk(struct kburn_t *kburn, const void *data, uint64_t size) {
  struct kburn_usb_pkt_wrap csw;

  spdlog::debug("write chunk {}", size);

  if (true == kburn_write_data(kburn, const_cast<void *>(data), size)) {
    return true;
  }

  spdlog::error("kburn write medium chunk failed,");

  if (false == kburn_read_data(kburn, &csw, sizeof(csw), NULL)) {
    spdlog::error(
        "kburn write medium chunk failed, recv error msg failed too.");

    return false;
  }

  if (KBURN_RESULT_ERROR_MSG == csw.hdr.result) {
    csw.data[csw.hdr.data_size] = 0;

    spdlog::error("command recv error resp, error msg {}", reinterpret_cast<char *>(csw.data));
    strncpy(kburn->error_msg, reinterpret_cast<char *>(csw.data), sizeof(kburn->error_msg));
  }

  return false;
}

bool kbrun_write_end(struct kburn_t *kburn) {
  struct kburn_usb_pkt_wrap csw;

  if (false == kburn_read_data(kburn, &csw, sizeof(csw), NULL)) {
    spdlog::error("kburn write medium end, recv error msg failed.");

    return false;
  }

  if (csw.hdr.cmd != (KBURN_CMD_WRITE_LBA | CMD_FLAG_DEV_TO_HOST)) {
    spdlog::error("kburn write medium end, resp cmd error.");

    strncpy(kburn->error_msg, "kburn write medium end, resp cmd error.",
            sizeof(kburn->error_msg));

    return false;
  }

  if (KBURN_RESULT_OK != csw.hdr.result) {
    spdlog::error("command recv error resp result");

    strncpy(kburn->error_msg, "cmd recv resp error", sizeof(kburn->error_msg));

    if (KBURN_RESULT_ERROR_MSG == csw.hdr.result) {
      csw.data[csw.hdr.data_size] = 0;

      spdlog::error("command recv error resp, error msg {}", reinterpret_cast<char *>(csw.data));

      strncpy(kburn->error_msg, reinterpret_cast<char *>(csw.data), sizeof(kburn->error_msg));
    }

    return false;
  }

  spdlog::info("write end, resp msg {}", reinterpret_cast<char *>(csw.data));

  kburn_nop(kburn);

  return true;
}

///////////////////////////////////////////////////////////////////////////////
K230UBOOTBurner::K230UBOOTBurner(struct kburn_usb_node *node) : KBurner(node) {
  kburn_.node = node;
  kburn_.medium_info.timeout_ms = 10;

  if (LIBUSB_SUCCESS != __get_endpoint(&kburn_)) {
    spdlog::error("kburn get ep failed");
  }
  spdlog::debug("device ep_in {:#02x}, ep_out {:#02x}", kburn_.ep_in, kburn_.ep_out);

  /* clear error status */
  kburn_nop(&kburn_);

  kburn_.medium_info.timeout_ms = 1000;
}

bool K230UBOOTBurner::probe(void) {
  probe_succ = kburn_probe(&kburn_, _medium_type, &chunk_size);

  return probe_succ;
}

struct kburn_medium_info *K230UBOOTBurner::get_medium_info() {
  if (0x00 == kburn_get_capacity(&kburn_)) {
    spdlog::error("get medium capacity failed");

    memset(&kburn_.medium_info, 0, sizeof(kburn_.medium_info));
  }

  return &kburn_.medium_info;
}

bool K230UBOOTBurner::reboot(void) {
  kburn_reset_chip(&kburn_);

  return true;
}

bool K230UBOOTBurner::write(const void *data, size_t size, uint64_t address) {
  uint64_t bytes_per_send, bytes_sent = 0, total_size = 0;

  size_t blk_size = kburn_.medium_info.blk_size;
  size_t aligned_size = (size + blk_size - 1) / blk_size * blk_size;

  write_buffer.resize(aligned_size, 0);
  memcpy(write_buffer.data(), reinterpret_cast<const uint8_t*>(data), size);

  if (false == kburn_write_start(&kburn_, address, aligned_size)) {
    spdlog::error("uboot burner, start write failed");
    return false;
  }

  bytes_sent = 0;
  total_size = aligned_size;

  log_progress(0, total_size);

  do {
    if ((total_size - bytes_sent) > chunk_size) {
      bytes_per_send = chunk_size;
    } else {
      bytes_per_send = (total_size - bytes_sent);
    }

    if (false ==
        kburn_write_chunk(&kburn_, write_buffer.data() + bytes_sent, bytes_per_send)) {
      spdlog::error("write failed @ {}", bytes_sent);

      return false;
    }

    bytes_sent += bytes_per_send;

    log_progress(bytes_sent, total_size);

  } while (bytes_sent < total_size);

  if (false == kbrun_write_end(&kburn_)) {
    spdlog::error("uboot burner, finsh write failed");
    return false;
  }

  return true;
}

}; // namespace K230

}; // namespace Kendryte_Burning_Tool
