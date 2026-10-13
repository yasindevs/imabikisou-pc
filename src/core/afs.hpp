#pragma once

#include "core/file.hpp"

#include <map>
#include <optional>

namespace imbk {

// CRI "AFS" archive: a table of (offset, size) pairs plus an optional table of 48-byte name records.
class Afs {
public:
    struct Entry {
        std::string name;
        std::uint32_t offset = 0;
        std::uint32_t size = 0;
    };

    static Afs parse(Slice archive);

    const std::vector<Entry>& entries() const { return entries_; }
    const Entry* find(std::string_view name) const;
    Slice data(const Entry& e) const { return archive_.sub(e.offset, e.size); }

    // Builds a copy of the archive with some entries replaced (by name), keeping the original layout:
    // same entry order, 2048-byte alignment, name table after the data. Without replacements the
    // result is identical to the original archive.
    Bytes rebuild(const std::map<std::string, Bytes>& replacements) const;

private:
    Slice archive_;
    std::vector<Entry> entries_;
    std::uint32_t namesOffset_ = 0, namesSize_ = 0;
    std::size_t namesPointerAt_ = 0; // where the (offset, size) of the name table is stored
};

} // namespace imbk
