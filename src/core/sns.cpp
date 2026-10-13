#include "core/sns.hpp"

namespace imbk::sns {

bool isSns(ByteSpan data) { return data.size() >= kKeySize && startsWith(data, 0, kMagic); }

std::string version(ByteSpan data) {
    if (!isSns(data)) throw FormatError("not a CSD-CSCNV script");
    return cstr(data, kMagic.size(), kKeySize - kMagic.size());
}

void decrypt(Bytes& data) {
    if (!isSns(data)) throw FormatError("not a CSD-CSCNV script");
    for (std::size_t i = kKeySize; i < data.size(); ++i)
        data[i] = static_cast<std::uint8_t>(data[i] - data[i % kKeySize]);
}

void encrypt(Bytes& data) {
    if (!isSns(data)) throw FormatError("not a CSD-CSCNV script");
    for (std::size_t i = kKeySize; i < data.size(); ++i)
        data[i] = static_cast<std::uint8_t>(data[i] + data[i % kKeySize]);
}

} // namespace imbk::sns
