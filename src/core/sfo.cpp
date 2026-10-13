#include "core/sfo.hpp"

namespace imbk {

std::map<std::string, std::string> parseSfo(ByteSpan d) {
    if (!startsWith(d, 0, std::string_view("\0PSF", 4))) throw FormatError("not a PARAM.SFO file");
    const auto keyTable = le32(d, 8);
    const auto dataTable = le32(d, 12);
    const auto count = le32(d, 16);
    std::map<std::string, std::string> out;
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::size_t e = 20 + i * 16;
        const auto key = cstr(d, keyTable + le16(d, e), 64);
        const auto fmt = le16(d, e + 2);
        const auto len = le32(d, e + 4);
        const std::size_t data = dataTable + le32(d, e + 12);
        if (fmt == 0x0404) {
            out[key] = std::to_string(le32(d, data));
        } else {
            require(d, data, len, "SFO value");
            std::string v(reinterpret_cast<const char*>(&d[data]), len);
            while (!v.empty() && v.back() == '\0') v.pop_back();
            out[key] = v;
        }
    }
    return out;
}

} // namespace imbk
