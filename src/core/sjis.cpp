#include "core/sjis.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <cerrno>
#include <iconv.h>
#endif

namespace imbk {

#ifdef _WIN32

std::string sjisToUtf8(ByteSpan sjis) {
    if (sjis.empty()) return {};
    const auto* src = reinterpret_cast<const char*>(sjis.data());
    const int srcLen = static_cast<int>(sjis.size());
    const int wlen = MultiByteToWideChar(932, MB_ERR_INVALID_CHARS, src, srcLen, nullptr, 0);
    if (wlen <= 0) throw FormatError("invalid Shift-JIS text");
    std::wstring wide(static_cast<std::size_t>(wlen), L'\0');
    MultiByteToWideChar(932, MB_ERR_INVALID_CHARS, src, srcLen, wide.data(), wlen);
    const int len = WideCharToMultiByte(CP_UTF8, 0, wide.data(), wlen, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), wlen, out.data(), len, nullptr, nullptr);
    return out;
}

#else

std::string sjisToUtf8(ByteSpan sjis) {
    if (sjis.empty()) return {};
    iconv_t cd = iconv_open("UTF-8", "CP932");
    if (cd == reinterpret_cast<iconv_t>(-1)) throw std::runtime_error("iconv does not support CP932");
    std::string out(sjis.size() * 3 + 4, '\0');
    auto* in = const_cast<char*>(reinterpret_cast<const char*>(sjis.data()));
    std::size_t inLeft = sjis.size();
    char* dst = out.data();
    std::size_t outLeft = out.size();
    const auto r = iconv(cd, &in, &inLeft, &dst, &outLeft);
    iconv_close(cd);
    if (r == static_cast<std::size_t>(-1)) throw FormatError("invalid Shift-JIS text");
    out.resize(out.size() - outLeft);
    return out;
}

#endif

} // namespace imbk
