#include "kburn.h"

#include "3rd-party/libusb-cmake/libusb/libusb/libusb.h"
#include "k230/kburn_k230.h"

#include "spdlog/spdlog.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <spdlog/common.h>
#include <sys/types.h>

namespace Kendryte_Burning_Tool {

#if WIN32

#include <windows.h>

void do_sleep(int ms) {
	Sleep(ms);
}
#else

#include <unistd.h>

void do_sleep(int ms) {
	usleep(ms * 1000);
}
#endif

void spdlog_set_log_level(int level) {
  spdlog::set_level(static_cast<spdlog::level::level_enum>(level));
}

int spdlog_get_log_level(void) { return static_cast<int>(spdlog::get_level()); }

void spdlog_set_user_logger(
    std::function<void(int, const std::string &)> callback) {
  auto custom_sink = std::make_shared<spdlog_custom_sink>(callback);
  auto logger = std::make_shared<spdlog::logger>("user_logger", custom_sink);

  spdlog::set_default_logger(logger);
}

void spdlog_log(const char *msg, spdlog::level::level_enum level) {
  spdlog::log(level, "[python] {}", msg);
}

// Mapping libusb log levels to spdlog levels
spdlog::level::level_enum map_libusb_log_level(int libusb_level) {
  switch (libusb_level) {
  case LIBUSB_LOG_LEVEL_ERROR:
    return spdlog::level::err;
  case LIBUSB_LOG_LEVEL_WARNING:
    return spdlog::level::warn;
  case LIBUSB_LOG_LEVEL_INFO:
    return spdlog::level::info;
  case LIBUSB_LOG_LEVEL_DEBUG:
    return spdlog::level::debug;
  default:
    return spdlog::level::info; // Default to info if unknown level
  }
}

// Custom log callback for libusb
void libusb_log_callback(libusb_context *ctx, enum libusb_log_level level,
                         const char *message) {
  spdlog::level::level_enum spdlog_level = map_libusb_log_level(level);
  spdlog::log(spdlog_level, "[libusb] {}", message);
}

KBurn::KBurn() {
  spdlog::info("kburn v{}.{}.{}", COMPILE_VERSION_MAJOR, COMPILE_VERSION_MINOR, COMPILE_VERSION_PATCH);
  spdlog::info("Compiled at {}, commit {}", COMPILE_TIME, COMPILE_HASH);

  const struct libusb_version *version = libusb_get_version();
  spdlog::info("libusb v{}.{}.{}.{}.", version->major, version->minor,
               version->micro, version->nano);

  usb_can_detach_kernel_driver =
      libusb_has_capability(LIBUSB_CAP_SUPPORTS_DETACH_KERNEL_DRIVER);
  spdlog::info("libusb detach_kernel_driver: {}.",
               usb_can_detach_kernel_driver);

  int r = libusb_init(&usb_ctx);
  if (r < 0) {
    spdlog::error("libusb init failed {}({}).", r, libusb_error_name(r));
    return;
  }

  libusb_set_log_cb(usb_ctx, libusb_log_callback, LIBUSB_LOG_CB_GLOBAL);
  r = libusb_set_option(usb_ctx, LIBUSB_OPTION_LOG_LEVEL,
                        LIBUSB_LOG_LEVEL_INFO);
  if (r < 0) {
    spdlog::error("log level set failed, {}({}).", r, libusb_error_name(r));
  }

  spdlog::info("kburn start.");
}

KBurn::~KBurn() {
  if (usb_ctx) {
    spdlog::info("libusb exit.");
    libusb_exit(usb_ctx);
  }
  spdlog::info("kburn stop.");
}

KBurn *KBurn::_instance = NULL;

KBurn *KBurn::instance() { return KBurn::_instance; }

void KBurn::createInstance() {
  if (NULL != KBurn::_instance) {
    spdlog::error("KBurn instance is created.");
    return;
  }
  auto instance = new KBurn();

  KBurn::_instance = instance;
}

void KBurn::deleteInstance() {
  delete KBurn::_instance;
  KBurn::_instance = NULL;
}

KBurner::~KBurner()
{
  close_usb_dev(dev_node);
}

void KBurner::default_progress(void *ctx, size_t current, size_t total) {
  (void)ctx;

  if (total <= 0)
    return;

  int bar_width = 50;
  int pos = (current * bar_width) / total;

  std::string progress_bar;
  progress_bar.append(pos, '=');
  progress_bar.append(bar_width - pos, ' ');

  int percentage = (current * 100) / total;

  spdlog::info("[{}] {}% [{}/{}]", progress_bar, percentage, current, total);
}

void kburn_initialize(void) {
  spdlog::set_pattern("[%H:%M:%S.%e] [%L] [thread %t] %v");

  spdlog::info("kburn initialize.");

  KBurn::createInstance();

  spdlog::set_level(spdlog::level::level_enum::err);
}

void kburn_deinitialize(void) {
  spdlog::info("kburn deinitialize.");

  KBurn::deleteInstance();
}

static void usb_get_dev_path(struct libusb_device *dev, char *path_buffer) {
  uint8_t bus, port;

  bus = libusb_get_bus_number(dev);
  port = libusb_get_port_number(dev);

  snprintf(path_buffer, KBURN_USB_PATH_BUFERR_SIZE, "%d-%d", bus, port);
}

KBurnUSBDeviceList *list_usb_device_with_vid_pid(uint16_t vid, uint16_t pid) {
  struct kburn_usb_node node;
  struct kburn_usb_dev_info info;
  
  KBurnUSBDeviceList *list = new KBurnUSBDeviceList();

  libusb_device **dev_list = NULL;
  ssize_t dev_count =
      libusb_get_device_list(KBurn::instance()->context(), &dev_list);

  if (0 > dev_count) {
    delete list;

    spdlog::warn("can not get usb device list");

    return nullptr;
  }

  for (ssize_t i = 0; i < dev_count; i++) {
    int result;
    struct libusb_device *dev = dev_list[i];
    struct libusb_device_descriptor desc;

    if (0 > libusb_get_device_descriptor(dev, &desc)) {
      continue;
    }

    if (vid != desc.idVendor || pid != desc.idProduct) {
      continue;
    }

    info.vid = desc.idVendor;
    info.pid = desc.idProduct;
    usb_get_dev_path(dev, info.path);

    for(int retry = 0; retry < 3; retry++) {
      if(LIBUSB_SUCCESS == (result = libusb_open(dev, &node.handle))) {
        break;
      }
      do_sleep(500);
    }

    if(LIBUSB_SUCCESS != result) {
      spdlog::warn("Open usb device failed, {}({})", result, libusb_strerror(result));
      continue;
    } else {
      memcpy(&node.info, &info, sizeof(info));

      info.type = get_usb_dev_type_with_node(&node);

      libusb_close(node.handle);
    }

    spdlog::debug("found usb device vid 0x{:04x} pid 0x{:04x} path {}",
                  info.vid, info.pid, info.path);

    list->push(info);
  }

  libusb_free_device_list(dev_list, true);

  return list;
}

struct kburn_usb_node *open_usb_dev_with_info(struct kburn_usb_dev_info &info) {
  char dev_path[KBURN_USB_PATH_BUFERR_SIZE];

