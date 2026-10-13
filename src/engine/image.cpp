#include "engine/image.hpp"

#include <algorithm>
#include <bit>

namespace imbk::engine {
namespace {

std::uint32_t rgb565(std::uint16_t c) {
    const std::uint32_t r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
    return 0xFF000000u | ((r * 255 + 15) / 31) << 16 | ((g * 255 + 31) / 63) << 8 | ((b * 255 + 15) / 31);
}

std::uint32_t mix(std::uint32_t a, std::uint32_t b, int wa, int wb, int div) {
    std::uint32_t out = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        const std::uint32_t ca = (a >> shift) & 0xFF, cb = (b >> shift) & 0xFF;
        out |= ((ca * wa + cb * wb) / div) << shift;
    }
    return out;
}

// Colour part of a DXT block (8 bytes). DXT1 blocks with c0 <= c1 use 3 colours + transparent black.
void decodeColorBlock(ByteSpan d, std::size_t o, bool dxt1, std::uint32_t out[16]) {
    const std::uint16_t c0 = le16(d, o), c1 = le16(d, o + 2);
    std::uint32_t pal[4] = {rgb565(c0), rgb565(c1), 0, 0};
    if (c0 > c1 || !dxt1) {
        pal[2] = mix(pal[0], pal[1], 2, 1, 3);
        pal[3] = mix(pal[0], pal[1], 1, 2, 3);
    } else {
        pal[2] = mix(pal[0], pal[1], 1, 1, 2);
        pal[3] = 0;
    }
    const std::uint32_t bits = le32(d, o + 4);
    for (int i = 0; i < 16; ++i) out[i] = pal[(bits >> (2 * i)) & 3];
}

void setAlpha(std::uint32_t& px, std::uint32_t a) { px = (px & 0x00FFFFFF) | (a << 24); }

} // namespace

Image decodeDds(ByteSpan d) {
    if (!startsWith(d, 0, "DDS ")) throw FormatError("not a DDS file");
    Image img;
    img.height = static_cast<int>(le32(d, 12));
    img.width = static_cast<int>(le32(d, 16));
    if (img.width <= 0 || img.height <= 0 || img.width > 8192 || img.height > 8192) throw FormatError("bad DDS size");
    const auto pfFlags = le32(d, 80);
    const auto bitCount = le32(d, 88);
    img.pixels.assign(static_cast<std::size_t>(img.width) * img.height, 0);
    const std::size_t data = 128;

    if (pfFlags & 4) { // FourCC
        const bool dxt1 = startsWith(d, 84, "DXT1"), dxt3 = startsWith(d, 84, "DXT3"), dxt5 = startsWith(d, 84, "DXT5");
        if (!dxt1 && !dxt3 && !dxt5) throw FormatError("unsupported DDS compression");
        const std::size_t blockSize = dxt1 ? 8 : 16;
        const int bw = (img.width + 3) / 4, bh = (img.height + 3) / 4;
        require(d, data, static_cast<std::size_t>(bw) * bh * blockSize, "DDS blocks");
        std::uint32_t block[16];
        for (int by = 0; by < bh; ++by)
            for (int bx = 0; bx < bw; ++bx) {
                const std::size_t o = data + (static_cast<std::size_t>(by) * bw + bx) * blockSize;
                decodeColorBlock(d, dxt1 ? o : o + 8, dxt1, block);
                if (dxt3) {
                    for (int i = 0; i < 16; ++i) {
                        const std::uint32_t a4 = (d[o + i / 2] >> ((i & 1) * 4)) & 15;
                        setAlpha(block[i], a4 * 17);
                    }
                } else if (dxt5) {
                    const std::uint32_t a0 = d[o], a1 = d[o + 1];
                    std::uint32_t ap[8] = {a0, a1};
                    for (int i = 2; i < 8; ++i)
                        ap[i] = a0 > a1 ? (a0 * (8 - i) + a1 * (i - 1)) / 7 : i < 6 ? (a0 * (6 - i) + a1 * (i - 1)) / 5 : (i == 6 ? 0 : 255);
                    std::uint64_t bits = 0;
                    for (int i = 0; i < 6; ++i) bits |= static_cast<std::uint64_t>(d[o + 2 + i]) << (8 * i);
                    for (int i = 0; i < 16; ++i) setAlpha(block[i], ap[(bits >> (3 * i)) & 7]);
                }
                for (int y = 0; y < 4; ++y)
                    for (int x = 0; x < 4; ++x) {
                        const int px = bx * 4 + x, py = by * 4 + y;
                        if (px < img.width && py < img.height) img.pixels[static_cast<std::size_t>(py) * img.width + px] = block[y * 4 + x];
                    }
            }
        return img;
    }

    if (bitCount != 32 && bitCount != 24) throw FormatError("unsupported DDS pixel format");
    const std::uint32_t masks[4] = {le32(d, 92), le32(d, 96), le32(d, 100), (pfFlags & 1) ? le32(d, 104) : 0};
    const std::size_t bpp = bitCount / 8;
    require(d, data, static_cast<std::size_t>(img.width) * img.height * bpp, "DDS pixels");
    auto channel = [](std::uint32_t v, std::uint32_t mask) -> std::uint32_t {
        if (!mask) return 255;
        const int shift = std::countr_zero(mask);
        const std::uint32_t max = mask >> shift;
        return ((v & mask) >> shift) * 255 / max;
    };
    for (std::size_t i = 0; i < img.pixels.size(); ++i) {
        const std::size_t o = data + i * bpp;
        const std::uint32_t v = d[o] | d[o + 1] << 8 | d[o + 2] << 16 | (bpp == 4 ? static_cast<std::uint32_t>(d[o + 3]) << 24 : 0);
        img.pixels[i] = channel(v, masks[3]) << 24 | channel(v, masks[0]) << 16 | channel(v, masks[1]) << 8 | channel(v, masks[2]);
    }
    return img;
}

Image resize(const Image& src, int width, int height) {
    Image out;
    out.width = width;
    out.height = height;
    out.pixels.resize(static_cast<std::size_t>(width) * height);
    if (src.width == width && src.height == height) {
        out.pixels = src.pixels;
        return out;
    }
    for (int y = 0; y < height; ++y) {
        const float sy = std::max(0.0f, (y + 0.5f) * src.height / height - 0.5f);
        const int y0 = std::min(static_cast<int>(sy), src.height - 1), y1 = std::min(y0 + 1, src.height - 1);
        const int fy = static_cast<int>((sy - y0) * 256);
        for (int x = 0; x < width; ++x) {
            const float sx = std::max(0.0f, (x + 0.5f) * src.width / width - 0.5f);
            const int x0 = std::min(static_cast<int>(sx), src.width - 1), x1 = std::min(x0 + 1, src.width - 1);
            const int fx = static_cast<int>((sx - x0) * 256);
            const auto p = [&](int px, int py) { return src.pixels[static_cast<std::size_t>(py) * src.width + px]; };
            const auto top = mix(p(x0, y0), p(x1, y0), 256 - fx, fx, 256);
            const auto bottom = mix(p(x0, y1), p(x1, y1), 256 - fx, fx, 256);
            out.pixels[static_cast<std::size_t>(y) * width + x] = mix(top, bottom, 256 - fy, fy, 256);
        }
    }
    return out;
}

} // namespace imbk::engine
