#pragma once

#include "core/scr.hpp"

#include <string_view>

namespace imbk::sns {

// Main scenario script (residentchunk.afs/st.sns), "CSD-CSCNV" format. See docs/sns-format.md.
//
// The first 16 bytes are both the magic ("CSD-CSCNV:01.68\0") and the key: every following byte is
// stored as plain[i] + key[i % 16] (mod 256). Recovered from the game's loader (EBOOT 0x00090418).

constexpr std::size_t kKeySize = 16;
constexpr std::string_view kMagic = "CSD-CSCNV:";

bool isSns(ByteSpan data);
std::string version(ByteSpan data); // e.g. "01.68"

void decrypt(Bytes& data);
void encrypt(Bytes& data);

// --- Decrypted container -----------------------------------------------------------------------------

namespace op {
constexpr std::uint8_t Text = 0x01; // UTF-8 run terminated by TextEnd
constexpr std::uint8_t TextEnd = 0x02;
constexpr std::uint8_t Wait = 0x17;
constexpr std::uint8_t RubyBegin = 0x18; // reading runs follow, then RubyEnd, then the base text run
constexpr std::uint8_t RubyEnd = 0x19;
constexpr std::uint8_t Newline = 0x1A;
constexpr std::uint8_t Page = 0x1B;
constexpr std::uint8_t NarrationBegin = 0x1F;
constexpr std::uint8_t NarrationEnd = 0x21;
constexpr std::uint8_t Name = 0x37; // u8 character index: inserts that character's name
constexpr std::uint8_t Branch = 0x49; // u8 flags, label name (conditional jump; e.g. the two exits of the prologue check)
constexpr std::uint8_t Jump = 0x4A;  // u8 flags, label name (517 uses; e.g. the boot script's jump to the prologue)
constexpr std::uint8_t Label = 0x4D;
constexpr std::uint8_t MessageBegin = 0x7E; // payload ends with the message id, e.g. "__1_17"
constexpr std::uint8_t MessageEnd = 0x7F;
constexpr std::uint8_t Return = 0x9C; // last instruction of every script file (followed by zero padding)
} // namespace op

std::string_view mnemonic(std::uint8_t op); // "op_XX" when the meaning is still unknown

struct Instruction {
    std::uint32_t offset = 0;    // relative to the file's code base
    std::uint8_t opcode = 0;
    std::uint32_t argOffset = 0; // absolute offset in the container
    std::uint32_t argSize = 0;   // Text: without the terminator
    std::uint32_t size = 0;      // whole instruction
};

struct File {
    std::string name; // original source name, e.g. "s00a_day1.xml"
    std::uint32_t offset = 0, size = 0, codeBase = 0;
    std::vector<scr::Label> labels;
    std::vector<Instruction> code;
};

// Resource name tables; the bytecode refers to resources by index.
struct Resources {
    std::vector<std::string> pictures, movies, bgm, sounds, aisac;
};

// One translatable line: text up to a wait/page/end-of-block, with inline codes rendered as tags.
// Every instruction in [first, last) is represented in `text`, so the range can be re-encoded from it.
struct Line {
    std::string id;   // message id ("__1_17", "#n" suffix after the first line) or "<label>#<n>"
    std::size_t file = 0;
    std::size_t first = 0, last = 0; // instruction index range in File::code, `last` excluded
    std::string text; // UTF-8; "\n" for Newline, <ruby:reading>, <name:XX>, <XX:hex> for other inline codes
};

class Script {
public:
    static Script parse(Bytes decrypted);

    std::string version;
    std::string startLabel;
    std::vector<File> files;
    Resources resources;

    ByteSpan bytes() const { return data_; }
    ByteSpan args(const Instruction& i) const { return ByteSpan(data_).subspan(i.argOffset, i.argSize); }
    std::string labelName(const Instruction& i) const; // for op::Label
    std::string messageId(const Instruction& i) const; // for op::MessageBegin

    std::vector<Line> lines() const;
    std::vector<std::string> validate() const;

private:
    Bytes data_;
};

} // namespace imbk::sns