  struct kburn_usb_node *node = NULL;

  libusb_device **dev_list = NULL;
  ssize_t dev_count =
      libusb_get_device_list(KBurn::instance()->context(), &dev_list);

  if (0 > dev_count) {
    spdlog::warn("can not get usb device list");

    return nullptr;
  }

  node = new kburn_usb_node();

  memset(node, 0, sizeof(*node));

  node->info = info;

  for (ssize_t i = 0; i < dev_count; i++) {
    int result;
    struct libusb_device *dev = dev_list[i];
    struct libusb_device_descriptor desc;

    if (0 > libusb_get_device_descriptor(dev, &desc)) {
      continue;
    }

    if (info.vid != desc.idVendor || info.pid != desc.idProduct) {
      continue;
    }

    usb_get_dev_path(dev, dev_path);

    if (0x00 != strncmp(dev_path, info.path, KBURN_USB_PATH_BUFERR_SIZE)) {
      continue;
    }

    if (LIBUSB_SUCCESS != (result = libusb_open(dev, &node->handle))) {
      spdlog::warn("open usb device failed, {}({})", result,
                   libusb_error_name(result));

      goto _failed;
    }
    node->isOpen = true;

    if(KBurn::instance()->can_detach()) {
      result = libusb_kernel_driver_active(node->handle, 0);
      if (result == 0) {
        spdlog::debug("libusb kernel driver is already set to this device");
      } else if (result == 1) {
        result = libusb_detach_kernel_driver(node->handle, 0);
        if (result != LIBUSB_ERROR_NOT_FOUND && result != 0) {
          spdlog::error("libusb_detach_kernel_driver() returns {}({})", result, libusb_error_name(result));

          goto _failed;
        }
        spdlog::debug("libusb kernel driver switch ok");
      } else if (result == LIBUSB_ERROR_NOT_SUPPORTED) {
        spdlog::debug("open_single_usb_port: system not support detach kernel driver {}({})", result, libusb_error_name(result));
      }
    }

    int lastr = -1;
    for (int i = 0; i < 20; i++) {
      result = libusb_claim_interface(node->handle, 0);
      if (result == 0) {
        spdlog::info("claim interface success, tried {} times", i + 1);
        break;
      }
      if (lastr != result) {
        lastr = result;
        spdlog::error("libusb_claim_interface failed, {}({})", result, libusb_error_name(result));
      }
      do_sleep(500);
    }

    if (result != 0) {
      spdlog::error("libusb_claim_interface: can not claim interface in 10s, other program is using this port.");

      goto _failed;
    }

    node->isClaim = true;

    get_usb_dev_type_with_node(node);

    spdlog::debug("open deivce vid 0x{:04x}, pid 0x{:04x}, path {}, type {}",
                  info.vid, info.pid, info.path, static_cast<int>(node->info.type));

    libusb_free_device_list(dev_list, true);

    return node;
  }

_failed:

  if(node) {
    if(node->isOpen) {
      node->isOpen = false;

      libusb_close(node->handle);
    }

    delete node;
  }

  libusb_free_device_list(dev_list, true);

  return nullptr;
}

void close_usb_dev(struct kburn_usb_node *node) {
  if(node->isClaim) {
    node->isClaim = false;

		libusb_release_interface(node->handle, 0);
  }

  if(node->isOpen) {
    node->isOpen = false;

    libusb_close(node->handle);
  }

  spdlog::debug("close device vid 0x{:04x}, pid 0x{:04x}, path {}", node->info.vid, node->info.pid, node->info.path);

  delete node;
}

enum kburn_usb_dev_type get_usb_dev_type_with_node(struct kburn_usb_node *node) {
  node->info.type = KBURN_USB_DEV_INVALID;

  switch (node->info.pid) {
  case 0x0230:
    K230::k230_probe_device(node);
    break;
  default:
    spdlog::error("unsupport vid 0x{:04x} pid 0x{:04x}, path {}", node->info.vid, node->info.pid, node->info.path);
    break;
  }

  spdlog::debug("device type {}", static_cast<int>(node->info.type));

  return node->info.type;
}

enum kburn_usb_dev_type get_usb_dev_type_with_info(struct kburn_usb_dev_info &info) {
  enum kburn_usb_dev_type type;

  struct kburn_usb_node *node = nullptr;

  spdlog::debug("get device type, vid 0x{:04x}, pid 0x{:04x}, path {}",
                info.vid, info.pid, info.path);

  if (!(node = open_usb_dev_with_info(info))) {
    return KBURN_USB_DEV_INVALID;
  }
  type = node->info.type;

  close_usb_dev(node);

  spdlog::debug("device type {}", static_cast<int>(node->info.type));

  return type;
}

KBurner *request_burner_with_info(struct kburn_usb_dev_info &info) {
	KBurner *burner = nullptr;
	struct kburn_usb_node *node = nullptr;

	node = open_usb_dev_with_info(info);

	if(!node) {
  		return nullptr;
	}

	if (0x0230 == node->info.pid) {
		burner = K230::k230_request_burner(node);
	} else {
		spdlog::error("unsupport vid 0x{:04x} pid 0x{:04x} path {}", node->info.vid, node->info.pid, node->info.path);
	}

	if(!burner) {
  		close_usb_dev(node);
	}

	return burner;
}

}; // namespace Kendryte_Burning_Tool
