#include "core/scr.hpp"

#include <algorithm>

namespace imbk::scr {

Layout parseLayout(ByteSpan d, std::size_t base) {
    Layout l;
    l.table = base + kHeaderSize;
    l.codeBase = base + le32(d, l.table + kBuckets * 4);
    require(d, l.codeBase, 0, "code base");

    for (std::size_t b = 0; b < kBuckets; ++b) {
        std::size_t e = le32(d, l.table + b * 4);
        if (e == 0) continue;
        e += base;
        for (std::size_t guard = 0;; ++guard) {
            // Entry: u16 next (= entry size, 0 = end of chain), u16 tag, u32 target, NUL-terminated name
            // padded to 4 bytes. sn4 entries are always 24 bytes; st.sns names can be longer.
            if (e + kLabelHeaderSize >= l.codeBase || guard > 4096) throw FormatError("corrupt label chain");
            const auto next = le16(d, e);
            if (next != 0 && next <= kLabelHeaderSize) throw FormatError("corrupt label entry");
            Label label;
            label.tag = le16(d, e + 2);
            label.target = le32(d, e + 4);
            label.entry = static_cast<std::uint32_t>(e);
            label.name = cstr(d, e + kLabelHeaderSize, std::min<std::size_t>(255, l.codeBase - e - kLabelHeaderSize));
            l.labels.push_back(std::move(label));
            if (next == 0) break;
            e += next;
        }
    }
    return l;
}

} // namespace imbk::scr
