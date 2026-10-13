#pragma once

#include "core/binary.hpp"

namespace imbk {

// Shift-JIS (Microsoft code page 932) -> UTF-8.
std::string sjisToUtf8(ByteSpan sjis);

} // namespace imbk
