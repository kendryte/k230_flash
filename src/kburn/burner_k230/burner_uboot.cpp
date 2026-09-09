#include "k230/kburn_k230.h"
#include <algorithm>
#include <climits>
#include <memory>

namespace Kendryte_Burning_Tool {

namespace K230 {

#define CMD_FLAG_DEV_TO_HOST (0x8000)

enum kburn_pkt_cmd {
  KBURN_CMD_NONE = 0,
  KBURN_CMD_REBOOT = 0x01,

  KBURN_CMD_DEV_PROBE = 0x10,
  KBURN_CMD_DEV_GET_INFO = 0x11,

	KBURN_CMD_ERASE_LBA = 0x20,

	KBURN_CMD_WRITE_LBA = 0x21,
	KBURN_CMD_WRITE_LBA_CHUNK = 0x22,

	KBURN_CMD_READ_LBA = 0x23,
	KBURN_CMD_READ_LBA_CHUNK = 0x24,

  KBURN_CMD_MAX,
};

enum kburn_pkt_result {
  KBURN_RESULT_NONE = 0,

  KBURN_RESULT_OK = 1,
  KBURN_RESULT_ERROR = 2,

  KBURN_RESULT_ERROR_MSG = 0xFF,

  KBURN_RESULT_MAX,
};

#define KBUNR_USB_PKT_SIZE (60)

#pragma pack(push, 1)

struct kburn_usb_pkt {
  uint16_t cmd;
  uint16_t result; /* only valid in csw */
  uint16_t data_size;
};

struct kburn_usb_pkt_wrap {
  struct kburn_usb_pkt hdr;
  uint8_t data[KBUNR_USB_PKT_SIZE - sizeof(struct kburn_usb_pkt)];
};

#pragma pack(pop)

static_assert(sizeof(kburn_usb_pkt) == 6, "KBURN packet header ABI changed");
static_assert(sizeof(kburn_usb_pkt_wrap) == KBUNR_USB_PKT_SIZE,
	      "KBURN command packet ABI changed");

static bool kburn_copy_error_message(const struct kburn_usb_pkt_wrap *packet,
                                     kburn_t *kburn) {
  if (packet->hdr.data_size > sizeof(packet->data)) {
    spdlog::error("invalid response data size {}", packet->hdr.data_size);
    strncpy(kburn->error_msg, "invalid response data size",
            sizeof(kburn->error_msg));
    kburn->error_msg[sizeof(kburn->error_msg) - 1] = '\0';
    return false;
  }

  size_t message_size = std::min<size_t>(packet->hdr.data_size,
                                         sizeof(kburn->error_msg) - 1);
  memcpy(kburn->error_msg, packet->data, message_size);
  kburn->error_msg[message_size] = '\0';
  return true;
}

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
        if (dir == LIBUSB_ENDPOINT_IN) {
          kburn->ep_in = ep->bEndpointAddress;
        } else {
          kburn->ep_out = ep->bEndpointAddress;
          kburn->ep_out_mps = ep->wMaxPacketSize;
        }
      }
    }
  }

  libusb_free_config_descriptor(config);
	if (!kburn->ep_in || !kburn->ep_out || !kburn->ep_out_mps)
		return LIBUSB_ERROR_NOT_FOUND;

  return LIBUSB_SUCCESS;
}

static int kburn_probe_loader_version(kburn_t *kburn)
{
  int rc = -1;
  uint32_t version = 0;

  rc = libusb_control_transfer(
    /* dev_handle    */ kburn->node->handle,
    /* bmRequestType */ (uint8_t)(LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE),
    /* bRequest      */ 0,
    /* wValue        */ (uint16_t)(0x0001),
    /* wIndex        */ 0,
    /* Data          */ (uint8_t *)&version,
    /* wLength       */ sizeof(version),
    /* timeout       */ 1000);

  if (rc != sizeof(version)) {
    spdlog::error("usb issue control transfer failed, {}({})", rc, libusb_error_name(rc));
    return 0;
  }

  spdlog::debug("loader version is {}", version);

  return version;
}

