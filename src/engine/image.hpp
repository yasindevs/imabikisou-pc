#pragma once

#include "core/binary.hpp"

namespace imbk::engine {

// 32-bit image, top-down rows, pixels as 0xAARRGGBB (BGRA bytes in memory on little-endian hosts).
struct Image {
    int width = 0;
    int height = 0;
    std::vector<std::uint32_t> pixels;
};

// DirectDraw Surface: DXT1/DXT3/DXT5 or uncompressed 24/32-bit (first mip level only).
Image decodeDds(ByteSpan dds);

// Bilinear resize.
Image resize(const Image& src, int width, int height);

} // namespace imbk::engine
