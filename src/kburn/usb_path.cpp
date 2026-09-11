#include "usb_path.h"

#include <cstdio>

namespace Kendryte_Burning_Tool::internal {

bool format_usb_port_path(uint8_t bus, const uint8_t *ports,
                          std::size_t port_count, char *path_buffer,
                          std::size_t path_buffer_size) {
  if (!path_buffer || path_buffer_size == 0) {
    return false;
  }
  path_buffer[0] = '\0';

  if (!ports || port_count == 0 || port_count > kUsbMaxPortDepth) {
    return false;
  }

  int written = std::snprintf(path_buffer, path_buffer_size, "%u",
                              static_cast<unsigned>(bus));
  if (written < 0 || static_cast<std::size_t>(written) >= path_buffer_size) {
    path_buffer[0] = '\0';
    return false;
  }

  std::size_t offset = static_cast<std::size_t>(written);
  for (std::size_t index = 0; index < port_count; ++index) {
    const std::size_t remaining = path_buffer_size - offset;
    written = std::snprintf(path_buffer + offset, remaining, "%c%u",
                            index == 0 ? '-' : '.',
                            static_cast<unsigned>(ports[index]));
    if (written < 0 || static_cast<std::size_t>(written) >= remaining) {
      path_buffer[0] = '\0';
      return false;
    }
    offset += static_cast<std::size_t>(written);
  }

  return true;
}

} // namespace Kendryte_Burning_Tool::internal
