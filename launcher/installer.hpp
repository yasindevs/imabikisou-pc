// Headless side of the launcher: settings, the install steps and the game
// process. The UI (main.cpp) only reads State and calls the entry points; all
// long work runs on a worker thread and reports through State.
#pragma once

#include <atomic>
#include <deque>
#include <filesystem>
#include <mutex>
#include <string>

namespace launcher {

namespace fs = std::filesystem;

// Persistent settings, stored as key=value lines in launcher.ini next to the exe.
struct Config {
    fs::path iso;          // the user's own disc image
    fs::path rpcs3;        // rpcs3.exe, used once to decrypt EBOOT.BIN
    fs::path eboot;        // or an already decrypted EBOOT.ELF
    fs::path dataDir;      // extracted disc and prepared languages
    std::string language = "en"; // "ja" or "en"
    bool vsync = false;
    bool mute = false;

    static Config load(const fs::path& file);
    void save(const fs::path& file) const;
};

// Where everything lives, derived from the launcher's own folder.
struct Paths {
    fs::path base;      // launcher folder
    fs::path imbk;      // base/imbk.exe
    fs::path game;      // base/game/ImabikisoPC.exe
    fs::path langDir;   // base/lang (story.tsv / menu.tsv per language)
    fs::path ini;       // base/launcher.ini
    fs::path profiles;  // base/profiles/<language>: working dir, saves in gamedata/
    fs::path logs;      // base/logs

    static Paths fromExe();
    fs::path disc(const Config& c) const { return c.dataDir / "disc"; }
    fs::path elf(const Config& c) const { return c.dataDir / "EBOOT.ELF"; }
    fs::path langDisc(const Config& c, const std::string& l) const { return c.dataDir / "lang" / l / "disc"; }
    fs::path langElf(const Config& c, const std::string& l) const { return c.dataDir / "lang" / l / "EBOOT.ELF"; }
};

// Progress shared with the UI. Guarded by `mu` except the atomics.
struct State {
    std::mutex mu;
    std::string step;              // current step, for the progress line
    float progress = 0.f;          // 0..1 within the step, <0 = indeterminate
    std::deque<std::string> log;   // recent lines (bounded)
    size_t dropped = 0;            // lines removed from the front so far
    std::string error;             // last failure, empty on success
    std::atomic<bool> busy{false};
    std::atomic<bool> cancel{false};
    std::atomic<bool> gameRunning{false};

    void setStep(const std::string& s, float p = -1.f);
    void setProgress(float p);
    void addLog(const std::string& line);
};

// What is already prepared on disk.
struct Status {
    bool discReady = false;   // data/disc holds the extracted game
    bool elfReady = false;    // data/EBOOT.ELF is the supported decrypted executable
    bool langReady = false;   // the selected language is prepared (always true for "ja")
    bool gameExe = false;     // game/ImabikisoPC.exe is present
};
Status queryStatus(const Paths& p, const Config& c);

// Worker entry points; each starts a thread and returns at once.
void startInstall(const Paths& p, const Config& c, State& s);
void startPlay(const Paths& p, const Config& c, State& s);

// Helpers for the UI.
std::string toUtf8(const std::wstring& w);
std::wstring toWide(const std::string& s);
void openFolder(const fs::path& dir);

// Supported input: SHA-256 of the decrypted EBOOT.ELF of BLJM60042 v01.00.
constexpr const char* kElfSha256 = "735d34f943b13788a7a776226181e1ae456b0bcaf3e6e495f6f542bb3fd143db";

} // namespace launcher
