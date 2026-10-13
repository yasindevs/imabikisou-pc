#pragma once

#include "core/binary.hpp"

namespace imbk::sns {

// Main scenario script (residentchunk.afs/st.sns), "CSD-CSCNV" format. See docs/sns-format.md.
//
// The first 16 bytes are both the magic ("CSD-CSCNV:01.68\0") and the key: every following byte is
// stored as plain[i] + key[i % 16] (mod 256). Recovered from the game's loader (EBOOT 0x00090418).

constexpr std::size_t kKeySize = 16;
constexpr std::string_view kMagic = "CSD-CSCNV:";

bool isSns(ByteSpan data);
std::string version(ByteSpan data); // e.g. "01.68"

void decrypt(Bytes& data);
void encrypt(Bytes& data);

} // namespace imbk::sns
