#include "core/afs.hpp"

#include <cstdio>

namespace imbk {
namespace {
constexpr std::size_t kNameRecord = 48; // char name[32]; u16 date[6]; u32 size
constexpr std::size_t kAlign = 0x800;

std::size_t alignUp(std::size_t v) { return (v + kAlign - 1) / kAlign * kAlign; }

void put32(Bytes& b, std::size_t o, std::uint32_t v) {
    if (b.size() < o + 4) b.resize(o + 4);
    for (int i = 0; i < 4; ++i) b[o + i] = static_cast<std::uint8_t>(v >> (8 * i));
}
} // namespace

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

    afs.namesPointerAt_ = 8 + count * 8;
    std::uint32_t namesOffset = le32(toc, count * 8);
    std::uint32_t namesSize = le32(toc, count * 8 + 4);
    // Some AFS writers put the name-table pointer just before the first file instead.
    if (namesOffset == 0 && count > 0 && afs.entries_[0].offset >= 8) {
        afs.namesPointerAt_ = afs.entries_[0].offset - 8;
        const auto alt = archive.read(afs.namesPointerAt_, 8);
        namesOffset = le32(alt, 0);
        namesSize = le32(alt, 4);
    }
    const bool hasNames = namesOffset != 0 && namesSize >= count * kNameRecord &&
                          namesOffset <= archive.size() && namesSize <= archive.size() - namesOffset;
    if (hasNames) {
        afs.namesOffset_ = namesOffset;
        afs.namesSize_ = namesSize;
    }
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

Bytes Afs::rebuild(const std::map<std::string, Bytes>& replacements) const {
    for (const auto& [name, _] : replacements)
        if (!find(name)) throw std::runtime_error("AFS has no entry named " + name);

    // Everything before the first entry (header, TOC, padding) is kept, then rewritten.
    const std::size_t dataStart = entries_.empty() ? alignUp(namesPointerAt_ + 8) : entries_.front().offset;
    Bytes out = archive_.read(0, dataStart);
    std::vector<std::uint32_t> sizes;
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        const auto& e = entries_[i];
        const auto it = replacements.find(e.name);
        const Bytes data = it != replacements.end() ? it->second : archive_.read(e.offset, e.size);
        out.resize(alignUp(out.size()));
        put32(out, 8 + i * 8, static_cast<std::uint32_t>(out.size()));
        put32(out, 12 + i * 8, static_cast<std::uint32_t>(data.size()));
        sizes.push_back(static_cast<std::uint32_t>(data.size()));
        out.insert(out.end(), data.begin(), data.end());
    }
    if (namesOffset_) {
        auto names = archive_.read(namesOffset_, namesSize_);
        // The last field of a name record usually repeats the entry size; only update it when it does.
        for (std::size_t i = 0; i < entries_.size(); ++i)
            if (le32(names, i * kNameRecord + 44) == entries_[i].size) put32(names, i * kNameRecord + 44, sizes[i]);
        out.resize(alignUp(out.size()));
        put32(out, namesPointerAt_, static_cast<std::uint32_t>(out.size()));
        put32(out, namesPointerAt_ + 4, namesSize_);
        out.insert(out.end(), names.begin(), names.end());
    }
    out.resize(alignUp(out.size()));
    return out;
}

} // namespace imbk
