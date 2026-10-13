#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace imbk {

using Bytes = std::vector<std::uint8_t>;
using ByteSpan = std::span<const std::uint8_t>;

// Thrown when game data does not match the expected layout.
struct FormatError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

inline void require(ByteSpan data, std::size_t offset, std::size_t size, const char* what) {
    if (offset > data.size() || size > data.size() - offset)
        throw FormatError(std::string(what) + ": read past end of data");
}

inline std::uint16_t le16(ByteSpan d, std::size_t o) {
    require(d, o, 2, "le16");
    return static_cast<std::uint16_t>(d[o] | d[o + 1] << 8);
}

inline std::uint32_t le32(ByteSpan d, std::size_t o) {
    require(d, o, 4, "le32");
    return static_cast<std::uint32_t>(d[o]) | static_cast<std::uint32_t>(d[o + 1]) << 8 |
           static_cast<std::uint32_t>(d[o + 2]) << 16 | static_cast<std::uint32_t>(d[o + 3]) << 24;
}

inline std::uint16_t be16(ByteSpan d, std::size_t o) {
    require(d, o, 2, "be16");
    return static_cast<std::uint16_t>(d[o] << 8 | d[o + 1]);
}

inline std::uint32_t be32(ByteSpan d, std::size_t o) {
    require(d, o, 4, "be32");
    return static_cast<std::uint32_t>(d[o]) << 24 | static_cast<std::uint32_t>(d[o + 1]) << 16 |
           static_cast<std::uint32_t>(d[o + 2]) << 8 | static_cast<std::uint32_t>(d[o + 3]);
}

// Reads a NUL-terminated ASCII string of at most `max` bytes.
inline std::string cstr(ByteSpan d, std::size_t o, std::size_t max) {
    require(d, o, 0, "cstr");
    std::string s;
    for (std::size_t i = o; i < d.size() && i < o + max && d[i] != 0; ++i) s.push_back(static_cast<char>(d[i]));
    return s;
}

inline bool startsWith(ByteSpan d, std::size_t o, std::string_view magic) {
    if (o > d.size() || magic.size() > d.size() - o) return false;
    for (std::size_t i = 0; i < magic.size(); ++i)
        if (d[o + i] != static_cast<std::uint8_t>(magic[i])) return false;
    return true;
}

} // namespace imbk
