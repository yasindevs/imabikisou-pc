#define _CRT_SECURE_NO_WARNINGS
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include "installer.hpp"

#include <windows.h>
#include <bcrypt.h>
#include <shellapi.h>
#include <winioctl.h>

#include <cstring>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <thread>
#include <vector>

namespace launcher {

// ---------------------------------------------------------------- text ------

std::string toUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring toWide(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

static std::string u8(const fs::path& p) { return toUtf8(p.wstring()); }
static fs::path fromU8(const std::string& s) { return fs::path(toWide(s)); }

// ---------------------------------------------------------------- config ----

Config Config::load(const fs::path& file) {
    Config c;
    std::ifstream in(file, std::ios::binary);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("\xEF\xBB\xBF", 0) == 0) line.erase(0, 3);  // Notepad's UTF-8 BOM
        const auto eq = line.find('=');
        if (eq == std::string::npos || line[0] == '#') continue;
        const auto k = line.substr(0, eq), v = line.substr(eq + 1);
        if (k == "iso") c.iso = fromU8(v);
        else if (k == "rpcs3") c.rpcs3 = fromU8(v);
        else if (k == "eboot") c.eboot = fromU8(v);
        else if (k == "data_dir") c.dataDir = fromU8(v);
        else if (k == "language" && (v == "ja" || v == "en")) c.language = v;
        else if (k == "vsync") c.vsync = v == "1";
        else if (k == "mute") c.mute = v == "1";
    }
    return c;
}

void Config::save(const fs::path& file) const {
    std::ofstream out(file, std::ios::binary);
    out << "# Imabikiso PC launcher settings\n"
        << "iso=" << u8(iso) << "\n"
        << "rpcs3=" << u8(rpcs3) << "\n"
        << "eboot=" << u8(eboot) << "\n"
        << "data_dir=" << u8(dataDir) << "\n"
        << "language=" << language << "\n"
        << "vsync=" << (vsync ? 1 : 0) << "\n"
        << "mute=" << (mute ? 1 : 0) << "\n";
}

Paths Paths::fromExe() {
    wchar_t buf[MAX_PATH * 4];
    const DWORD n = GetModuleFileNameW(nullptr, buf, (DWORD)std::size(buf));
    Paths p;
    p.base = fs::path(std::wstring(buf, n)).parent_path();
    p.imbk = p.base / "imbk.exe";
    p.game = p.base / "game" / "ImabikisoPC.exe";
    p.langDir = p.base / "lang";
    p.ini = p.base / "launcher.ini";
    p.profiles = p.base / "profiles";
    p.logs = p.base / "logs";
    return p;
}

// ---------------------------------------------------------------- state -----

void State::setStep(const std::string& s, float p) {
    std::lock_guard<std::mutex> lk(mu);
    step = s;
    progress = p;
    log.push_back("== " + s);
    while (log.size() > 400) { log.pop_front(); ++dropped; }
}

void State::setProgress(float p) {
    std::lock_guard<std::mutex> lk(mu);
    progress = p;
}

void State::addLog(const std::string& line) {
    std::lock_guard<std::mutex> lk(mu);
    log.push_back(line);
    while (log.size() > 400) { log.pop_front(); ++dropped; }
}

// ---------------------------------------------------------------- helpers ---

static std::string sha256File(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return {};
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE h = nullptr;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) return {};
    std::string hex;
    if (BCryptCreateHash(alg, &h, nullptr, 0, nullptr, 0, 0) == 0) {
        std::vector<char> buf(1 << 20);
        while (in) {
            in.read(buf.data(), (std::streamsize)buf.size());
            if (in.gcount() > 0) BCryptHashData(h, (PUCHAR)buf.data(), (ULONG)in.gcount(), 0);
        }
        unsigned char d[32];
        if (BCryptFinishHash(h, d, sizeof d, 0) == 0) {
            char t[3];
            for (unsigned char b : d) { std::snprintf(t, sizeof t, "%02x", b); hex += t; }
        }
        BCryptDestroyHash(h);
    }
    BCryptCloseAlgorithmProvider(alg, 0);
    return hex;
}

static std::wstring quote(const fs::path& p) { return L"\"" + p.wstring() + L"\""; }

constexpr int kCancelled = -1, kTimedOut = -2, kNotStarted = -3;

