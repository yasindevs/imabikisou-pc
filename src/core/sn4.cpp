#include "core/sn4.hpp"

#include <array>
#include <cstdio>
#include <map>

namespace imbk::sn4 {
namespace {

constexpr std::size_t kHeaderBlockTable = 0xA0; // u32 count, then (u32 offset, u32 size) blocks

} // namespace

std::optional<Operand> operandOf(std::uint8_t op) {
    if (op == op::Text) return Operand::Text;
    if (op <= 0x1F) return Operand::None;
    if (op == 0x22) return Operand::None;
    if (op >= 0x2F && op <= 0x3F) return Operand::U8;
    if (op == 0x40) return Operand::U8;
    if (op == 0x4C) return Operand::U16;
    if (op >= 0x50 && op <= 0x5F) return Operand::U32;
    if (op == 0x66) return Operand::U32; // probably two u16: x, y
    if (op >= 0x60 && op <= 0x8F) return Operand::Sized;
    return std::nullopt;
}

std::string_view mnemonic(std::uint8_t op) {
    // Names are working hypotheses, see docs/sn4-format.md.
    switch (op) {
    case 0x02: return "text";
    case 0x04: return "newline";
    case 0x09: return "wait_click";
    case 0x0B: return "page";
    case 0x22: return "begin";
    case 0x50: return "delay";
    case 0x66: return "text_pos";
    case 0x6A: return "label_ref";
    case 0x6C: return "branch";
    case 0x70: return "choice";
    case 0x73: return "image";
    case 0x74: return "image_fx";
    case 0x85: return "label";
    }
    static const auto names = [] {
        std::array<std::array<char, 6>, 256> n{};
        for (int i = 0; i < 256; ++i) std::snprintf(n[i].data(), n[i].size(), "op_%02X", i);
        return n;
    }();
    return names[op].data();
}

Script Script::parse(Bytes data) {
    Script s;
    s.data_ = std::move(data);
    const ByteSpan d = s.data_;

    if (!startsWith(d, 0, "ScenarioLink")) throw FormatError("not a ScenarioLink script");
    s.version = cstr(d, 0x10, 16);
    if (s.version.starts_with(":Ver:")) s.version.erase(0, 5);
    if (le32(d, kHeaderBlockTable) != 1) throw FormatError("unexpected ScenarioLink block count");
    const std::size_t block = le32(d, kHeaderBlockTable + 4);
    const std::size_t blockSize = le32(d, kHeaderBlockTable + 8);
    require(d, block, blockSize, "SCR block");
    if (!startsWith(d, block, "SCR-file")) throw FormatError("missing SCR-file block");

    auto layout = scr::parseLayout(d, block);
    s.codeBase = static_cast<std::uint32_t>(layout.codeBase);
    s.labels = std::move(layout.labels);

    const std::size_t end = block + blockSize;
    for (std::size_t o = s.codeBase; o < end;) {
        Instruction in;
        in.offset = static_cast<std::uint32_t>(o - s.codeBase);
        in.opcode = d[o];
        const auto kind = operandOf(in.opcode);
        char where[64];
        std::snprintf(where, sizeof where, " at code offset 0x%X", in.offset);
        if (!kind) {
            char msg[32];
            std::snprintf(msg, sizeof msg, "unknown opcode 0x%02X", in.opcode);
            throw FormatError(msg + std::string(where));
        }
        in.argOffset = static_cast<std::uint32_t>(o + 1);
        switch (*kind) {
        case Operand::None: in.argSize = 0; in.size = 1; break;
        case Operand::U8: in.argSize = 1; in.size = 2; break;
        case Operand::U16: in.argSize = 2; in.size = 3; break;
        case Operand::U32: in.argSize = 4; in.size = 5; break;
        case Operand::Sized:
            require(d, o + 1, 1, "operand length");
            in.argOffset = static_cast<std::uint32_t>(o + 2);
            in.argSize = d[o + 1];
            in.size = 2 + in.argSize;
            break;
        case Operand::Text: {
            std::size_t t = o + 1;
            while (t < end && d[t] != op::TextEnd) ++t;
            if (t >= end) throw FormatError(std::string("unterminated text") + where);
            in.argSize = static_cast<std::uint32_t>(t - (o + 1));
            in.size = in.argSize + 2;
            break;
        }
        }
        if (o + in.size > end) throw FormatError(std::string("instruction runs past end of script") + where);
        s.code.push_back(in);
        o += in.size;
    }
    return s;
}

std::string Script::labelName(const Instruction& i) const {
    // Label payloads are: u8 flags (always 0 so far), NUL-terminated name.
    const auto a = args(i);
    if (a.size() < 2) return {};
    return cstr(a, 1, a.size() - 1);
}

std::vector<Line> Script::lines() const {
    std::vector<Line> out;
    std::string label = "_start";
    std::uint32_t index = 0;
    for (const auto& in : code) {
        if (in.opcode == op::Label) {
            label = labelName(in);
            index = 0;
        } else if (in.opcode == op::Text) {
            Line l;
            l.label = label;
            l.index = index++;
            l.id = label + "#" + std::to_string(l.index);
            l.instr = &in;
            out.push_back(std::move(l));
        }
    }
    return out;
}

std::vector<std::string> Script::validate() const {
    std::vector<std::string> problems;
    std::map<std::string, std::uint32_t> declared; // label name -> offset right after its 0x85 instruction
    for (const auto& in : code)
        if (in.opcode == op::Label) declared[labelName(in)] = in.offset + in.size;

    std::size_t scene = 0;
    for (const auto& l : labels) {
        if (l.tag != kSceneLabelTag) continue;
        ++scene;
        const auto it = declared.find(l.name);
        if (it == declared.end())
            problems.push_back("label " + l.name + " is not declared in the code");
        else if (it->second != l.target)
            problems.push_back("label " + l.name + " target mismatch");
    }
    if (scene != declared.size())
        problems.push_back(std::to_string(declared.size()) + " label instructions but " + std::to_string(scene) +
                           " scene labels in the table");
    return problems;
}

} // namespace imbk::sn4
