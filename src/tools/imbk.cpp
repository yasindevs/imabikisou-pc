// imbk: command-line tool for the user's own copy of Imabikisou (PS3).
// Reads the game directly from the .iso (or a folder with the disc contents) and never ships game data.

#include "core/afs.hpp"
#include "core/crc32.hpp"
#include "core/game.hpp"
#include "core/sfo.hpp"
#include "core/sjis.hpp"
#include "core/sn4.hpp"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

using namespace imbk;
namespace fs = std::filesystem;

namespace {

constexpr std::string_view kTitleId = "BLJM60042";
constexpr std::string_view kScriptArchive = "scriptchunk.afs";
constexpr std::string_view kScriptEntry = "scenario.sn4";
constexpr std::uint32_t kScenarioCrc = 0x3E472406; // scenario.sn4 of BLJM60042 v01.00

void usage() {
    std::cout <<
        R"(imbk - Imabikisou PC port tools

Usage:
  imbk info    <game>                        Disc metadata and archive overview
  imbk verify  <game>                        Check that <game> is the supported release
  imbk ls      <game> [archive.afs]          List archives, or the entries of one archive
  imbk extract <game> <out-dir> [archive...] Extract archive entries (all archives by default)
  imbk script disasm <game> [out.txt]        Disassemble scenario.sn4
  imbk script text   <game> [out.tsv]        Export dialogue lines as "<id>\t<text>"

<game> is your Imabikisou .iso, or a folder containing PS3_GAME/ (mounted disc or extracted copy).
Output written to stdout or files is UTF-8.
)";
}

std::string hex(std::uint32_t v, int width = 8) {
    char b[16];
    std::snprintf(b, sizeof b, "%0*X", width, v);
    return b;
}

std::string sizeStr(std::uint64_t n) {
    char b[32];
    if (n >= 1u << 20) std::snprintf(b, sizeof b, "%.1f MiB", n / 1048576.0);
    else if (n >= 1u << 10) std::snprintf(b, sizeof b, "%.1f KiB", n / 1024.0);
    else std::snprintf(b, sizeof b, "%llu B", static_cast<unsigned long long>(n));
    return b;
}

sn4::Script loadScript(const Game& game) {
    const auto afs = Afs::parse(game.chunk(kScriptArchive));
    const auto* e = afs.find(kScriptEntry);
    if (!e) throw std::runtime_error(std::string(kScriptEntry) + " not found in " + std::string(kScriptArchive));
    return sn4::Script::parse(afs.data(*e).readAll());
}

// Writes to the given file, or stdout when no path is given.
class Output {
public:
    explicit Output(const char* path) {
        if (path) {
            file_.open(fs::path(path), std::ios::binary);
            if (!file_) throw std::runtime_error(std::string("cannot create ") + path);
        }
    }
    std::ostream& out() { return file_.is_open() ? file_ : std::cout; }

private:
    std::ofstream file_;
};

int cmdInfo(const Game& game) {
    const auto sfo = parseSfo(game.file("PS3_GAME/PARAM.SFO").readAll());
    auto get = [&](const char* k) { auto it = sfo.find(k); return it == sfo.end() ? std::string("?") : it->second; };
    std::cout << "Source    : " << game.describe() << "\n"
              << "Title     : " << get("TITLE") << "\n"
              << "Title ID  : " << get("TITLE_ID") << "\n"
              << "Version   : " << get("APP_VER") << " (disc " << get("VERSION") << ")\n\n";
    std::cout << "Archives in " << Game::kChunkDir << ":\n";
    for (const auto& name : game.chunkNames()) {
        const auto slice = game.chunk(name);
        const auto afs = Afs::parse(slice);
        std::printf("  %-30s %6zu entries  %12s\n", name.c_str(), afs.entries().size(), sizeStr(slice.size()).c_str());
    }
    return 0;
}

int cmdVerify(const Game& game) {
    bool ok = true;
    const auto sfo = parseSfo(game.file("PS3_GAME/PARAM.SFO").readAll());
    const auto id = sfo.contains("TITLE_ID") ? sfo.at("TITLE_ID") : "";
    std::cout << "Title ID      : " << id << (id == kTitleId ? "  OK" : "  UNEXPECTED (supported: BLJM60042)") << "\n";
    ok &= id == kTitleId;

    const auto afs = Afs::parse(game.chunk(kScriptArchive));
    const auto* e = afs.find(kScriptEntry);
    if (!e) throw std::runtime_error("scenario.sn4 missing");
    const auto crc = crc32(afs.data(*e).readAll());
    const bool crcOk = crc == kScenarioCrc;
    std::cout << "scenario.sn4  : CRC32 " << hex(crc) << (crcOk ? "  OK" : "  UNEXPECTED (different revision?)") << "\n";
    ok &= crcOk;

    const auto script = sn4::Script::parse(afs.data(*e).readAll());
    const auto problems = script.validate();
    std::cout << "Script        : " << script.code.size() << " instructions, " << script.labels.size() << " labels, "
              << script.lines().size() << " lines" << (problems.empty() ? "  OK" : "  INCONSISTENT") << "\n";
    for (const auto& p : problems) std::cout << "  - " << p << "\n";
    ok &= problems.empty();

    std::cout << (ok ? "\nThis copy is supported.\n" : "\nThis copy does not match the supported release.\n");
    return ok ? 0 : 1;
}