// Runs a console program, streaming its output lines to `onLine`. The process
// and anything it starts live in a job object, so cancel (or a timeout, in
// ms, 0 = none) ends all of them. Returns the exit code or one of the k* above.
template <class F>
static int runProcess(const std::wstring& cmdline, const fs::path& cwd, State& s, F onLine, DWORD timeoutMs = 0) {
    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return kNotStarted;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION lim{};
    lim.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (job) SetInformationJobObject(job, JobObjectExtendedLimitInformation, &lim, sizeof lim);
    STARTUPINFOW si{};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = wr;
    si.hStdError = wr;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi{};
    std::wstring cmd = cmdline;
    const BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED,
                                   nullptr, cwd.empty() ? nullptr : cwd.c_str(), &si, &pi);
    CloseHandle(wr);
    if (!ok) {
        CloseHandle(rd);
        if (job) CloseHandle(job);
        s.addLog("cannot start: " + toUtf8(cmdline));
        return kNotStarted;
    }
    if (job) AssignProcessToJobObject(job, pi.hProcess);
    ResumeThread(pi.hThread);
    std::string pending;
    char buf[4096];
    DWORD got = 0;
    int result = 0;
    const ULONGLONG start = GetTickCount64();
    bool pipeOpen = true;
    for (;;) {
        if (s.cancel) { result = kCancelled; break; }
        if (timeoutMs && GetTickCount64() - start > timeoutMs) { result = kTimedOut; break; }
        DWORD avail = 0;
        if (pipeOpen && !PeekNamedPipe(rd, nullptr, 0, nullptr, &avail, nullptr)) pipeOpen = false;
        if (avail) {
            if (!ReadFile(rd, buf, std::min<DWORD>(avail, sizeof buf), &got, nullptr) || got == 0) { pipeOpen = false; continue; }
            pending.append(buf, got);
            size_t nl;
            while ((nl = pending.find_first_of("\r\n")) != std::string::npos) {
                std::string line = pending.substr(0, nl);
                pending.erase(0, nl + 1);
                if (!line.empty()) onLine(line);
            }
            continue;
        }
        // Nothing to read: done once the process has exited and the pipe is drained.
        if (WaitForSingleObject(pi.hProcess, 50) == WAIT_OBJECT_0) {
            if (!pipeOpen || !PeekNamedPipe(rd, nullptr, 0, nullptr, &avail, nullptr) || avail == 0) break;
        }
    }
    if (!pending.empty()) onLine(pending);
    if (result != 0) {
        if (job) TerminateJobObject(job, 1);
        else TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 5000);
    } else {
        DWORD code = 1;
        GetExitCodeProcess(pi.hProcess, &code);
        result = (int)code;
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(rd);
    if (job) CloseHandle(job);
    return result;
}

static void checkCancel(State& s) {
    if (s.cancel) throw std::runtime_error("Cancelled.");
}

static void writeMarker(const fs::path& file, const std::string& text);

static void fail(const std::string& msg) { throw std::runtime_error(msg); }

// ---------------------------------------------------------------- status ----

static bool discPresent(const fs::path& disc) {
    std::error_code ec;
    return fs::exists(disc / "PS3_GAME" / "USRDIR" / "data" / "chunk" / "residentchunk.afs", ec) &&
           fs::exists(disc / "PS3_GAME" / "PARAM.SFO", ec) && fs::exists(disc / ".imbk-complete", ec);
}

Status queryStatus(const Paths& p, const Config& c) {
    Status st;
    std::error_code ec;
    st.gameExe = fs::exists(p.game, ec);
    if (c.dataDir.empty()) return st;
    st.discReady = discPresent(p.disc(c));
    st.elfReady = fs::exists(p.elf(c), ec) && fs::exists(c.dataDir / ".elf-verified", ec);
    st.langReady = c.language == "ja" ||
                   (fs::exists(p.langElf(c, c.language), ec) &&
                    fs::exists(c.dataDir / "lang" / c.language / ".prepared", ec));
    return st;
}

// ---------------------------------------------------------------- steps -----

static void stepVerify(const Paths& p, const Config& c, State& s) {
    s.setStep("Checking your disc image", -1.f);
    const int rc = runProcess(quote(p.imbk) + L" verify " + quote(c.iso), {}, s,
                              [&](const std::string& l) { s.addLog(l); });
    checkCancel(s);
    if (rc != 0) fail("This disc image is not the supported release (Imabikiso, BLJM60042).");
}

