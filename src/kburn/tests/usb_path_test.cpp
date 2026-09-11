#include "usb_path.h"

#include <array>
#include <cstring>
#include <iostream>

namespace internal = Kendryte_Burning_Tool::internal;

template <std::size_t PortCount>
bool expect_path(uint8_t bus, const std::array<uint8_t, PortCount> &ports,
                 const char *expected) {
  char actual[internal::kUsbPortPathBufferSize];
  if (!internal::format_usb_port_path(bus, ports.data(), ports.size(), actual,
                                      sizeof(actual))) {
    std::cerr << "Expected a valid path: " << expected << '\n';
    return false;
  }
  if (std::strcmp(actual, expected) != 0) {
    std::cerr << "Expected " << expected << ", got " << actual << '\n';
    return false;
  }
  return true;
}

int main() {
  bool passed = true;

  passed &= expect_path(1, std::array<uint8_t, 1>{1}, "1-1");
  passed &= expect_path(1, std::array<uint8_t, 3>{5, 3, 1}, "1-5.3.1");
  passed &= expect_path(1, std::array<uint8_t, 3>{5, 4, 1}, "1-5.4.1");
  passed &= expect_path(
      255, std::array<uint8_t, 7>{255, 255, 255, 255, 255, 255, 255},
      "255-255.255.255.255.255.255.255");

  char path[internal::kUsbPortPathBufferSize] = "unchanged";
  const std::array<uint8_t, 1> direct{1};
  if (internal::format_usb_port_path(1, direct.data(), 0, path, sizeof(path)) ||
      path[0] != '\0') {
    std::cerr << "A path without ports must be rejected\n";
    passed = false;
  }

  char small_path[7] = "stale";
  const std::array<uint8_t, 3> hub_path{5, 3, 2};
  if (internal::format_usb_port_path(1, hub_path.data(), hub_path.size(),
                                     small_path, sizeof(small_path)) ||
      small_path[0] != '\0') {
    std::cerr << "A truncated path must be rejected\n";
    passed = false;
  }

  const std::array<uint8_t, 8> too_deep{1, 1, 1, 1, 1, 1, 1, 1};
  if (internal::format_usb_port_path(1, too_deep.data(), too_deep.size(), path,
                                     sizeof(path)) ||
      path[0] != '\0') {
    std::cerr << "A path deeper than the USB limit must be rejected\n";
    passed = false;
  }

  return passed ? 0 : 1;
}
