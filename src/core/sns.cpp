#include "core/sns.hpp"

#include <array>
#include <cstdio>
#include <map>
#include <set>

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

namespace {

constexpr std::size_t kStartLabel = 0xE0;
constexpr std::size_t kDirectory = 0x100;   // u32 count, then (u32 offset, u32 size) per file
constexpr std::size_t kAisacTables = 0x28;  // u32 offset table, u32 string pool
constexpr std::size_t kResourceTables = 0x60; // 4 x (u32 offset table, u32 string pool)

// A string table is an array of u32 offsets into a pool of NUL-terminated strings; the pool starts
// right where the offset array ends.
std::vector<std::string> readStringTable(ByteSpan d, std::size_t table, std::size_t pool) {
    if (pool < table || (pool - table) % 4) throw FormatError("bad string table");
    std::vector<std::string> out((pool - table) / 4);
    for (std::size_t i = 0; i < out.size(); ++i) out[i] = cstr(d, pool + le32(d, table + i * 4), 256);
    return out;
}

std::string hexOf(ByteSpan a) {
    std::string s;
    char b[3];
    for (auto c : a) {
        std::snprintf(b, sizeof b, "%02X", c);
        s += b;
    }
    return s;
}

} // namespace

std::string_view mnemonic(std::uint8_t op) {
    // From the EBOOT's handler table (0x100AE740) and its debug strings; see docs/opcodes.md for the evidence
    // and confidence of each name.
    switch (op) {
    case op::Text: return "text";
    case op::TextEnd: return "text_end";
    case 0x12: return "text_pos";
    case op::Wait: return "wait";
    case op::RubyBegin: return "ruby";
    case op::RubyEnd: return "ruby_end";
    case op::Newline: return "newline";
    case op::Page: return "page";
    case 0x1E: return "scene_box";
    case op::NarrationBegin: return "narration";
    case op::NarrationEnd: return "narration_end";
    case 0x29: return "delay";
    case op::Name: return "name";
    case 0x3C: return "sys_sound";
    case 0x3D: return "movie_pause";
    case 0x3E: return "movie_resume";
    case 0x40: return "picture";
    case op::Branch: return "branch";
    case op::Jump: return "jump";
    case op::Label: return "label";
    case 0x4E: return "if_flag";
    case 0x51: return "call";
    case 0x52: return "ret";
    case 0x59: return "transition_set";
    case 0x5A: return "transition_wait";
    case 0x5B: return "transition_start";
    case 0x60: return "picture_clear";
    case op::MessageBegin: return "message";
    case op::MessageEnd: return "message_end";
    case 0x88: return "bgm_play";
    case 0x8E: return "movie";
    case 0x8F: return "movie_stop";
    case 0x96: return "scene_start";
    case 0x97: return "scene_end";
    case 0x98: return "scene_title";
    case op::Return: return "end";
    }
    static const auto names = [] {
        std::array<std::array<char, 6>, 256> n{};
        for (int i = 0; i < 256; ++i) std::snprintf(n[i].data(), n[i].size(), "op_%02X", i);
        return n;
    }();
    return names[op].data();
}