static void stepUnpack(const Paths& p, const Config& c, State& s) {
    const fs::path disc = p.disc(c);
    s.setStep("Extracting the game files (about 9 GB)", 0.f);
    fs::create_directories(disc);
    // imbk prints "[  12/345] name   size"; files already complete are skipped.
    const int rc = runProcess(quote(p.imbk) + L" unpack " + quote(c.iso) + L" " + quote(disc), {}, s,
                              [&](const std::string& l) {
                                  unsigned i = 0, n = 0;
                                  if (std::sscanf(l.c_str(), "[%u/%u]", &i, &n) == 2 && n) {
                                      s.setProgress((float)i / (float)n);
                                      if (i == n || i % 25 == 0) s.addLog(l);
                                  } else {
                                      s.addLog(l);
                                  }
                              });
    checkCancel(s);
    if (rc != 0) fail("Extraction failed (see the log). Free disk space? It resumes where it stopped.");
    writeMarker(disc / ".imbk-complete", "ok");
}

static void stepElf(const Paths& p, const Config& c, State& s) {
    const fs::path out = p.elf(c);
    s.setStep("Preparing the game executable", -1.f);
    fs::path elf;
    if (!c.eboot.empty()) {
        elf = c.eboot;
    } else {
        if (c.rpcs3.empty() || !fs::exists(c.rpcs3)) fail("Choose rpcs3.exe (used once to decrypt EBOOT.BIN), or a decrypted EBOOT.ELF.");
        const fs::path tmp = c.dataDir / "tmp";
        fs::create_directories(tmp);
        const fs::path bin = tmp / "EBOOT.BIN";
        fs::copy_file(p.disc(c) / "PS3_GAME" / "USRDIR" / "EBOOT.BIN", bin, fs::copy_options::overwrite_existing);
        fs::remove(tmp / "EBOOT.elf");
        s.addLog("Decrypting EBOOT.BIN with RPCS3...");
        const int rc = runProcess(quote(c.rpcs3) + L" --decrypt " + quote(bin), tmp, s,
                                  [&](const std::string& l) { s.addLog(l); }, 5 * 60 * 1000);
        checkCancel(s);
        if (rc == kTimedOut) fail("RPCS3 did not finish decrypting within 5 minutes. Use a decrypted EBOOT.ELF instead.");
        elf = tmp / "EBOOT.elf";
        if (!fs::exists(elf))
            fail("RPCS3 did not produce EBOOT.elf (exit code " + std::to_string(rc) +
                 "). It needs a recent RPCS3 with --decrypt, or give a decrypted EBOOT.ELF.");
    }
    const auto hash = sha256File(elf);
    s.addLog("EBOOT.ELF sha256 " + hash);
    if (hash != kElfSha256) fail("This EBOOT.ELF is not the supported one (BLJM60042 v01.00).");
    if (elf != out) fs::copy_file(elf, out, fs::copy_options::overwrite_existing);
    writeMarker(c.dataDir / ".elf-verified", hash);
    std::error_code ec;
    fs::remove_all(c.dataDir / "tmp", ec);
}

static void writeMarker(const fs::path& file, const std::string& text) {
    std::ofstream out(file, std::ios::binary);
    out << text << "\n";
    out.close();
    if (!out) fail("Cannot write " + u8(file));
}

// A directory junction (no admin rights needed, unlike symbolic links).
static bool createJunction(const fs::path& link, const fs::path& target) {
    if (!CreateDirectoryW(link.c_str(), nullptr)) return false;
    HANDLE h = CreateFileW(link.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                           FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h == INVALID_HANDLE_VALUE) { RemoveDirectoryW(link.c_str()); return false; }
    const std::wstring print = fs::absolute(target).wstring();
    const std::wstring sub = L"\\??\\" + print;
    const WORD subBytes = (WORD)(sub.size() * 2), printBytes = (WORD)(print.size() * 2);
    const WORD dataLen = (WORD)(8 + subBytes + 2 + printBytes + 2);
    std::vector<BYTE> buf(8 + dataLen, 0);
    auto put16 = [&](size_t at, WORD v) { std::memcpy(&buf[at], &v, 2); };
    const DWORD tag = IO_REPARSE_TAG_MOUNT_POINT;
    std::memcpy(&buf[0], &tag, 4);
    put16(4, dataLen);
    put16(8, 0);                      // substitute name offset
    put16(10, subBytes);
    put16(12, (WORD)(subBytes + 2));  // print name offset
    put16(14, printBytes);
    std::memcpy(&buf[16], sub.c_str(), subBytes);
    std::memcpy(&buf[16 + subBytes + 2], print.c_str(), printBytes);
    DWORD ret = 0;
    const BOOL ok = DeviceIoControl(h, FSCTL_SET_REPARSE_POINT, buf.data(), (DWORD)buf.size(), nullptr, 0, &ret, nullptr);
    CloseHandle(h);
    if (!ok) RemoveDirectoryW(link.c_str());
    return ok != 0;
}

