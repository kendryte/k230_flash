#pragma once

#include <cstddef>
#include <cstdint>

namespace Kendryte_Burning_Tool::internal {

inline constexpr std::size_t kUsbMaxPortDepth = 7;
inline constexpr std::size_t kUsbPortPathBufferSize = 32;

bool format_usb_port_path(uint8_t bus, const uint8_t *ports,
                          std::size_t port_count, char *path_buffer,
                          std::size_t path_buffer_size);

} // namespace Kendryte_Burning_Tool::internal
