#pragma once

#include "core/binary.hpp"

#include <optional>
#include <string_view>

namespace imbk::sn4 {

// Bytecode of "ScenarioLink 3.25" scripts (scenario.sn4). See docs/sn4-format.md.

enum class Operand {
    None,  // opcode only
    U8,    // 1 byte
    U16,   // 2 bytes, big-endian
    U32,   // 4 bytes, big-endian
    Sized, // u8 length, then that many bytes
    Text,  // Shift-JIS bytes up to and including a 0x03 terminator
};

std::optional<Operand> operandOf(std::uint8_t op);
std::string_view mnemonic(std::uint8_t op); // "op_XX" when the meaning is still unknown

namespace op {
constexpr std::uint8_t Text = 0x02;
constexpr std::uint8_t Label = 0x85;
constexpr std::uint8_t TextEnd = 0x03;
} // namespace op

constexpr std::uint16_t kSceneLabelTag = 0x6F4E; // "No"
constexpr std::uint16_t kLocalLabelTag = 0xFFFF; // "_AMIn" labels used by choices

struct Label {
    std::string name;
    std::uint16_t tag = 0;
    std::uint32_t target = 0; // offset in code, relative to Script::codeBase
};

struct Instruction {
    std::uint32_t offset = 0; // relative to Script::codeBase
    std::uint8_t opcode = 0;
    std::uint32_t argOffset = 0; // absolute offset of the operand bytes in Script::data
    std::uint32_t argSize = 0;   // Sized: payload only; Text: without the terminator
    std::uint32_t size = 0;      // whole instruction, opcode included
};

// One line of dialogue/narration with a stable ID usable by translation files.
struct Line {
    std::string id; // "<scene label>#<index within the label>"
    std::string label;
    std::uint32_t index = 0;
    const Instruction* instr = nullptr;
};

class Script {
public:
    static Script parse(Bytes data);

    std::string version;
    std::uint32_t codeBase = 0; // absolute offset of the bytecode
    std::vector<Label> labels;
    std::vector<Instruction> code;

    ByteSpan bytes() const { return data_; }
    ByteSpan args(const Instruction& i) const { return ByteSpan(data_).subspan(i.argOffset, i.argSize); }
    std::string labelName(const Instruction& i) const; // for Label (0x85) and other label references

    std::vector<Line> lines() const;

    // Cross-checks the label table against the bytecode; returns human-readable problems (empty = OK).
    std::vector<std::string> validate() const;

private:
    Bytes data_;
};

} // namespace imbk::sn4
