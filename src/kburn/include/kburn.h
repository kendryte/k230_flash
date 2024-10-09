#pragma once

#include <cstdint>
#include <functional>
#include <iostream>
#include <mutex>

#include <libusb.h>

#include <spdlog/sinks/base_sink.h>
#include <spdlog/spdlog.h>

#include "version_info.h"

namespace Kendryte_Burning_Tool {

#define KBURN_USB_PATH_BUFERR_SIZE (8)

#if defined(_WIN32)
    #ifdef kburn_EXPORTS
        #define KBURN_API __declspec(dllexport)
    #else  // !kburn_EXPORTS
        #define KBURN_API __declspec(dllimport)
    #endif
#else  // !defined(_WIN32)
    #define KBURN_API __attribute__((visibility("default")))
#endif

enum KBurnMediumType {
  KBURN_MEDIUM_INVAILD = 0,
  KBURN_MEDIUM_EMMC,
  KBURN_MEDIUM_SDCARD,
  KBURN_MEDIUM_SPI_NAND,
  KBURN_MEDIUM_SPI_NOR,
  KBURN_MEDIUM_OTP,
};

enum kburn_usb_dev_type {
  KBURN_USB_DEV_INVALID = 0,
  KBURN_USB_DEV_BROM = 1,
  KBURN_USB_DEV_UBOOT = 2,
  KBURN_USB_DEV_MAX,
};

struct kburn_usb_dev_info {
  enum kburn_usb_dev_type type;

  uint16_t vid, pid;
  char path[KBURN_USB_PATH_BUFERR_SIZE];
};

struct kburn_usb_node {
  struct libusb_device_handle *handle;

  struct kburn_usb_dev_info info;

  bool isOpen = false;
  bool isClaim = false;
};

class KBURN_API KBurnUSBDeviceList {
public:
  KBurnUSBDeviceList() {}

  // Custom iterator class
  class Iterator {
  public:
    Iterator(struct kburn_usb_dev_info *ptr) : ptr_(ptr) {}

    struct kburn_usb_dev_info operator*() const { return *ptr_; }

    Iterator &operator++() {
      ++ptr_;
      return *this;
    }

    bool operator==(const Iterator &other) const { return ptr_ == other.ptr_; }
    bool operator!=(const Iterator &other) const { return ptr_ != other.ptr_; }

  private:
    struct kburn_usb_dev_info *ptr_;
  };

    // Method to get the number of devices
  size_t size() const {
      return data_.size(); // Assuming `devices` is a std::vector or similar
  }

  // Methods to return the iterator
  Iterator begin() { return Iterator(data_.data()); }
  Iterator end() { return Iterator(data_.data() + data_.size()); }

  void push(struct kburn_usb_dev_info info) { data_.push_back(info); }

private:
  std::vector<struct kburn_usb_dev_info> data_;
};

class KBURN_API KBurn {
public:
  static class KBurn *_instance;

  KBurn();
  ~KBurn();

  static void createInstance();
  static void deleteInstance();
  static KBurn *instance();

  struct libusb_context *context() { return usb_ctx; }

  bool can_detach() { return usb_can_detach_kernel_driver; };

private:
  struct libusb_context *usb_ctx;
  bool usb_can_detach_kernel_driver = false;
};

class KBURN_API KBurner {
public:
  using progress_fn_t = std::function<void(void *ctx, size_t current, size_t totoal)>;

  explicit KBurner(struct kburn_usb_node *node) : dev_node(node) {}

  ~KBurner();

  void register_progress_fn(progress_fn_t progress_fn, void *ctx) {
    progress_fn_ = progress_fn;
    progress_user_ctx = ctx;
  }

  bool set_medium_type(enum KBurnMediumType type) {
    _medium_type = type;

    return true;
  }

  virtual bool write(const void *data, size_t size, uint64_t address) = 0;

protected:
  struct kburn_usb_node *dev_node;

  enum KBurnMediumType _medium_type = KBURN_MEDIUM_INVAILD;

  void *progress_user_ctx = NULL;
  progress_fn_t progress_fn_ = default_progress;

  void log_progress(int current, size_t total) {
    progress_fn_(progress_user_ctx, current, total);
  }

private:
  static void default_progress(void *ctx, size_t current, size_t totoal);
};

class KBURN_API spdlog_custom_sink : public spdlog::sinks::base_sink<std::mutex> {
public:
  using log_fn_t = std::function<void(int, const std::string &)>;

  explicit spdlog_custom_sink(log_fn_t log_function)
      : user_logger_(log_function) {}

protected:
  void sink_it_(const spdlog::details::log_msg &msg) override {
    spdlog::memory_buf_t formatted;
    formatter_->format(msg, formatted);
    std::string log_message = fmt::to_string(formatted);

    if (user_logger_) {
      user_logger_(static_cast<int>(msg.level), log_message);
    } else {
      if (spdlog::should_log(msg.level)) {
        std::cerr << "[FALLBACK] " << log_message;
      }
    }
  }

  void flush_() override {}

private:
  log_fn_t user_logger_;
};

KBURN_API void kburn_initialize(void);
KBURN_API void kburn_deinitialize(void);

KBURN_API void do_sleep(int ms);

KBURN_API void spdlog_set_log_level(int level);
KBURN_API int spdlog_get_log_level(void);

KBURN_API void spdlog_log(const char *msg, spdlog::level::level_enum level = spdlog::level::level_enum::info);
KBURN_API void spdlog_set_user_logger(std::function<void(int, const std::string &)> callback);

KBURN_API KBurnUSBDeviceList *list_usb_device_with_vid_pid(uint16_t vid = 0x29f1,
                                                 uint16_t pid = 0x0230);

KBURN_API struct kburn_usb_node *open_usb_dev_with_info(struct kburn_usb_dev_info &info);
KBURN_API void close_usb_dev(struct kburn_usb_node *node);

KBURN_API enum kburn_usb_dev_type get_usb_dev_type_with_node(struct kburn_usb_node *node);
KBURN_API enum kburn_usb_dev_type get_usb_dev_type_with_info(struct kburn_usb_dev_info &info);

KBURN_API KBurner *request_burner_with_info(struct kburn_usb_dev_info &info);

}; // namespace Kendryte_Burning_Tool
