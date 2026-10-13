#pragma once

#include "core/binary.hpp"

namespace imbk {

std::uint32_t crc32(ByteSpan data, std::uint32_t crc = 0);

} // namespace imbk
