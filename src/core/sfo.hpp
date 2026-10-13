#pragma once

#include "core/binary.hpp"

#include <map>

namespace imbk {

// PARAM.SFO: key/value metadata of a PS3 title. Integers are returned as decimal strings.
std::map<std::string, std::string> parseSfo(ByteSpan data);

} // namespace imbk
