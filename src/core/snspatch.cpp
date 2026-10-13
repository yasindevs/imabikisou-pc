#include "core/snspatch.hpp"

#include <unordered_map>

namespace imbk::sns {
namespace {

constexpr std::size_t kDirectory = 0x100;
constexpr std::string_view kPrologueLabel = "S00A_0000_PROLOGUE_0100";
// Header fields holding absolute offsets of the tables stored after the script files.
constexpr std::size_t kTailPointers[] = {0x20, 0x28, 0x2C, 0x60, 0x64, 0x68, 0x6C, 0x70, 0x74, 0x78, 0x7C};

int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

// "4A" or "4A:0102FF" -> opcode + payload. Returns false if `tag` is not of that form.
bool parseCodeTag(std::string_view tag, std::uint8_t& opcode, Bytes& payload) {
    const auto colon = tag.find(':');
    const auto opText = tag.substr(0, colon);
    if (opText.size() != 2 || hexDigit(opText[0]) < 0 || hexDigit(opText[1]) < 0) return false;
    opcode = static_cast<std::uint8_t>(hexDigit(opText[0]) * 16 + hexDigit(opText[1]));
    payload.clear();
    if (colon == std::string_view::npos) return true;
    const auto hex = tag.substr(colon + 1);
    if (hex.size() % 2) return false;
    for (std::size_t i = 0; i < hex.size(); i += 2) {
        const int hi = hexDigit(hex[i]), lo = hexDigit(hex[i + 1]);
        if (hi < 0 || lo < 0) return false;
        payload.push_back(static_cast<std::uint8_t>(hi * 16 + lo));
    }
    return true;
}

void put32(Bytes& b, std::size_t o, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) b[o + i] = static_cast<std::uint8_t>(v >> (8 * i));
}

} // namespace

Bytes encodeLine(std::string_view text) {
    Bytes out;
    std::string run;
    auto flushRun = [&] {
        if (run.empty()) return;
        out.push_back(op::Text);
        out.insert(out.end(), run.begin(), run.end());
        out.push_back(op::TextEnd);
        run.clear();
    };
    auto emit = [&](std::uint8_t opcode, const Bytes& payload) {
        if (payload.size() > 255) throw FormatError("instruction payload too long");
        flushRun();
        out.push_back(opcode);
        out.push_back(static_cast<std::uint8_t>(payload.size()));
        out.insert(out.end(), payload.begin(), payload.end());
    };

    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '\n') {
            emit(op::Newline, {});
            continue;
        }
        if (static_cast<std::uint8_t>(c) == op::TextEnd) throw FormatError("byte 0x02 is not allowed in text");
        if (c == '<') {
            const auto close = text.find('>', i);
            if (close != std::string_view::npos) {
                const auto tag = text.substr(i + 1, close - i - 1);
                std::uint8_t opcode = 0;
                Bytes payload;
                if (tag.starts_with("ruby:")) {
                    flushRun();
                    out.push_back(op::RubyBegin);
                    run = tag.substr(5);
                    flushRun();
                    out.push_back(op::RubyEnd);
                    i = close;
                    continue;
                }
                if (tag.starts_with("name:") && parseCodeTag(std::string("37:") + std::string(tag.substr(5)), opcode, payload)) {
                    emit(op::Name, payload);
                    i = close;
                    continue;
                }
                if (parseCodeTag(tag, opcode, payload)) {
                    if (opcode == op::Text || opcode == op::TextEnd || opcode == op::RubyBegin || opcode == op::RubyEnd)
                        throw FormatError("tag <" + std::string(tag) + "> cannot be used in text");
                    emit(opcode, payload);
                    i = close;
                    continue;
                }
            }
        }
        run += c; // includes a literal '<' that does not start a tag
    }
    flushRun();
    return out;
}