// A per-language disc view: every directory is a junction to the extracted
// disc except those on the way to the one patched archive, whose files are
// hard links. Nothing of the 9 GB is copied when the data folder is on NTFS.
static void mirror(const fs::path& src, const fs::path& dst, const fs::path& keepRel, const fs::path& rel, State& s) {
    fs::create_directories(dst);
    for (const auto& e : fs::directory_iterator(src)) {
        const fs::path r = rel / e.path().filename();
        const fs::path t = dst / e.path().filename();
        checkCancel(s);
        bool onPath = false;
        for (fs::path q = keepRel.parent_path(); !q.empty() && q != q.root_path(); q = q.parent_path())
            if (q == r) onPath = true;
        if (e.is_directory() && onPath) {
            mirror(e.path(), t, keepRel, r, s);  // a real folder: also completes an interrupted run
            continue;
        }
        std::error_code ec;
        if (fs::exists(t, ec) || fs::is_symlink(t, ec)) continue;
        if (e.is_directory()) {
            if (!createJunction(t, e.path())) fail("Cannot create a folder junction in " + u8(dst) + " (NTFS needed).");
        } else if (!CreateHardLinkW(t.c_str(), e.path().c_str(), nullptr)) {
            fs::copy_file(e.path(), t);  // e.g. not NTFS: costs the file's size
        }
    }
}

