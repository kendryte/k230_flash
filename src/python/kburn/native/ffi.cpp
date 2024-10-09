#include <string>

#include <pybind11/pybind11.h>
#include <pybind11/pytypes.h>

#include <k230/kburn_k230.h>
#include <kburn.h>

namespace py = pybind11;

using namespace Kendryte_Burning_Tool;

namespace pybind11::detail {
std::atomic_bool g_python_shutdown = false;
}

PYBIND11_MODULE(_kburn, m) {
  m.doc() = "k230 burn library";

  // Create version string using the defined version components
  std::string version = std::to_string(COMPILE_VERSION_MAJOR) + "." +
                        std::to_string(COMPILE_VERSION_MINOR) + "." +
                        std::to_string(COMPILE_VERSION_PATCH);

  // Set the module attributes
  m.attr("__version__") = version;           // Version from defines
  m.attr("__compile_time__") = COMPILE_TIME; // Compilation time
  m.attr("__compile_hash__") = COMPILE_HASH; // Git commit hash

  m.def("_initialize", &kburn_initialize);

  m.add_object("_cleanup", py::capsule([]() {
                 kburn_deinitialize();
                 pybind11::detail::g_python_shutdown.store(
                     true, std::memory_order_release);
               }));

  // Log
  m.def("log", &spdlog_log);
  m.def("set_log_level", &spdlog_set_log_level);
  m.def("get_log_level", &spdlog_get_log_level);

  m.def("set_custom_logger",
        [](std::function<void(int, const std::string &)> callback) {
          spdlog_set_user_logger(callback);
        });

  py::enum_<spdlog::level::level_enum>(m, "LogLevel")
      .value("TRACE", spdlog::level::level_enum::trace)
      .value("DEBUG", spdlog::level::level_enum::debug)
      .value("INFO", spdlog::level::level_enum::info)
      .value("WARN", spdlog::level::level_enum::warn)
      .value("ERROR", spdlog::level::level_enum::err)
      .value("CRITICAL", spdlog::level::level_enum::critical)
      .value("OFF", spdlog::level::level_enum::off)
      .export_values();

  // list device
  m.def("list_device", &list_usb_device_with_vid_pid);

  py::class_<kburn_usb_dev_info>(m, "KBurnUSBDeviceInfo")
      .def_readonly("path", &kburn_usb_dev_info::path)
      .def_readonly("vid", &kburn_usb_dev_info::vid)
      .def_readonly("pid", &kburn_usb_dev_info::pid)
      .def_readonly("type", &kburn_usb_dev_info::type)
      .def("__repr__", [](const kburn_usb_dev_info &dev_info) {
        return fmt::format(
            "<KBurnUSBDeviceInfo path='{}' vid=0x{:04x} pid=0x{:04x} type={}>",
            dev_info.path, dev_info.vid, dev_info.pid,
            static_cast<int>(dev_info.type));
      });

  py::class_<KBurnUSBDeviceList>(m, "KBurnUSBDeviceInfoList")
      .def("size", &KBurnUSBDeviceList::size)
      .def("__len__", &KBurnUSBDeviceList::size)
      .def(
          "__iter__",
          [](KBurnUSBDeviceList &list) {
            // Exposing begin and end using py::make_iterator
            return py::make_iterator(list.begin(), list.end());
          },
          py::keep_alive<0, 1>());

  // get device type
  m.def("get_device_type", &get_usb_dev_type_with_info);

  py::enum_<kburn_usb_dev_type>(m, "KBurnUSBDeviceType")
      .value("INVALID", KBURN_USB_DEV_INVALID)
      .value("BROM", KBURN_USB_DEV_BROM)
      .value("UBOOT", KBURN_USB_DEV_UBOOT)
      .export_values();

  // request burner
  py::enum_<KBurnMediumType>(m, "KBurnMediumType")
      .value("INVALID", KBURN_MEDIUM_INVAILD)
      .value("EMMC", KBURN_MEDIUM_EMMC)
      .value("SDCARD", KBURN_MEDIUM_SDCARD)
      .value("SPINAND", KBURN_MEDIUM_SPI_NAND)
      .value("SPINOR", KBURN_MEDIUM_SPI_NOR)
      .value("OTP", KBURN_MEDIUM_OTP)
      .export_values();

  m.def("request_burner", [](kburn_usb_dev_info &info) -> py::object {
    auto burner = request_burner_with_info(info);

    if (nullptr == burner) {
      return py::none();
    }

    return py::cast(burner, py::return_value_policy::take_ownership);
  });

  py::class_<K230::K230BROMBurner>(m, "K230BROMBurner")
      .def("set_medium_type", &K230::K230BROMBurner::set_medium_type)
      .def("set_custom_progress",
           [](K230::K230BROMBurner &self, py::function py_fn) {
             self.register_progress_fn(
                 [py_fn](void *ctx, size_t current, size_t total) {
                   py_fn(current, total);
                 },
                 nullptr);
           })
      .def_property_readonly("loader",
                             [](K230::K230BROMBurner &self) {
                               const char *loader = nullptr;
                               size_t size = 0;
                               bool result = self.get_loader(&loader, &size);

                               if (result) {
                                 return py::bytes(loader, size);
                               }
                               return py::bytes();
                             })
      .def("boot", &K230::K230BROMBurner::boot_from, py::arg("address") = 0x80360000)
      .def(
          "write",
          [](K230::K230BROMBurner &self, py::bytes data_array,
             uint64_t address = 0x80360000) {
            std::string _str(data_array);

            size_t size = _str.size();
            const void *data = reinterpret_cast<const void *>(_str.data());

            bool result = self.write(data, size, address);

            return result;
          },
          py::arg("data"), py::arg("address") = 0x80360000);

  py::class_<K230::kburn_medium_info>(m, "KburnMediumInfo")
      .def_readwrite("capacity", &K230::kburn_medium_info::capacity)
      .def_readwrite("blk_size", &K230::kburn_medium_info::blk_size)
      .def_readwrite("erase_size", &K230::kburn_medium_info::erase_size)
      // Note that timeout_ms is now a lambda
      .def("timeout_ms",
           [](const K230::K230UBOOTBurner &self) { return self.get_timeout_ms(); })
      .def("wp", [](const K230::K230UBOOTBurner &self) { return self.get_wp(); })
      .def("type", [](const K230::K230UBOOTBurner &self) { return self.get_type(); })
      .def("valid",
           [](const K230::K230UBOOTBurner &self) { return self.get_valid(); });

  py::class_<K230::K230UBOOTBurner>(m, "K230UBOOTBurner")
      .def("set_medium_type", &K230::K230UBOOTBurner::set_medium_type)
      .def("set_custom_progress",
           [](K230::K230UBOOTBurner &self, py::function py_fn) {
             self.register_progress_fn(
                 [py_fn](void *ctx, size_t current, size_t total) {
                   py_fn(current, total);
                 },
                 nullptr);
           })
      .def_property_readonly(
          "medium_info",
          [](K230::K230UBOOTBurner &self) -> py::object {
            K230::kburn_medium_info *info = self.get_medium_info();
            if (info) {
              return py::cast(*info); // Create a new object for Python
            } else {
              return py::none(); // Return None if info is nullptr
            }
          })
      .def("probe", &K230::K230UBOOTBurner::probe)
      .def("reboot", &K230::K230UBOOTBurner::reboot)
      .def(
          "write",
          [](K230::K230UBOOTBurner &self, py::bytes data_array,
             uint64_t address = 0x00) {
            std::string _str(data_array);

            size_t size = _str.size();
            const void *data = reinterpret_cast<const void *>(_str.data());

            bool result = self.write(data, size, address);

            return result;
          },
          py::arg("data"), py::arg("address") = 0x00);
}
