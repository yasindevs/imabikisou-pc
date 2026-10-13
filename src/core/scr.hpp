#pragma once

#include "core/binary.hpp"

namespace imbk::scr {

// Pieces shared by both script formats (scenario.sn4 and the files inside st.sns): each script file has a
// 0x30-byte header, a 256-bucket label hash table followed by the code offset, chained label entries, then code.

constexpr std::size_t kHeaderSize = 0x30;
constexpr std::size_t kBuckets = 256;
constexpr std::size_t kLabelHeaderSize = 8; // u16 next, u16 tag, u32 target; the name follows
constexpr std::uint16_t kSceneLabelTag = 0x6F4E; // "No"
constexpr std::uint16_t kLocalLabelTag = 0xFFFF; // "_AMIn" labels used by sn4 choices

struct Label {
    std::string name;
    std::uint16_t tag = 0;
    std::uint32_t target = 0; // offset in code, relative to the code base
};

struct Layout {
    std::size_t table = 0;    // absolute offset of the bucket table
    std::size_t codeBase = 0; // absolute offset of the bytecode
    std::vector<Label> labels;
};

// `base` is the start of the script file (bucket, entry and code offsets are relative to it);
// the bucket table starts kHeaderSize bytes after it.
Layout parseLayout(ByteSpan d, std::size_t base);

} // namespace imbk::scr
