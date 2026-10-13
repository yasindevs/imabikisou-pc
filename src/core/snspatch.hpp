#pragma once

#include "core/sns.hpp"

#include <map>

namespace imbk::sns {

// The game's loader (EBOOT 0x00090418) reads st.sns into a buffer of this size.
constexpr std::size_t kMaxScriptSize = 0x2EE000;

// Encodes a line as rendered by Script::lines() back to bytecode:
// "\n" -> newline, <ruby:reading>, <name:XX>, <XX> / <XX:hex> -> that instruction, anything else -> text.
Bytes encodeLine(std::string_view text);

struct PatchOptions {
    // Testing aid: label that a new game goes to instead of the prologue (the prologue check's branch to the
    // prologue is retargeted; S00A_0000_0000 is where the game itself goes when the prologue is skipped).
    std::string startLabel;
};

struct PatchReport {
    std::size_t replaced = 0;
    std::size_t unchanged = 0;
    std::vector<std::string> errors; // non-empty = the patch must not be used
};

// Returns the decrypted container with lines replaced (id -> text). Label targets, file sizes, the file
// directory and the offsets of the resource tables are updated; the result is re-parsed and checked.
Bytes applyTranslations(const Script& script, const std::map<std::string, std::string>& translations,
                        const PatchOptions& options, PatchReport& report);

} // namespace imbk::sns