Script Script::parse(Bytes decrypted) {
    Script s;
    s.data_ = std::move(decrypted);
    const ByteSpan d = s.data_;
    s.version = sns::version(d);
    s.startLabel = cstr(d, kStartLabel, 0x20);

    s.resources.aisac = readStringTable(d, le32(d, kAisacTables), le32(d, kAisacTables + 4));
    std::vector<std::string>* tables[] = {&s.resources.pictures, &s.resources.movies, &s.resources.bgm,
                                          &s.resources.sounds};
    for (std::size_t i = 0; i < 4; ++i)
        *tables[i] = readStringTable(d, le32(d, kResourceTables + i * 8), le32(d, kResourceTables + i * 8 + 4));

    const auto count = le32(d, kDirectory);
    if (count > 4096) throw FormatError("implausible script file count");
    for (std::uint32_t f = 0; f < count; ++f) {
        File file;
        file.offset = le32(d, kDirectory + 4 + f * 8);
        file.size = le32(d, kDirectory + 8 + f * 8);
        require(d, file.offset, file.size, "script file");
        file.name = cstr(d, file.offset, scr::kHeaderSize);
        auto layout = scr::parseLayout(d, file.offset);
        file.codeBase = static_cast<std::uint32_t>(layout.codeBase);
        file.labels = std::move(layout.labels);

        const std::size_t end = file.offset + file.size;
        for (std::size_t o = file.codeBase; o < end;) {
            Instruction in;
            in.offset = static_cast<std::uint32_t>(o - file.codeBase);
            in.opcode = d[o];
            if (in.opcode == 0x00) {
                // Zero padding up to a 4-byte boundary after the last instruction.
                for (std::size_t p = o; p < end; ++p)
                    if (d[p] != 0) throw FormatError(file.name + ": unexpected opcode 00 inside the code");
                if (end - o >= 4) throw FormatError(file.name + ": oversized padding");
                break;
            }
            if (in.opcode == op::RubyBegin || in.opcode == op::RubyEnd) {
                in.argOffset = static_cast<std::uint32_t>(o + 1);
                in.size = 1;
            } else if (in.opcode == op::Text) {
                std::size_t t = o + 1;
                while (t < end && d[t] != op::TextEnd) ++t;
                if (t >= end) throw FormatError(file.name + ": unterminated text");
                in.argOffset = static_cast<std::uint32_t>(o + 1);
                in.argSize = static_cast<std::uint32_t>(t - (o + 1));
                in.size = in.argSize + 2;
            } else {
                require(d, o + 1, 1, "operand length");
                in.argOffset = static_cast<std::uint32_t>(o + 2);
                in.argSize = d[o + 1];
                in.size = 2 + in.argSize;
            }
            if (o + in.size > end) {
                char msg[96];
                std::snprintf(msg, sizeof msg, ": instruction at code offset 0x%X runs past the end of the file", in.offset);
                throw FormatError(file.name + msg);
            }
            file.code.push_back(in);
            o += in.size;
        }
        s.files.push_back(std::move(file));
    }
    return s;
}

std::string Script::labelName(const Instruction& i) const {
    const auto a = args(i); // u8 script file index, NUL-terminated name
    return a.size() < 2 ? std::string() : cstr(a, 1, a.size() - 1);
}

std::optional<std::size_t> labelRefOffset(std::uint8_t op) {
    switch (op) {
    case 0x49: case 0x4A: case 0x4D: case 0x50: case 0x51: case 0x91: case 0x92: case 0x93: return 0;
    case 0x62: case 0x6A: return 3;
    case 0x4B: case 0x4C: return 5;
    case op::MessageBegin: return 20;
    case 0x4E: return kTrailingLabelRef; // one condition, or FF FD + several: the label always comes last
    }
    return std::nullopt;
}

std::optional<LabelRef> Script::labelRef(const Instruction& i) const {
    auto at = labelRefOffset(i.opcode);
    const auto a = args(i);
    if (at == kTrailingLabelRef) {
        // Single condition: u16 flag, u8 comparison, u8, u8 value, then the label.
        // Compound: s16 -n (FF FE = 2 ... FF FB = 5), then n 6-byte conditions (4E, u16 flag, u8, u8, u8)
        // joined by n-1 connector bytes, then the label.
        at = 5;
        if (a.size() > 2 && a[0] == 0xFF) {
            const std::size_t n = 0x10000 - be16(a, 0);
            at = 2 + n * 6 + (n - 1);
        }
    }
    if (!at || *at + 2 > a.size()) return std::nullopt;
    LabelRef r;
    r.file = a[*at];
    r.name = cstr(a, *at + 1, a.size() - *at - 1);
    return r;
}

std::optional<std::string> Script::resourceName(const Instruction& i) const {
    // u16 resource index at the start of the payload: pictures (40), movies (8E), BGM (88; 0x4000 = mode bit).
    const std::vector<std::string>* table = i.opcode == 0x40   ? &resources.pictures
                                            : i.opcode == 0x8E ? &resources.movies
                                            : i.opcode == 0x88 ? &resources.bgm
                                                               : nullptr;
    const auto a = args(i);
    if (!table || a.size() < 2) return std::nullopt;
    const std::size_t index = be16(a, 0) & (i.opcode == 0x40 ? 0xFFFF : 0x3FFF);
    if (index >= table->size()) return std::nullopt;
    return (*table)[index];
}

std::string Script::messageId(const Instruction& i) const {
    // 18 bytes of parameters, u16, u8 (1 or 2), then the NUL-terminated id ("__1_17").
    constexpr std::size_t kIdOffset = 21;
    const auto a = args(i);
    return a.size() <= kIdOffset ? std::string() : cstr(a, kIdOffset, a.size() - kIdOffset);
}

