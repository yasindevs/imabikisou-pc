// imbk: command-line tool for the user's own copy of Imabikisou (PS3).
// Reads the game directly from the .iso (or a folder with the disc contents) and never ships game data.

#include "core/afs.hpp"
#include "core/crc32.hpp"
#include "core/game.hpp"
#include "core/sfo.hpp"
#include "core/sjis.hpp"
#include "core/sn4.hpp"
#include "core/sns.hpp"
#include "core/snspatch.hpp"
#include "core/tsv.hpp"

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
constexpr std::string_view kResidentArchive = "residentchunk.afs";
constexpr std::string_view kMainScriptEntry = "st.sns";
constexpr std::uint32_t kMainScriptCrc = 0xC83EFAFA; // st.sns (encrypted) of BLJM60042 v01.00

void usage() {
    std::cout <<
        R"(imbk - Imabikisou PC port tools

Usage:
  imbk info    <game>                        Disc metadata and archive overview
  imbk verify  <game>                        Check that <game> is the supported release
  imbk ls      <game> [archive.afs]          List archives, or the entries of one archive
  imbk extract <game> <out-dir> [archive...] Extract archive entries (all archives by default)

Main script (residentchunk.afs/st.sns):
  imbk sns info    <game>                    Files, labels, resources and line counts
  imbk sns text    <game> [out.tsv]          Export lines as "<id>\t<text>"
  imbk sns layout  <game> [out.tsv]          Where each line starts: "<id>\tpage|line|cont" (for wrapping)
  imbk sns disasm  <game> [out.txt]          Disassemble all script files
  imbk sns decrypt <game> <out-file>         Write the decrypted container

Translation patch (test in RPCS3):
  imbk unpack <game> <disc-dir>              Copy the disc contents to a folder RPCS3 can boot
  imbk patch  <iso> <lines.tsv> <disc-dir> [--start <label>]
                                             Apply translated lines to the main script in <disc-dir>;
                                             --start makes New Game jump straight to <label> (testing)

Older secondary script (scriptchunk.afs/scenario.sn4):
  imbk sn4 text    <game> [out.tsv]
  imbk sn4 disasm  <game> [out.txt]

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

Bytes loadMainScriptRaw(const Game& game) {
    const auto afs = Afs::parse(game.chunk(kResidentArchive));
    const auto* e = afs.find(kMainScriptEntry);
    if (!e) throw std::runtime_error(std::string(kMainScriptEntry) + " not found in " + std::string(kResidentArchive));
    return afs.data(*e).readAll();
}

sns::Script loadMainScript(const Game& game) {
    auto data = loadMainScriptRaw(game);
    sns::decrypt(data);
    return sns::Script::parse(std::move(data));
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
    std::cout << "scenario.sn4  : " << script.code.size() << " instructions, " << script.labels.size() << " labels, "
              << script.lines().size() << " lines" << (problems.empty() ? "  OK" : "  INCONSISTENT") << "\n";
    for (const auto& p : problems) std::cout << "  - " << p << "\n";
    ok &= problems.empty();

    const auto sns = loadMainScriptRaw(game);
    const auto snsCrc = crc32(sns);
    const bool snsOk = sns::isSns(sns) && snsCrc == kMainScriptCrc;
    std::cout << "st.sns        : CSD-CSCNV " << (sns::isSns(sns) ? sns::version(sns) : "?") << ", CRC32 " << hex(snsCrc)
              << (snsOk ? "  OK" : "  UNEXPECTED") << "\n";
    ok &= snsOk;

    const auto main = loadMainScript(game);
    std::size_t instructions = 0;
    for (const auto& f : main.files) instructions += f.code.size();
    const auto mainProblems = main.validate();
    std::cout << "st.sns        : " << main.files.size() << " files, " << instructions << " instructions, "
              << main.lines().size() << " lines" << (mainProblems.empty() ? "  OK" : "  INCONSISTENT") << "\n";
    for (std::size_t i = 0; i < mainProblems.size() && i < 20; ++i) std::cout << "  - " << mainProblems[i] << "\n";
    ok &= mainProblems.empty();

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

int cmdSnsDecrypt(const Game& game, const fs::path& outPath) {
    auto data = loadMainScriptRaw(game);
    sns::decrypt(data);
    std::ofstream out(outPath, std::ios::binary);
    out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!out) throw std::runtime_error("cannot write " + outPath.string());
    std::cout << "st.sns (CSD-CSCNV " << sns::version(data) << ") decrypted -> " << outPath.string() << " ("
              << data.size() << " bytes)\n";
    return 0;
}

int cmdSnsInfo(const Game& game) {
    const auto s = loadMainScript(game);
    const auto lines = s.lines();
    std::vector<std::size_t> perFile(s.files.size());
    for (const auto& l : lines) ++perFile[l.file];
    std::cout << "st.sns  CSD-CSCNV " << s.version << ", start label " << s.startLabel << "\n\n";
    std::printf("  %-22s %8s %7s %7s\n", "file", "code", "labels", "lines");
    for (std::size_t i = 0; i < s.files.size(); ++i) {
        const auto& f = s.files[i];
        std::printf("  %-22s %8zu %7zu %7zu\n", f.name.c_str(), f.code.size(), f.labels.size(), perFile[i]);
    }
    const auto& r = s.resources;
    std::cout << "\nResources: " << r.pictures.size() << " pictures, " << r.movies.size() << " movies, " << r.bgm.size()
              << " bgm, " << r.sounds.size() << " sounds, " << r.aisac.size() << " AISAC controls\n"
              << "Lines: " << lines.size() << "\n";
    return 0;
}

int cmdSnsText(const Game& game, const char* outPath) {
    const auto s = loadMainScript(game);
    Output o(outPath);
    for (const auto& line : s.lines()) o.out() << line.id << '\t' << tsvEscape(line.text) << '\n';
    return 0;
}

// Layout facts for word wrapping: where each line starts on screen.
int cmdSnsLayout(const Game& game, const char* outPath) {
    const auto s = loadMainScript(game);
    Output o(outPath);
    for (const auto& line : s.lines()) {
        const char* b = line.brk == sns::Line::Break::Page ? "page" : line.brk == sns::Line::Break::Line ? "line" : "cont";
        o.out() << line.id << '\t' << b << '\n';
    }
    return 0;
}

int cmdSnsDisasm(const Game& game, const char* outPath) {
    const auto s = loadMainScript(game);
    Output o(outPath);
    auto& out = o.out();
    out << "; st.sns  CSD-CSCNV " << s.version << "  start label " << s.startLabel << "\n";
    for (const auto& f : s.files) {
        out << "\n;; ===== " << f.name << "  (code base 0x" << hex(f.codeBase, 0) << ", " << f.labels.size()
            << " labels)\n";
        for (const auto& in : f.code) {
            const auto a = s.args(in);
            if (in.opcode == sns::op::Label) out << "\n" << s.labelName(in) << ":\n";
            std::string m(sns::mnemonic(in.opcode));
            m.resize(std::max<std::size_t>(m.size(), 16), ' ');
            out << hex(in.offset, 6) << "  " << hex(in.opcode, 2) << " " << m;
            if (in.opcode == sns::op::Text) {
                out << " \"" << std::string_view(reinterpret_cast<const char*>(a.data()), a.size()) << "\"";
            } else if (!a.empty()) {
                out << " [" << a.size() << "] " << hexBytes(a);
                if (const auto ref = s.labelRef(in); ref && in.opcode != sns::op::Label) {
                    const auto target = ref->file < s.files.size() ? s.files[ref->file].name : "?";
                    out << "  -> " << target << ":" << ref->name;
                } else if (const auto res = s.resourceName(in)) {
                    out << "  -> " << *res;
                } else if (in.opcode != sns::op::Label) {
                    out << "  | " << printable(a);
                }
            }
            out << "\n";
        }
    }
    return 0;
}

int cmdUnpack(const Game& game, const fs::path& outDir) {
    const auto files = game.allFiles();
    std::uint64_t done = 0;
    for (std::size_t i = 0; i < files.size(); ++i) {
        const auto src = game.file(files[i]);
        const auto dst = outDir / fs::path(files[i]);
        fs::create_directories(dst.parent_path());
        const bool upToDate = fs::exists(dst) && fs::file_size(dst) == src.size();
        std::printf("[%3zu/%zu] %-60s %12s%s\n", i + 1, files.size(), files[i].c_str(), sizeStr(src.size()).c_str(),
                    upToDate ? "  (already there)" : "");
        std::fflush(stdout);
        if (!upToDate) src.copyTo(dst);
        done += src.size();
    }
    std::cout << "Unpacked " << files.size() << " files (" << sizeStr(done) << ") to " << outDir.string() << "\n";
    return 0;
}

int cmdPatch(const fs::path& gamePath, const fs::path& tsvPath, const fs::path& discDir, const sns::PatchOptions& options) {
    const auto target = discDir / "PS3_GAME/USRDIR/data/chunk" / fs::path(std::string(kResidentArchive));
    if (!fs::exists(target))
        throw std::runtime_error(target.string() + " not found: run `imbk unpack` first to create the disc folder");

    std::ifstream tsvIn(tsvPath, std::ios::binary);
    if (!tsvIn) throw std::runtime_error("cannot read " + tsvPath.string());
    const std::string tsv((std::istreambuf_iterator<char>(tsvIn)), std::istreambuf_iterator<char>());
    std::vector<std::string> errors;
    const auto translations = parseTsv(tsv, errors);

    Bytes archive;
    sns::PatchReport report;
    report.errors = errors;
    std::size_t scriptSize = 0;
    {
        // Everything is built in memory first, so <game> may be the same folder as <disc-dir>.
        const auto game = Game::open(gamePath);
        auto raw = loadMainScriptRaw(game);
        if (crc32(raw) != kMainScriptCrc)
            throw std::runtime_error("st.sns in " + gamePath.string() +
                                     " is not the original one (already patched?): pass your ISO as <game>");
        sns::decrypt(raw);
        const auto script = sns::Script::parse(std::move(raw));
        auto patched = report.errors.empty() ? sns::applyTranslations(script, translations, options, report) : Bytes{};
        std::cout << translations.size() << " lines in " << tsvPath.filename().string() << ": " << report.replaced
                  << " replaced, " << report.unchanged << " unchanged\n";
        if (!options.startLabel.empty()) std::cout << "New Game starts at " << options.startLabel << " (testing aid)\n";
        if (!report.errors.empty()) {
            std::cout << "Patch NOT applied, " << report.errors.size() << " problem(s):\n";
            for (std::size_t i = 0; i < report.errors.size() && i < 30; ++i) std::cout << "  - " << report.errors[i] << "\n";
            return 1;
        }
        scriptSize = patched.size();
        sns::encrypt(patched);
        archive = Afs::parse(game.chunk(kResidentArchive)).rebuild({{std::string(kMainScriptEntry), std::move(patched)}});
    }

    const auto tmp = fs::path(target).concat(".tmp");
    {
        std::ofstream out(tmp, std::ios::binary);
        out.write(reinterpret_cast<const char*>(archive.data()), static_cast<std::streamsize>(archive.size()));
        if (!out) throw std::runtime_error("cannot write " + tmp.string());
    }
    // Files copied from a mounted disc keep the read-only attribute.
    fs::permissions(target, fs::perms::owner_write, fs::perm_options::add);
    fs::remove(target);
    fs::rename(tmp, target);
    std::cout << "st.sns: " << scriptSize << " bytes (limit " << sns::kMaxScriptSize << ")\n"
              << "Wrote " << target.string() << "\n";
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
    if (cmd == "unpack" && argc >= 4) return cmdUnpack(gameArg(2), fs::path(argv[3]));
    if (cmd == "patch" && argc >= 5) {
        sns::PatchOptions options;
        for (int i = 5; i < argc; ++i) {
            if (std::string(argv[i]) == "--start" && i + 1 < argc) options.startLabel = argv[++i];
            else throw std::runtime_error(std::string("unknown patch option ") + argv[i]);
        }
        return cmdPatch(fs::path(argv[2]), fs::path(argv[3]), fs::path(argv[4]), options);
    }
    if ((cmd == "sn4" || cmd == "sns") && argc >= 4) {
        const std::string sub = argv[2];
        const char* out = argc > 4 ? argv[4] : nullptr;
        if (cmd == "sn4" && sub == "disasm") return cmdDisasm(gameArg(3), out);
        if (cmd == "sn4" && sub == "text") return cmdText(gameArg(3), out);
        if (cmd == "sns" && sub == "info") return cmdSnsInfo(gameArg(3));
        if (cmd == "sns" && sub == "text") return cmdSnsText(gameArg(3), out);
        if (cmd == "sns" && sub == "layout") return cmdSnsLayout(gameArg(3), out);
        if (cmd == "sns" && sub == "disasm") return cmdSnsDisasm(gameArg(3), out);
        if (cmd == "sns" && sub == "decrypt" && out) return cmdSnsDecrypt(gameArg(3), fs::path(out));
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
