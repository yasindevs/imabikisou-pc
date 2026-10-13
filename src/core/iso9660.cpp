#include "core/iso9660.hpp"

#include <algorithm>
#include <cctype>

namespace imbk {
namespace {

constexpr std::uint64_t kSector = 2048;

bool iequals(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}

std::vector<std::string_view> splitPath(std::string_view path) {
    std::vector<std::string_view> parts;
    while (!path.empty()) {
        const auto slash = path.find_first_of("/\\");
        const auto part = path.substr(0, slash);
        if (!part.empty()) parts.push_back(part);
        if (slash == std::string_view::npos) break;
        path.remove_prefix(slash + 1);
    }
    return parts;
}

// Directory record -> Entry. Returns the record length (0 = padding up to the next sector).
// `moreExtents` is set when the file continues in the next record (multi-extent file).
std::size_t parseRecord(ByteSpan d, std::size_t o, bool joliet, Iso9660::Entry& out, bool& moreExtents) {
    const std::size_t len = d[o];
    if (len == 0) return 0;
    require(d, o, len, "ISO directory record");
    if (len < 34) throw FormatError("ISO directory record too short");
    const std::size_t nameLen = d[o + 32];
    require(d, o + 33, nameLen, "ISO file name");
    moreExtents = (d[o + 25] & 0x80) != 0;

    out.offset = static_cast<std::uint64_t>(le32(d, o + 2)) * kSector;
    out.size = le32(d, o + 10);
    out.isDirectory = (d[o + 25] & 0x02) != 0;
    out.name.clear();
    if (nameLen == 1 && (d[o + 33] == 0 || d[o + 33] == 1)) {
        out.name = d[o + 33] == 0 ? "." : "..";
        return len;
    }
    if (joliet) {
        for (std::size_t i = 0; i + 1 < nameLen; i += 2) {
            const unsigned c = be16(d, o + 33 + i);
            out.name.push_back(c < 0x80 ? static_cast<char>(c) : '?');
        }
    } else {
        out.name.assign(reinterpret_cast<const char*>(&d[o + 33]), nameLen);
    }
    if (const auto semi = out.name.find(';'); semi != std::string::npos) out.name.resize(semi);
    if (!out.name.empty() && out.name.back() == '.') out.name.pop_back();
    return len;
}

} // namespace

Iso9660::Iso9660(const std::filesystem::path& image) : image_(std::make_shared<const File>(image)) {
    std::optional<Entry> primary, joliet;
    Bytes vd(kSector);
    for (std::uint64_t sector = 16; sector < 64; ++sector) {
        image_->read(sector * kSector, vd.data(), vd.size());
        if (!startsWith(vd, 1, "CD001")) throw FormatError("not an ISO 9660 image");
        const auto type = vd[0];
        if (type == 255) break;
        const bool isJoliet = type == 2 && vd[88] == '%' && vd[89] == '/' && (vd[90] == '@' || vd[90] == 'C' || vd[90] == 'E');
        if (type != 1 && !isJoliet) continue;
        if (le16(vd, 128) != kSector) throw FormatError("unsupported ISO logical block size");
        Entry root;
        bool more = false;
        parseRecord(vd, 156, isJoliet, root, more);
        (isJoliet ? joliet : primary) = root;
    }
    if (!primary && !joliet) throw FormatError("ISO image has no volume descriptor");
    joliet_ = joliet.has_value();
    root_ = joliet ? *joliet : *primary;
}

std::vector<Iso9660::Entry> Iso9660::readDirectory(const Entry& dir) const {
    Bytes d(static_cast<std::size_t>(dir.size));
    image_->read(dir.offset, d.data(), d.size());
    std::vector<Entry> entries;
    bool continuing = false; // previous record had the multi-extent flag
    for (std::size_t o = 0; o < d.size();) {
        Entry e;
        bool more = false;
        const auto len = parseRecord(d, o, joliet_, e, more);
        if (len == 0) { // records never straddle sectors; skip the padding
            o = (o / kSector + 1) * kSector;
            continue;
        }
        o += len;
        if (continuing) {
            // Following extent of the same file: only contiguous extents can be exposed as one slice.
            auto& prev = entries.back();
            if (e.name != prev.name || e.offset != prev.offset + prev.size)
                throw FormatError("non-contiguous multi-extent file: " + prev.name);
            prev.size += e.size;
        } else if (e.name != "." && e.name != "..") {
            entries.push_back(std::move(e));
        }
        continuing = more;
    }
    if (continuing) throw FormatError("truncated multi-extent file in ISO directory");
    return entries;
}

std::optional<Iso9660::Entry> Iso9660::find(std::string_view path) const {
    Entry cur = root_;
    for (auto part : splitPath(path)) {
        if (!cur.isDirectory) return std::nullopt;
        const auto entries = readDirectory(cur);
        const auto it = std::find_if(entries.begin(), entries.end(), [&](const Entry& e) { return iequals(e.name, part); });
        if (it == entries.end()) return std::nullopt;
        cur = *it;
    }
    return cur;
}

std::vector<Iso9660::Entry> Iso9660::list(std::string_view dirPath) const {
    const auto dir = find(dirPath);
    if (!dir || !dir->isDirectory) throw std::runtime_error("no such directory in ISO: " + std::string(dirPath));
    return readDirectory(*dir);
}

std::vector<std::string> Iso9660::allFiles() const {
    std::vector<std::string> out;
    std::vector<std::pair<std::string, Entry>> stack{{"", root_}};
    while (!stack.empty()) {
        auto [prefix, dir] = stack.back();
        stack.pop_back();
        for (const auto& e : readDirectory(dir)) {
            const auto path = prefix.empty() ? e.name : prefix + "/" + e.name;
            if (e.isDirectory) stack.emplace_back(path, e);
            else out.push_back(path);
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

Slice Iso9660::open(std::string_view path) const {
    const auto e = find(path);
    if (!e || e->isDirectory) throw std::runtime_error("no such file in ISO: " + std::string(path));
    return Slice(image_, e->offset, e->size);
}

} // namespace imbk