static void applyMenu(const fs::path& tsv, const fs::path& elfIn, const fs::path& elfOut) {
    std::ifstream in(elfIn, std::ios::binary);
    std::vector<char> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::ifstream m(tsv, std::ios::binary);
    if (!m) fail("Missing menu translation " + u8(tsv));
    std::string line;
    while (std::getline(m, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ls(line);
        std::string a, b, text;
        std::getline(ls, a, '\t');
        std::getline(ls, b, '\t');
        std::getline(ls, text);
        if (a == "elf_sha256") {
            if (b != kElfSha256) fail("menu.tsv targets another EBOOT.ELF");
            continue;
        }
        const size_t off = std::stoul(a, nullptr, 16), size = std::stoul(b);
        if (text.size() > size || off + size > data.size()) fail("menu.tsv entry does not fit: " + line);
        std::fill(data.begin() + off, data.begin() + off + size, '\0');
        std::copy(text.begin(), text.end(), data.begin() + off);
    }
    const fs::path tmp = fs::path(elfOut).concat(L".tmp");
    {
        std::ofstream out(tmp, std::ios::binary);
        out.write(data.data(), (std::streamsize)data.size());
        out.close();
        if (!out) fail("Cannot write " + u8(tmp));
    }
    fs::rename(tmp, elfOut);
}

static void stepLanguage(const Paths& p, const Config& c, State& s) {
    const std::string l = c.language;
    if (l == "ja") return;
    const fs::path dir = c.dataDir / "lang" / l;
    const fs::path story = p.langDir / l / "story.tsv";
    if (!fs::exists(story)) fail("Missing translation file " + u8(story));
    s.setStep("Preparing the " + std::string(l == "en" ? "English" : l) + " version", -1.f);
    std::error_code ec;
    fs::remove(dir / ".prepared", ec);
    const fs::path keep = fs::path("PS3_GAME") / "USRDIR" / "data" / "chunk" / "residentchunk.afs";
    mirror(p.disc(c), p.langDisc(c, l), keep, {}, s);
    checkCancel(s);
    const int rc = runProcess(quote(p.imbk) + L" patch " + quote(c.iso) + L" " + quote(story) + L" " + quote(p.langDisc(c, l)),
                              {}, s, [&](const std::string& x) { s.addLog(x); });
    checkCancel(s);
    if (rc != 0) fail("Applying the translation failed (see the log).");
    applyMenu(p.langDir / l / "menu.tsv", p.elf(c), p.langElf(c, l));
    writeMarker(dir / ".prepared", "ok");
}

void startInstall(const Paths& p, const Config& c, State& s) {
    if (s.busy.exchange(true)) return;
    s.cancel = false;
    {
        std::lock_guard<std::mutex> lk(s.mu);
        s.error.clear();
    }
    std::thread([p, c, &s] {
        try {
            if (c.iso.empty() || !fs::exists(c.iso)) fail("Choose your Imabikiso disc image (.iso) first.");
            if (!fs::exists(p.imbk)) fail("imbk.exe is missing next to the launcher.");
            fs::create_directories(c.dataDir);
            const Status st = queryStatus(p, c);
            if (!st.discReady) {
                stepVerify(p, c, s);
                stepUnpack(p, c, s);
            }
            if (!queryStatus(p, c).elfReady) stepElf(p, c, s);
            if (!queryStatus(p, c).langReady) stepLanguage(p, c, s);
            if (!fs::exists(p.game)) fail("Game files ready, but game\\ImabikisoPC.exe is missing next to the launcher.");
            s.setStep("Ready to play", 1.f);
        } catch (const std::exception& e) {
            std::lock_guard<std::mutex> lk(s.mu);
            s.error = e.what();
            s.step = "Stopped";
            s.log.push_back("ERROR: " + s.error);
        }
        s.busy = false;
    }).detach();
}

// ---------------------------------------------------------------- play ------

static std::wstring environmentWith(const std::map<std::wstring, std::wstring>& set) {
    std::map<std::wstring, std::wstring> env;
    if (wchar_t* block = GetEnvironmentStringsW()) {
        for (const wchar_t* e = block; *e; e += wcslen(e) + 1) {
            const std::wstring kv = e;
            const auto eq = kv.find(L'=', 1);
            if (eq != std::wstring::npos) env[kv.substr(0, eq)] = kv.substr(eq + 1);
        }
        FreeEnvironmentStringsW(block);
    }
    // Test hooks must never leak into normal play.
    for (const wchar_t* k : {L"PAD_SCRIPT", L"PAD_FILE", L"LD_FRAME_DUMP", L"AUDIO_WAV"}) env.erase(k);
    for (const auto& [k, v] : set) env[k] = v;
    std::wstring out;
    for (const auto& [k, v] : env) out += k + L"=" + v + L'\0';
    out += L'\0';
    return out;
}

void startPlay(const Paths& p, const Config& c, State& s) {
    if (s.busy || s.gameRunning.exchange(true)) return;
    {
        std::lock_guard<std::mutex> lk(s.mu);
        s.error.clear();
    }
    std::thread([p, c, &s] {
        try {
            const bool ja = c.language == "ja";
            const fs::path vfs = ja ? p.disc(c) : p.langDisc(c, c.language);
            const fs::path elf = ja ? p.elf(c) : p.langElf(c, c.language);
            const fs::path profile = p.profiles / c.language;
            fs::create_directories(profile);
            fs::create_directories(p.logs);
            const std::wstring env = environmentWith({
                {L"PS3_VFS_ROOT", vfs.wstring()},
                {L"PS3_TITLE", ja ? L"Imabikiso PC" : L"Imabikiso PC - English draft"},
                {L"PS3_ENTER_BUTTON", L"circle"},
                {L"RSX_LIVE_DRAW", L"1"},
                {L"RSX_VSYNC", c.vsync ? L"1" : L"0"},
                {L"PS3_VERBOSE", L"0"},
                {L"AUDIO_FILL_CURSOR", L"1"},
                {L"AUDIO_HOST_MUTE", c.mute ? L"1" : L"0"},
                {L"AUDIO_HOST_PRIORITY", L"1"},
                {L"SPU_HOST_PRIORITY", L"1"},
                // Crash / hang reports (minidumps) go next to the game logs.
                {L"IMBK_DUMP_DIR", p.logs.wstring()},
            });
            SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
            HANDLE out = CreateFileW((p.logs / "game.out.txt").c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa,
                                     CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            HANDLE err = CreateFileW((p.logs / "game.err.txt").c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa,
                                     CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            STARTUPINFOW si{};
            si.cb = sizeof si;
            si.dwFlags = STARTF_USESTDHANDLES;
            si.hStdOutput = out;
            si.hStdError = err;
            PROCESS_INFORMATION pi{};
            std::wstring cmd = quote(p.game) + L" " + quote(elf);
            s.addLog("Starting the game (" + c.language + ")");
            const BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_UNICODE_ENVIRONMENT,
                                           (LPVOID)env.c_str(), profile.c_str(), &si, &pi);
            CloseHandle(out);
            CloseHandle(err);
            if (!ok) fail("Cannot start game/ImabikisoPC.exe");
            WaitForSingleObject(pi.hProcess, INFINITE);
            DWORD code = 0;
            GetExitCodeProcess(pi.hProcess, &code);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            char msg[96];
            std::snprintf(msg, sizeof msg, "Game closed (exit code 0x%lX). Logs: logs\\game.err.txt", (unsigned long)code);
            s.addLog(msg);
            if (code != 0) {
                std::lock_guard<std::mutex> lk(s.mu);
                s.error = msg;
            }
        } catch (const std::exception& e) {
            std::lock_guard<std::mutex> lk(s.mu);
            s.error = e.what();
            s.log.push_back("ERROR: " + s.error);
        }
        s.gameRunning = false;
    }).detach();
}

void openFolder(const fs::path& dir) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

} // namespace launcher
