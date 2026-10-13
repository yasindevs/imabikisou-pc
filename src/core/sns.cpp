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
    // Names are working hypotheses, see docs/sns-format.md.
    switch (op) {
    case op::Text: return "text";
    case op::Wait: return "wait";
    case op::RubyBegin: return "ruby";
    case op::RubyEnd: return "ruby_end";
    case op::Newline: return "newline";
    case op::Page: return "page";
    case op::NarrationBegin: return "narration";
    case op::NarrationEnd: return "narration_end";
    case op::Name: return "name";
    case 0x29: return "delay";
    case op::Label: return "label";
    case op::MessageBegin: return "message";
    case op::MessageEnd: return "message_end";
    case op::Return: return "return";
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
    const auto a = args(i); // u8 flags, NUL-terminated name
    return a.size() < 2 ? std::string() : cstr(a, 1, a.size() - 1);
}

std::string Script::messageId(const Instruction& i) const {
    const auto a = args(i); // parameters, then 0x01 and the NUL-terminated id
    for (std::size_t k = a.size(); k-- > 0;)
        if (a[k] == 0x01) return cstr(a, k + 1, a.size() - k - 1);
    return {};
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

        for (std::size_t i = 0; i < code.size(); ++i) {
            const auto& in = code[i];
            const auto a = args(in);
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
                } else if (hasText) {
                    flush(i);
                }
                break;
            }
        }
        flush(code.empty() ? 0 : code.size() - 1);
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
        for (const auto& [name, _] : declared)
            if (!globalLabels.insert(name).second) problems.push_back("label " + name + " declared in several files");
    }
    std::set<std::string> ids;
    for (const auto& l : lines())
        if (!ids.insert(l.id).second) problems.push_back("duplicate line id " + l.id);
    return problems;
}

} // namespace imbk::sns
