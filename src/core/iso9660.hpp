#pragma once

#include "core/file.hpp"

#include <optional>
#include <string_view>

namespace imbk {

// Minimal read-only ISO 9660 reader (with Joliet names when present), enough for PS3 Blu-ray dumps.
class Iso9660 {
public:
    struct Entry {
        std::string name;
        bool isDirectory = false;
        std::uint64_t offset = 0; // absolute byte offset in the image
        std::uint64_t size = 0;
    };

    explicit Iso9660(const std::filesystem::path& image);

    // Paths use '/' separators and are matched case-insensitively, e.g. "PS3_GAME/PARAM.SFO".
    std::optional<Entry> find(std::string_view path) const;
    std::vector<Entry> list(std::string_view dirPath) const;
    Slice open(std::string_view path) const;
    std::vector<std::string> allFiles() const; // every file path in the image, '/'-separated

private:
    std::vector<Entry> readDirectory(const Entry& dir) const;

    std::shared_ptr<const File> image_;
    Entry root_;
    bool joliet_ = false;
};

} // namespace imbk