static bool kburn_write_data(kburn_t *kburn, void *data, int length) {
  int rc = -1, size = 0;

	if (!kburn || length < 0 || (length && !data))
		return false;

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

static bool kburn_write_zlp(kburn_t *kburn) {
	int transferred = 0;
	int rc = libusb_bulk_transfer(kburn->node->handle, kburn->ep_out, nullptr,
				      0, &transferred,
				      kburn->medium_info.timeout_ms);

	if (rc != LIBUSB_SUCCESS) {
		spdlog::error("usb bulk write ZLP failed, {}({})", rc,
			      libusb_error_name(rc));
		return false;
	}
	return true;
}

static bool kburn_read_data(kburn_t *kburn, void *data, int length,
                            int *is_timeout) {
  int rc = -1, size = 0;

	  if (!kburn || length < 0 || (length && !data)) {
      spdlog::error("invalid buffer");
      return false;
  }

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
  if (csw->hdr.data_size > sizeof(csw->data)) {
    spdlog::error("command response data size is invalid: {}",
                  csw->hdr.data_size);
    strncpy(kburn->error_msg, "invalid response data size",
            sizeof(kburn->error_msg));
    kburn->error_msg[sizeof(kburn->error_msg) - 1] = '\0';
    return false;
  }

  if (csw->hdr.cmd != (cmd | CMD_FLAG_DEV_TO_HOST)) {
    spdlog::error("command recv error resp cmd");
    strncpy(kburn->error_msg, "cmd recv resp error", sizeof(kburn->error_msg));

    return false;
  }

  if (KBURN_RESULT_OK != csw->hdr.result) {
    spdlog::error("command recv error resp result");

    strncpy(kburn->error_msg, "cmd recv resp error", sizeof(kburn->error_msg));

    if (KBURN_RESULT_ERROR_MSG == csw->hdr.result) {
      kburn_copy_error_message(csw, kburn);
      spdlog::error("command recv error resp, error msg {}", kburn->error_msg);
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

    strncpy(kburn->error_msg, "response buffer too small",
            sizeof(kburn->error_msg));
    kburn->error_msg[sizeof(kburn->error_msg) - 1] = '\0';
    return false;
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

  if (size < 0 || size > (int)sizeof(cbw.data) || (size && !data)) {
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
  uint32_t timeout_ms = kburn->medium_info.timeout_ms;

  spdlog::debug("issue a nop command, clear device error status");

  // issue a command, clear device state
  spdlog::level::level_enum old_level = spdlog::get_level();

  spdlog::set_level(spdlog::level::level_enum::off);

  /* read last packet */
  struct kburn_usb_pkt_wrap csw;
  kburn->medium_info.timeout_ms = 50;
  kburn_read_data(kburn, &csw, sizeof(csw), NULL);
  kburn->medium_info.timeout_ms = timeout_ms;

  kburn_send_cmd(kburn, KBURN_CMD_NONE, NULL, 0, NULL, NULL);

  spdlog::set_level(old_level);
}

bool kburn_parse_erase_config(struct kburn_t *kburn, uint64_t *offset,
                              uint64_t *size) {
  uint64_t o, s, end;
	uint64_t erase_size;

	if (!kburn || !offset || !size || !kburn->medium_info.erase_size)
		return false;

  o = *offset;
  s = *size;
	erase_size = kburn->medium_info.erase_size;

  if (o > kburn->medium_info.capacity ||
      s > (kburn->medium_info.capacity - o)) {
    return false;
  }
	end = o + s;

	o = round_down(o, erase_size);
	if (end % erase_size) {
		if (end > UINT64_MAX - (erase_size - end % erase_size))
			return false;
		end += erase_size - end % erase_size;
	}
	if (end > kburn->medium_info.capacity)
		return false;
	s = end - o;

  *offset = o;
  *size = s;

  return true;
}

char *kburn_get_error_msg(kburn_t *kburn) { return kburn->error_msg; }

void kburn_reset_chip(kburn_t *kburn) {
#define REBOOT_MARK (0x52626F74)

  struct kburn_usb_pkt_wrap cbw;
  const uint64_t reboot_mark = REBOOT_MARK;

  memset(&cbw, 0, sizeof(cbw));
  cbw.hdr.cmd = KBURN_CMD_REBOOT;
  cbw.hdr.data_size = sizeof(uint64_t);

  memcpy(&cbw.data[0], &reboot_mark, sizeof(uint64_t));

  if (false == kburn_write_data(kburn, &cbw, sizeof(cbw))) {
    spdlog::error("command send data failed");

    strncpy(kburn->error_msg, "cmd send failed", sizeof(kburn->error_msg));
  }
}

bool kburn_probe(kburn_t *kburn, enum KBurnMediumType target,
                 uint64_t *out_out_chunk_size, uint64_t *in_out_chunk_size) {
  uint8_t data[2];
  uint64_t result[2];
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
	if (!result[0] || result[0] > INT_MAX || !result[1] ||
	    result[1] > UINT16_MAX) {
		spdlog::error("loader returned invalid chunk sizes: {}, {}",
			      result[0], result[1]);
		return false;
	}

  spdlog::error("kburn probe, chunksize: out {}, in {}", result[0], result[1]);

  if (out_out_chunk_size) {
    *out_out_chunk_size = result[0];
  }
	kburn->out_chunk_size = result[0];

  if(in_out_chunk_size) {
    *in_out_chunk_size = result[1];
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

  if (offset > kburn->medium_info.capacity ||
      size > (kburn->medium_info.capacity - offset)) {
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

  } while ((retry_times++) < max_retry);

  return true == kburn_parse_resp(&csw, kburn, KBURN_CMD_ERASE_LBA, NULL, NULL);
}

bool kburn_write_start(struct kburn_t *kburn, uint64_t offset, uint64_t size, uint64_t max, uint64_t part_flag) {
  int cfg_size = sizeof(uint64_t) * 3;
  uint64_t cfg[4] = {offset, size, max, part_flag};
	uint64_t medium_size = size;

	if (part_flag && kburn->loader_version < 1) {
		spdlog::error("loader protocol does not support partition flags");
		strncpy(kburn->error_msg,
			"loader protocol does not support partition flags",
			sizeof(kburn->error_msg));
		kburn->error_msg[sizeof(kburn->error_msg) - 1] = '\0';
		return false;
	}

	if (KBURN_FLAG_SPI_NAND_WRITE_WITH_OOB == KBURN_FLAG_FLAG(part_flag)) {
		uint64_t page_size = KBURN_FLAG_VAL1(part_flag);
		uint64_t oob_size = KBURN_FLAG_VAL2(part_flag);

		if (kburn->medium_info.type != KBURN_MEDIUM_SPI_NAND || !page_size ||
		    !oob_size || page_size + oob_size < page_size ||
		    size % (page_size + oob_size)) {
			spdlog::error("invalid SPI NAND OOB layout");
			return false;
		}
		medium_size = (size / (page_size + oob_size)) * page_size;
	}

	  if (!size || offset > kburn->medium_info.capacity ||
	      medium_size > (kburn->medium_info.capacity - offset)) {
    spdlog::error("kburn write medium exceed");

    strncpy(kburn->error_msg, "kburn write medium exceed",
            sizeof(kburn->error_msg));
    return false;
  }

	if (max && (medium_size > max ||
		    max > (kburn->medium_info.capacity - offset))) {
		spdlog::error("kburn write exceeds partition");
		strncpy(kburn->error_msg, "kburn write exceeds partition",
			sizeof(kburn->error_msg));
		return false;
	}

  if (0x01 == kburn->medium_info.wp) {
    spdlog::error("kburn write medium failed, wp enabled");

    strncpy(kburn->error_msg, "kburn write medium failed, wp enabled",
            sizeof(kburn->error_msg));
    return false;
  }

	  if (!kburn->medium_info.erase_size ||
	      offset % kburn->medium_info.erase_size) {
    spdlog::error("kburn write medium failed, write start address {} is not align to erase_size {}", offset, kburn->medium_info.erase_size);

    strncpy(kburn->error_msg, "kburn write medium failed, write start address is not align to erase_size",
            sizeof(kburn->error_msg));
    return false;
  }

  if(0x01 <= kburn->loader_version) {
      cfg_size = sizeof(uint64_t) * 4;
  }

  if (false == kburn_send_cmd(kburn, KBURN_CMD_WRITE_LBA, &cfg[0], cfg_size,
                              NULL, NULL)) {
    spdlog::error("kburn write medium cfg failed");
    return false;
  }

  spdlog::info("kburn write medium cfg succ");
	kburn->dl_total = size;
	kburn->dl_sent = 0;

  return true;
}

bool kburn_write_chunk(struct kburn_t *kburn, const void *data, uint64_t size) {
  struct kburn_usb_pkt_wrap csw;
	uint64_t remaining, expected;

  spdlog::debug("write chunk {}", size);
	if (!kburn || !data || !size || size > INT_MAX ||
	    !kburn->out_chunk_size || kburn->dl_sent > kburn->dl_total) {
		spdlog::error("invalid write chunk");
		return false;
	}

	remaining = kburn->dl_total - kburn->dl_sent;
	expected = std::min(kburn->out_chunk_size, remaining);
	if (size > expected) {
		spdlog::error("write chunk {} exceeds expected {}", size, expected);
		return false;
	}

	if (true == kburn_write_data(kburn, const_cast<void *>(data),
				    static_cast<int>(size))) {
		if (size < expected && kburn->ep_out_mps &&
		    size % kburn->ep_out_mps == 0 && !kburn_write_zlp(kburn))
			return false;
		kburn->dl_sent += size;
		return true;
  }

  spdlog::error("kburn write medium chunk failed,");

  if (false == kburn_read_data(kburn, &csw, sizeof(csw), NULL)) {
    spdlog::error(
        "kburn write medium chunk failed, recv error msg failed too.");

    return false;
  }

	(void)kburn_parse_resp(&csw, kburn, KBURN_CMD_WRITE_LBA, nullptr,
			       nullptr);

	  return false;
}

bool kbrun_write_end(struct kburn_t *kburn) {
  struct kburn_usb_pkt_wrap csw;
	if (!kburn || kburn->dl_sent != kburn->dl_total) {
		if (kburn) {
			strncpy(kburn->error_msg, "incomplete write transfer",
				sizeof(kburn->error_msg));
			kburn->error_msg[sizeof(kburn->error_msg) - 1] = '\0';
		}
		return false;
	}

  if (false == kburn_read_data(kburn, &csw, sizeof(csw), NULL)) {
    spdlog::error("kburn write medium end, recv error msg failed.");

    return false;
  }

	if (!kburn_parse_resp(&csw, kburn, KBURN_CMD_WRITE_LBA, nullptr, nullptr))
		return false;

  kburn_nop(kburn);
	kburn->dl_total = 0;
	kburn->dl_sent = 0;

  return true;
}

bool kburn_read_start(struct kburn_t *kburn, uint64_t offset, uint64_t size) {
  uint64_t cfg[2] = {offset, size};

  if (offset > kburn->medium_info.capacity ||
      size > (kburn->medium_info.capacity - offset)) {
    spdlog::error("kburn read medium exceed");

    strncpy(kburn->error_msg, "kburn read medium exceed",
            sizeof(kburn->error_msg));
    return false;
  }

  if (false == kburn_send_cmd(kburn, KBURN_CMD_READ_LBA, &cfg[0], sizeof(cfg),
                              NULL, NULL)) {
    spdlog::error("kburn write medium cfg failed");
    return false;
  }

  spdlog::info("kburn write medium cfg succ");

  return true;
}

bool kburn_read_chunk(struct kburn_t *kburn, void *data, uint64_t size) {
  int is_timeout;
  int retry_times = 0;
  int max_retry = 3;

  struct kburn_usb_pkt_wrap *pkt = NULL;

	if (!kburn || !data || !size || size > UINT16_MAX || size > INT_MAX) {
		spdlog::error("invalid read chunk size {}", size);
		return false;
	}

	  size_t read_buffer_size = sizeof(struct kburn_usb_pkt) + size;

  spdlog::debug("read chunk {}", size);

	  if(read_buffer_size > kburn->rd_buffer.size()) {
    kburn->rd_buffer.resize(read_buffer_size, 0);
  }

  do {
    is_timeout = 0;

	    if (true == kburn_read_data(kburn, kburn->rd_buffer.data(),
				       static_cast<int>(read_buffer_size), &is_timeout)) {
      break;
    }

    if (LIBUSB_ERROR_TIMEOUT != is_timeout) {
      spdlog::info("kburn erase medium read resp failed");

      return false;
    }

  } while ((retry_times++) < max_retry);

  pkt = reinterpret_cast<struct kburn_usb_pkt_wrap *>(kburn->rd_buffer.data());

  if (((KBURN_CMD_READ_LBA_CHUNK | CMD_FLAG_DEV_TO_HOST) != pkt->hdr.cmd) || \
      (KBURN_RESULT_OK != pkt->hdr.result))
  {
    spdlog::error("kburn read medium chunk failed, result cmd {:04x}, status {:04x}", pkt->hdr.cmd, pkt->hdr.result);
    return false;
  }

  if(pkt->hdr.data_size != size) {
    spdlog::error("kburn read medium chunk failed, read data size mismatch {} != {}", size, pkt->hdr.data_size);
    return false;
  }

  memcpy(data, pkt->data, pkt->hdr.data_size);

  return true;
}

bool kbrun_read_end(struct kburn_t *kburn) {
  struct kburn_usb_pkt_wrap csw;

  if (false == kburn_read_data(kburn, &csw, sizeof(csw), NULL)) {
    spdlog::error("kburn read medium end, recv error msg failed.");

    return false;
  }

	return kburn_parse_resp(&csw, kburn, KBURN_CMD_READ_LBA_CHUNK,
				nullptr, nullptr);
}
///////////////////////////////////////////////////////////////////////////////
K230UBOOTBurner::K230UBOOTBurner(struct kburn_usb_node *node)
	: KBurner(node), kburn_{} {
  kburn_.node = node;
  kburn_.medium_info.timeout_ms = 10;

	  if (LIBUSB_SUCCESS != __get_endpoint(&kburn_)) {
	    spdlog::error("kburn get ep failed");
	    return;
	  }
	endpoints_valid = true;
  spdlog::debug("device ep_in {:#02x}, ep_out {:#02x}", kburn_.ep_in, kburn_.ep_out);

  kburn_.loader_version = kburn_probe_loader_version(&kburn_);

  /* clear error status */
  kburn_nop(&kburn_);

  kburn_.medium_info.timeout_ms = 10000; // set a longer timeout for probe medium info
}

bool K230UBOOTBurner::probe(void) {
	if (!endpoints_valid)
		return false;
  probe_succ = kburn_probe(&kburn_, _medium_type, &out_chunk_size, &in_chunk_size);

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

bool K230UBOOTBurner::write_stream(std::ifstream& file_stream, size_t size, uint64_t address, uint64_t max, uint64_t flag) {
  size_t bytes_per_send, bytes_sent = 0, total_size = 0;

	  size_t blk_size = kburn_.medium_info.blk_size;
	if (!size || !blk_size || !out_chunk_size || size > SIZE_MAX - (blk_size - 1))
		return false;
	  size_t aligned_size = (size + blk_size - 1) / blk_size * blk_size;
	  size_t chunk_size = out_chunk_size / blk_size * blk_size;
	if (!chunk_size)
		return false;

  uint64_t flag_flag, flag_val1, flag_val2;

  flag_flag = KBURN_FLAG_FLAG(flag);
  flag_val1 = KBURN_FLAG_VAL1(flag);
  flag_val2 = KBURN_FLAG_VAL2(flag);

  if ((KBURN_FLAG_SPI_NAND_WRITE_WITH_OOB == flag_flag) &&
      (KBURN_MEDIUM_SPI_NAND == kburn_.medium_info.type)) {
	      uint64_t page_size_with_oob = flag_val1 + flag_val2;
		if (!flag_val1 || !flag_val2 || page_size_with_oob < flag_val1 ||
		    out_chunk_size <= page_size_with_oob ||
		    size > SIZE_MAX - (page_size_with_oob - 1))
			return false;

      blk_size = page_size_with_oob;
      aligned_size = (size + blk_size - 1) / blk_size * blk_size;
      chunk_size = ((chunk_size / page_size_with_oob) - 1) * page_size_with_oob;
  }

  if (aligned_size != size) {
      spdlog::warn("uboot burner, aligned write size from {} to {}", size, aligned_size);
  }

  if (!kburn_write_start(&kburn_, address, aligned_size, max, flag)) {
      spdlog::error("uboot burner, start write failed");
      return false;
  }

  bytes_sent = 0;
  total_size = aligned_size;

  log_progress(0, total_size);

  std::vector<uint8_t> buffer(chunk_size, 0);

  while (bytes_sent < total_size) {
      bytes_per_send = std::min(chunk_size, total_size - bytes_sent);
      file_stream.read(reinterpret_cast<char*>(buffer.data()), bytes_per_send);

      std::streamsize read_count = file_stream.gcount();
      if (read_count < static_cast<std::streamsize>(bytes_per_send)) {
          // Pad with zeroes if not enough data (end of file)
          std::fill(buffer.begin() + read_count, buffer.begin() + bytes_per_send, 0);
      }

      if (!kburn_write_chunk(&kburn_, buffer.data(), bytes_per_send)) {
          spdlog::error("write failed @ {}", bytes_sent);
          return false;
      }

      bytes_sent += bytes_per_send;
      log_progress(bytes_sent, total_size);
  }

  if (!kbrun_write_end(&kburn_)) {
      spdlog::error("uboot burner, finish write failed");
      return false;
  }

  return true;
}

bool K230UBOOTBurner::read(void *data, size_t size, uint64_t address) {
  uint64_t bytes_per_read, bytes_read = 0, total_size = 0;

		if (!data || size == 0 || kburn_.medium_info.blk_size == 0 ||
		    !in_chunk_size || in_chunk_size > UINT16_MAX ||
		    address > kburn_.medium_info.capacity)
			return false;

  size_t blk_size = kburn_.medium_info.blk_size;
	uint64_t aligned_address = round_down(address, blk_size);
	uint64_t prefix = address - aligned_address;
	if (size > UINT64_MAX - prefix)
		return false;
	uint64_t requested_span = prefix + size;
	if (requested_span > UINT64_MAX - (blk_size - 1))
		return false;
	uint64_t aligned_size = (requested_span + blk_size - 1) /
				blk_size * blk_size;
	if (aligned_size > kburn_.medium_info.capacity - aligned_address)
		return false;

	  if(false == kburn_read_start(&kburn_, aligned_address, aligned_size)) {
    spdlog::error("uboot burner, start read failed");
    return false;
  }

  bytes_read = 0;
  total_size = aligned_size;

  log_progress(0, total_size);
	std::vector<uint8_t> chunk(in_chunk_size);

  do {
    if ((total_size - bytes_read) > in_chunk_size) {
      bytes_per_read = in_chunk_size;
    } else {
      bytes_per_read = (total_size - bytes_read);
    }

    if (false == kburn_read_chunk(&kburn_, chunk.data(), bytes_per_read)) {
      spdlog::error("read failed @ {}", bytes_read);

      return false;
    }

			uint64_t chunk_end = bytes_read + bytes_per_read;
			uint64_t copy_start = std::max(bytes_read, prefix);
			uint64_t copy_end = std::min(chunk_end, prefix + size);
			if (copy_end > copy_start) {
				memcpy(reinterpret_cast<uint8_t *>(data) +
					       (copy_start - prefix),
				       chunk.data() + (copy_start - bytes_read),
				       copy_end - copy_start);
			}

    bytes_read += bytes_per_read;

    log_progress(bytes_read, total_size);
  } while (bytes_read < total_size);

  if (false == kbrun_read_end(&kburn_)) {
    spdlog::error("uboot burner, finsh read failed");
		return false;
  }

  return true;
}

bool K230UBOOTBurner::erase(uint64_t address, size_t size) {
  spdlog::trace("%s", __func__);

	uint64_t erase_size = std::max<uint64_t>(kburn_.medium_info.erase_size, 1);
	int retry = static_cast<int>(std::clamp<uint64_t>(size / erase_size + 2,
							 3, 120));

  return kburn_erase(&kburn_, address, size, retry);
}

}; // namespace K230

}; // namespace Kendryte_Burning_Tool