std::vector<Line> Script::lines() const {
    std::vector<Line> out;
    std::map<std::string, std::uint32_t> perLabel;
    for (std::size_t f = 0; f < files.size(); ++f) {
        const auto& code = files[f].code;
        std::string label = files[f].name + ":start";
        std::string message; // id of the open 7E block, if any
        std::uint32_t messageLine = 0;
        bool inBlock = false;
        Line cur;
        bool hasText = false;

        auto flush = [&](std::size_t lastIndex) {
            if (hasText) {
                if (!message.empty()) {
                    cur.id = messageLine == 0 ? message : message + "#" + std::to_string(messageLine);
                    ++messageLine;
                } else {
                    cur.id = label + "#" + std::to_string(perLabel[label]++);
                }
                cur.file = f;
                cur.last = lastIndex;
                out.push_back(std::move(cur));
            }
            cur = Line{};
            hasText = false;
        };

        // Label targets must stay on line boundaries, so that a line can be re-encoded as a whole.
        std::set<std::uint32_t> targets;
        for (const auto& l : files[f].labels) targets.insert(l.target);

        for (std::size_t i = 0; i < code.size(); ++i) {
            const auto& in = code[i];
            const auto a = args(in);
            if (!cur.text.empty() && targets.contains(in.offset)) flush(i);
            if (cur.text.empty()) cur.first = i;
            switch (in.opcode) {
            case op::Text:
                cur.text.append(reinterpret_cast<const char*>(a.data()), a.size());
                hasText = true;
                break;
            case op::RubyBegin: cur.text += "<ruby:"; break;
            case op::RubyEnd: cur.text += ">"; break;
            case op::Newline: cur.text += "\n"; break;
            case op::Wait:
            case op::Page: flush(i); break;
            case op::MessageBegin:
                flush(i);
                message = messageId(in);
                messageLine = 0;
                inBlock = true;
                break;
            case op::NarrationBegin:
                flush(i);
                inBlock = true;
                break;
            case op::MessageEnd:
            case op::NarrationEnd:
                flush(i);
                message.clear();
                inBlock = false;
                break;
            case op::Label:
                flush(i);
                label = labelName(in);
                break;
            default:
                // Name insertion and 3F always belong to the line; other codes are kept as tags only
                // inside a message/narration block, and end a free-standing narration line otherwise.
                if (in.opcode == op::Name || in.opcode == 0x3F || (inBlock && !cur.text.empty())) {
                    char tag[8];
                    std::snprintf(tag, sizeof tag, "<%02X", in.opcode);
                    cur.text += in.opcode == op::Name ? "<name" : tag;
                    if (!a.empty()) cur.text += ":" + hexOf(a);
                    cur.text += ">";
                } else if (!inBlock) {
                    flush(i); // ends a free-standing narration line (or drops a text-less fragment)
                }
                break;
            }
        }
        flush(code.size());
    }
    return out;
}

std::vector<std::string> Script::validate() const {
    std::vector<std::string> problems;
    std::set<std::string> globalLabels;
    for (const auto& f : files) {
        std::map<std::string, std::uint32_t> declared; // label -> offset right after its op::Label
        std::set<std::uint32_t> starts;
        for (const auto& in : f.code) {
            starts.insert(in.offset);
            if (in.opcode == op::Label) declared[labelName(in)] = in.offset + in.size;
        }
        for (const auto& l : f.labels) {
            const auto it = declared.find(l.name);
            if (it != declared.end() ? it->second != l.target : !starts.contains(l.target))
                problems.push_back(f.name + ": label " + l.name + " does not point at an instruction");
        }
        if (f.code.empty() || f.code.back().opcode != op::Return) problems.push_back(f.name + ": does not end with op 9C");
        for (const auto& in : f.code) {
            if (!labelRefOffset(in.opcode)) continue;
            const auto ref = labelRef(in);
            bool found = false;
            if (ref && ref->file < files.size())
                for (const auto& l : files[ref->file].labels)
                    if (l.name == ref->name) {
                        found = true;
                        break;
                    }
            if (!found)
                problems.push_back(f.name + ": " + std::string(mnemonic(in.opcode)) + " refers to a missing label " +
                                   (ref ? std::to_string(ref->file) + ":" + ref->name : std::string("?")));
        }
        for (const auto& [name, _] : declared)
            if (!globalLabels.insert(name).second) problems.push_back("label " + name + " declared in several files");
    }
    std::set<std::string> ids;
    for (const auto& l : lines()) {
        if (!ids.insert(l.id).second) problems.push_back("duplicate line id " + l.id);
        for (unsigned char c : l.id)
            if (c <= 0x20 || c >= 0x7F) {
                problems.push_back(files[l.file].name + ": line id with a non-printable character");
                break;
            }
    }
    for (const auto& f : files)
        for (const auto& in : f.code)
            if (in.opcode == op::MessageBegin && messageId(in).empty()) problems.push_back(f.name + ": message without id");
    return problems;
}

} // namespace imbk::sns
