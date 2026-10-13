#include "core/afs.hpp"

#include <cstdio>

namespace imbk {
namespace {
constexpr std::size_t kNameRecord = 48; // char name[32]; u16 date[6]; u32 size
}

Afs Afs::parse(Slice archive) {
    const auto header = archive.read(0, 8);
    if (!startsWith(header, 0, std::string_view("AFS\0", 4))) throw FormatError("not an AFS archive");
    const auto count = le32(header, 4);
    if (count > (archive.size() - 8) / 8) throw FormatError("AFS entry count out of range");

    // TOC is followed by the (offset, size) of the name table.
    const auto toc = archive.read(8, count * 8 + 8);
    Afs afs;
    afs.archive_ = archive;
    afs.entries_.resize(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        auto& e = afs.entries_[i];
        e.offset = le32(toc, i * 8);
        e.size = le32(toc, i * 8 + 4);
        if (e.offset > archive.size() || e.size > archive.size() - e.offset)
            throw FormatError("AFS entry " + std::to_string(i) + " out of bounds");
    }

    std::uint32_t namesOffset = le32(toc, count * 8);
    std::uint32_t namesSize = le32(toc, count * 8 + 4);
    // Some AFS writers put the name-table pointer just before the first file instead.
    if (namesOffset == 0 && count > 0 && afs.entries_[0].offset >= 8) {
        const auto alt = archive.read(afs.entries_[0].offset - 8, 8);
        namesOffset = le32(alt, 0);
        namesSize = le32(alt, 4);
    }
    const bool hasNames = namesOffset != 0 && namesSize >= count * kNameRecord &&
                          namesOffset <= archive.size() && namesSize <= archive.size() - namesOffset;
    const auto names = hasNames ? archive.read(namesOffset, count * kNameRecord) : Bytes{};
    for (std::uint32_t i = 0; i < count; ++i) {
        auto& e = afs.entries_[i];
        if (hasNames) e.name = cstr(names, i * kNameRecord, 32);
        if (e.name.empty()) {
            char buf[32];
            std::snprintf(buf, sizeof buf, "entry_%05u.bin", i);
            e.name = buf;
        }
    }
    return afs;
}

const Afs::Entry* Afs::find(std::string_view name) const {
    for (const auto& e : entries_)
        if (e.name == name) return &e;
    return nullptr;
}

} // namespace imbk
