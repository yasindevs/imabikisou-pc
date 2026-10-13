#pragma once

#include "core/file.hpp"

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

private:
    Slice archive_;
    std::vector<Entry> entries_;
};

} // namespace imbk