int cmdList(const Game& game, const char* archive) {
    if (!archive) {
        for (const auto& name : game.chunkNames()) std::cout << name << "\n";
        return 0;
    }
    const auto afs = Afs::parse(game.chunk(archive));
    for (const auto& e : afs.entries()) std::printf("%08X %10u  %s\n", e.offset, e.size, e.name.c_str());
    return 0;
}

int cmdExtract(const Game& game, const fs::path& outDir, std::vector<std::string> archives) {
    if (archives.empty()) archives = game.chunkNames();
    for (const auto& name : archives) {
        const auto afs = Afs::parse(game.chunk(name));
        const auto dir = outDir / fs::path(name).stem();
        fs::create_directories(dir);
        std::cout << name << " -> " << dir.string() << " (" << afs.entries().size() << " entries)" << std::endl;
        for (const auto& e : afs.entries()) {
            if (fs::path(e.name).has_parent_path() || e.name == "." || e.name == "..")
                throw FormatError("refusing suspicious entry name: " + e.name);
            afs.data(e).copyTo(dir / e.name);
        }
    }
    return 0;
}

std::string printable(ByteSpan a) {
    std::string s;
    for (auto c : a) s.push_back(c >= 0x20 && c < 0x7F ? static_cast<char>(c) : '.');
    return s;
}

std::string hexBytes(ByteSpan a) {
    std::string s;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (i) s.push_back(' ');
        s += hex(a[i], 2);
    }
    return s;
}

int cmdDisasm(const Game& game, const char* outPath) {
    const auto script = loadScript(game);
    Output o(outPath);
    auto& out = o.out();
    out << "; scenario.sn4  ScenarioLink " << script.version << "  code base 0x" << hex(script.codeBase, 0) << "\n";
    for (const auto& in : script.code) {
        const auto a = script.args(in);
        if (in.opcode == sn4::op::Label) out << "\n" << script.labelName(in) << ":\n";
        out << hex(in.offset, 6) << "  " << hex(in.opcode, 2) << " ";
        std::string m(sn4::mnemonic(in.opcode));
        m.resize(std::max<std::size_t>(m.size(), 11), ' ');
        out << m;
        switch (*sn4::operandOf(in.opcode)) {
        case sn4::Operand::None: break;
        case sn4::Operand::U8: out << " " << unsigned(a[0]); break;
        case sn4::Operand::U16: out << " " << be16(a, 0); break;
        case sn4::Operand::U32:
            if (in.opcode == 0x66) out << " " << be16(a, 0) << ", " << be16(a, 2);
            else out << " " << be32(a, 0);
            break;
        case sn4::Operand::Sized: out << " [" << a.size() << "] " << hexBytes(a) << "  | " << printable(a); break;
        case sn4::Operand::Text: out << " \"" << sjisToUtf8(a) << "\""; break;
        }
        out << "\n";
    }
    return 0;
}

int cmdText(const Game& game, const char* outPath) {
    const auto script = loadScript(game);
    Output o(outPath);
    for (const auto& line : script.lines()) o.out() << line.id << '\t' << sjisToUtf8(script.args(*line.instr)) << '\n';
    return 0;
}

int run(int argc, char** argv) {
    if (argc < 3) {
        usage();
        return argc == 1 ? 0 : 2;
    }
    const std::string cmd = argv[1];
    auto gameArg = [&](int i) { return Game::open(fs::path(argv[i])); };

    if (cmd == "info") return cmdInfo(gameArg(2));
    if (cmd == "verify") return cmdVerify(gameArg(2));
    if (cmd == "ls") return cmdList(gameArg(2), argc > 3 ? argv[3] : nullptr);
    if (cmd == "extract" && argc >= 4)
        return cmdExtract(gameArg(2), fs::path(argv[3]),
                          std::vector<std::string>(argv + 4, argv + argc));
    if (cmd == "script" && argc >= 4) {
        const std::string sub = argv[2];
        const char* out = argc > 4 ? argv[4] : nullptr;
        if (sub == "disasm") return cmdDisasm(gameArg(3), out);
        if (sub == "text") return cmdText(gameArg(3), out);
    }
    usage();
    return 2;
}

} // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    try {
        return run(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