Bytes applyTranslations(const Script& script, const std::map<std::string, std::string>& translations,
                        const PatchOptions& options, PatchReport& report) {
    const auto lines = script.lines();
    std::unordered_map<std::string, const Line*> byId;
    for (const auto& l : lines) byId[l.id] = &l;

    // file -> first instruction index -> (end index, new bytes)
    std::vector<std::map<std::size_t, std::pair<std::size_t, Bytes>>> plan(script.files.size());
    for (const auto& [id, text] : translations) {
        const auto it = byId.find(id);
        if (it == byId.end()) {
            report.errors.push_back("unknown line id " + id);
            continue;
        }
        const Line& line = *it->second;
        if (text == line.text) {
            ++report.unchanged;
            continue;
        }
        try {
            plan[line.file][line.first] = {line.last, encodeLine(text)};
            ++report.replaced;
        } catch (const FormatError& e) {
            report.errors.push_back(id + ": " + e.what());
        }
    }
    if (!options.startLabel.empty()) {
        // The prologue check (S00A_0000_PROLOGUE_GOAROUND_CHK) prepares the scene state, then branches (op 49:
        // u8 flags, label name) either to S00A_0000_0000 (prologue skipped) or to the prologue. Only that
        // branch is retargeted, so the game still goes through its own set-up.
        bool known = false;
        std::uint8_t targetFile = 0; // label references are "[u8 script file index][name]"
        std::size_t retargeted = 0;
        for (std::size_t fi = 0; fi < script.files.size(); ++fi)
            for (const auto& in : script.files[fi].code)
                if (in.opcode == op::Label && script.labelName(in) == options.startLabel) {
                    known = true;
                    targetFile = static_cast<std::uint8_t>(fi);
                }
        if (!known) report.errors.push_back("--start: unknown label " + options.startLabel);
        if (options.startLabel.size() > 253) report.errors.push_back("--start: label too long");
        for (std::size_t fi = 0; fi < script.files.size() && known; ++fi) {
            const auto& f = script.files[fi];
            for (std::size_t i = 0; i < f.code.size(); ++i) {
                const auto& in = f.code[i];
                if (in.opcode != op::Branch || script.labelName(in) != kPrologueLabel) continue;
                Bytes branch{op::Branch, static_cast<std::uint8_t>(options.startLabel.size() + 2), targetFile};
                branch.insert(branch.end(), options.startLabel.begin(), options.startLabel.end());
                branch.push_back(0);
                plan[fi][i] = {i + 1, std::move(branch)};
                ++retargeted;
            }
        }
        if (known && retargeted == 0)
            report.errors.push_back(std::string("--start: no branch to ") + std::string(kPrologueLabel) + " found");
    }
    if (!report.errors.empty()) return {};

    const auto d = script.bytes();
    const auto& files = script.files;
    Bytes out(d.begin(), d.begin() + files.front().offset);
    for (std::size_t fi = 0; fi < files.size(); ++fi) {
        const auto& f = files[fi];
        const std::size_t newStart = out.size();
        out.insert(out.end(), d.begin() + f.offset, d.begin() + f.codeBase); // header, labels: unchanged layout
        const std::size_t newCode = out.size();

        std::unordered_map<std::uint32_t, std::uint32_t> remap; // old code offset -> new code offset
        for (std::size_t i = 0; i < f.code.size(); ++i) {
            const auto& in = f.code[i];
            remap[in.offset] = static_cast<std::uint32_t>(out.size() - newCode);
            if (const auto p = plan[fi].find(i); p != plan[fi].end()) {
                out.insert(out.end(), p->second.second.begin(), p->second.second.end());
                i = p->second.first - 1;
                continue;
            }
            const std::size_t start = f.codeBase + in.offset;
            out.insert(out.end(), d.begin() + start, d.begin() + start + in.size);
        }
        if (!f.code.empty()) remap[f.code.back().offset + f.code.back().size] = static_cast<std::uint32_t>(out.size() - newCode);
        while (out.size() % 4) out.push_back(0);

        for (const auto& l : f.labels) {
            const auto it = remap.find(l.target);
            if (it == remap.end()) {
                report.errors.push_back(f.name + ": label " + l.name + " points inside a replaced line");
                continue;
            }
            put32(out, newStart + (l.entry - f.offset) + 4, it->second);
        }
        put32(out, kDirectory + 4 + fi * 8, static_cast<std::uint32_t>(newStart));
        put32(out, kDirectory + 8 + fi * 8, static_cast<std::uint32_t>(out.size() - newStart));
    }

    // Resource tables after the last file move by the size difference.
    const std::size_t oldTail = files.back().offset + files.back().size;
    const std::size_t newTail = out.size();
    out.insert(out.end(), d.begin() + oldTail, d.end());
    for (const auto p : kTailPointers) {
        const auto v = le32(d, p);
        if (v < oldTail) throw FormatError("unexpected header pointer below the resource tables");
        put32(out, p, static_cast<std::uint32_t>(v - oldTail + newTail));
    }

    if (out.size() > kMaxScriptSize)
        report.errors.push_back("patched script is " + std::to_string(out.size()) + " bytes, the game's buffer holds " +
                                std::to_string(kMaxScriptSize));

    // End-to-end check: the result must parse, validate, and render the requested text for every line.
    const auto patched = Script::parse(out);
    for (const auto& p : patched.validate()) report.errors.push_back("patched script: " + p);
    std::unordered_map<std::string, std::string> rendered;
    for (const auto& l : patched.lines()) rendered[l.id] = l.text;
    for (const auto& [id, text] : translations) {
        const auto it = rendered.find(id);
        if (it == rendered.end()) report.errors.push_back(id + ": line missing after patching");
        else if (it->second != text) report.errors.push_back(id + ": renders as \"" + it->second + "\" after patching");
    }
    if (lines.size() != rendered.size()) report.errors.push_back("line count changed after patching");
    return out;
}

} // namespace imbk::sns
