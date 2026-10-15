/* Native movie decoding for Sofdec (CRI MWSFD 4.46).
 *
 * Sofdec decodes MPEG-1 on two "SPU Delegate" threads; through the recompiled
 * SPU code that is ~4x too slow for the 1280x720 ~26 Mbit/s movies, so they
 * stuttered. Here the same movie is decoded on the PC (movie_native.c,
 * pl_mpeg) and written into the YUV frame buffer that Sofdec hands to its own
 * converter -- everything else (timing, loops, ADX sound, the RGBA conversion
 * and the drawing) stays the game's.
 *
 *   movie identity  mwPlyStartAfs/StartAfsLp/EntryAfs(hn, pid, fid) give an
 *                   AFS partition id and file index; the first read from an
 *                   .afs archive at that entry binds pid -> archive.
 *   frame           mwPlyFxReqCnvFrmRgba(hn, cnvobj, rgba_out): cnvobj+0x00 is
 *                   the frame's YUV buffer (Y 1280x736, then U and V 640x368,
 *                   pitches 1280/640), cnvobj+0x04 its display frame number.
 *
 * IMBK_MOVIE=0 disables, =check compares with the game's own decode instead
 * of replacing it (prints Y PSNR).
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>
#include <map>
#include "movie_native.h"

extern "C" uint8_t* vm_base;
extern "C" void (*g_ps3_fs_read_hook)(int fd, const char* guest_path, long long pos, unsigned long long n);

static void update_native_active();

namespace {

struct AfsToc { std::vector<std::pair<uint32_t, uint32_t>> e; bool ok = false; };

struct Movie {
    imbk_movie* mv = nullptr;
    std::string path;
    uint32_t fid = 0;
    int frames = -1;          /* known once a request ran past the end */
    std::vector<uint8_t> y, u, v;
};

std::mutex g_mu;
std::map<uint32_t, std::string> g_pid_path;        /* AFS partition id -> host path */
std::map<std::string, AfsToc> g_toc;
struct Pending { uint32_t hn, pid, fid; };
std::vector<Pending> g_pending;
std::map<uint32_t, Movie> g_movies;                /* Sofdec handle -> movie */

int mode()
{
    static int m = -1;
    if (m < 0) {
        const char* e = getenv("IMBK_MOVIE");
        m = !e ? 1 : !strcmp(e, "check") ? 2 : atoi(e) ? 1 : 0;
    }
    return m;
}

std::string host_path(const char* guest)
{
    const char* root = getenv("PS3_VFS_ROOT");
    std::string g = guest ? guest : "";
    const std::string pre = "/dev_bdvd/";
    if (!root || g.compare(0, pre.size(), pre) != 0) return std::string();
    std::string p = std::string(root) + "\\" + g.substr(pre.size());
    for (auto& c : p) if (c == '/') c = '\\';
    std::string out;                                /* collapse "\\\\" from "data//chunk" */
    for (size_t i = 0; i < p.size(); i++) if (!(p[i] == '\\' && !out.empty() && out.back() == '\\')) out += p[i];
    return out;
}

const AfsToc& toc_of(const std::string& path)
{
    auto it = g_toc.find(path);
    if (it != g_toc.end()) return it->second;
    AfsToc t;
    if (FILE* f = fopen(path.c_str(), "rb")) {
        uint8_t h[8];
        if (fread(h, 1, 8, f) == 8 && !memcmp(h, "AFS", 3)) {
            const uint32_t n = h[4] | h[5] << 8 | h[6] << 16 | (uint32_t)h[7] << 24;
            std::vector<uint8_t> b((size_t)n * 8);
            if (n < 100000 && fread(b.data(), 1, b.size(), f) == b.size()) {
                for (uint32_t i = 0; i < n; i++) {
                    const uint8_t* q = &b[i * 8];
                    t.e.push_back({ q[0] | q[1] << 8 | q[2] << 16 | (uint32_t)q[3] << 24,
                                    q[4] | q[5] << 8 | q[6] << 16 | (uint32_t)q[7] << 24 });
                }
                t.ok = true;
            }
        }
        fclose(f);
    }
    return g_toc[path] = t;
}

void open_movie(uint32_t hn, const std::string& path, uint32_t fid)
{
    const AfsToc& t = toc_of(path);
    if (!t.ok || fid >= t.e.size()) return;
    auto& m = g_movies[hn];
    if (m.mv && m.path == path && m.fid == fid) return;
    if (m.mv) imbk_movie_close(m.mv);
    m = Movie();
    m.mv = imbk_movie_open(path.c_str(), t.e[fid].first, t.e[fid].second);
    m.path = path; m.fid = fid;
    fprintf(stderr, "[movie] hn=0x%08X native decode of %s entry %u (0x%X, %u bytes) -> %s\n",
            hn, path.c_str(), fid, t.e[fid].first, t.e[fid].second, m.mv ? "ok" : "FAILED");
}

void fs_read_hook(int, const char* guest, long long pos, unsigned long long)
{
    if (!guest || !strstr(guest, ".afs")) return;
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_pending.empty()) return;
    const std::string path = host_path(guest);
    if (path.empty()) return;
    const AfsToc& t = toc_of(path);
    if (!t.ok) return;
    for (size_t i = 0; i < g_pending.size(); i++) {
        const Pending p = g_pending[i];
        if (p.fid >= t.e.size()) continue;
        const long long lo = t.e[p.fid].first, hi = lo + t.e[p.fid].second;
        if (pos >= lo && pos < hi) {
            g_pid_path[p.pid] = path;
            fprintf(stderr, "[movie] AFS partition %u = %s\n", p.pid, path.c_str());
            open_movie(p.hn, path, p.fid);
            g_pending.erase(g_pending.begin() + (long)i);
            update_native_active();
            return;
        }
    }
}

double psnr(const uint8_t* a, const uint8_t* b, size_t n)
{
    double se = 0;
    for (size_t i = 0; i < n; i++) { const double d = (double)a[i] - b[i]; se += d * d; }
    return se ? 10.0 * log10(255.0 * 255.0 * n / se) : 99.0;
}

} // namespace

/* Sofdec's picture decode on the "SPU Delegate" threads (lifted image 5,
 * spu_0004): the task dispatcher at LS 0xB00 calls the task body through a
 * pointer -- LS 0x20458, returning to 0xB28 -- then reports completion from
 * 0xC00/0xC20. With every playing movie decoded natively that body is wasted
 * work (~50 ms per half picture on the heavy movies) and the reason Sofdec
 * fell behind, so the call returns at once. IMBK_MOVIE_SKIP_SPU=0 keeps it. */
extern "C" int (*g_spu_ib_skip)(void* ctx, uint32_t target, uint32_t link);
static std::atomic<int> g_native_active{0}; /* every started handle is native (written under g_mu) */
static int ib_skip(void* ctx, uint32_t target, uint32_t link)
{
    (void)ctx;
    return target == 0x20458u && link == 0xB28u && g_native_active.load(std::memory_order_acquire);
}

static void update_native_active()
{
    bool all = !g_movies.empty() && g_pending.empty();
    for (auto& kv : g_movies) if (!kv.second.mv) all = false;
    g_native_active.store(all && mode() == 1, std::memory_order_release);
}

extern "C" void imbk_movie_install(void)
{
    if (!mode()) return;
    g_ps3_fs_read_hook = fs_read_hook;
    const char* e = getenv("IMBK_MOVIE_SKIP_SPU");
    if (!(e && *e == '0')) g_spu_ib_skip = ib_skip;
}

/* mwPlyStartAfs / StartAfsLp / EntryAfs */
extern "C" void imbk_movie_on_start(uint32_t hn, uint32_t pid, uint32_t fid)
{
    if (!mode()) return;
    std::lock_guard<std::mutex> lk(g_mu);
    auto it = g_pid_path.find(pid);
    if (it != g_pid_path.end()) open_movie(hn, it->second, fid);
    else g_pending.push_back({ hn, pid, fid });
    update_native_active();
}

/* mwSfdStop */
extern "C" void imbk_movie_on_stop(uint32_t hn)
{
    std::lock_guard<std::mutex> lk(g_mu);
    auto it = g_movies.find(hn);
    if (it == g_movies.end()) return;
    if (it->second.mv) imbk_movie_close(it->second.mv);
    g_movies.erase(it);
    update_native_active();
}

/* mwPlyFxReqCnvFrmRgba entry: fill the frame's YUV buffer before the game's
 * converter reads it. Returns 1 when the frame came from the native decoder. */
extern "C" int imbk_movie_on_convert(uint32_t hn, uint32_t cnvobj)
{
    if (!mode() || !cnvobj) return 0;
    std::lock_guard<std::mutex> lk(g_mu);
    update_native_active();
    auto it = g_movies.find(hn);
    if (it == g_movies.end() || !it->second.mv) return 0;
    Movie& m = it->second;
    const auto rd = [](uint32_t ea) {
        const uint8_t* p = vm_base + ea;
        return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
    };
    const uint32_t buf = rd(cnvobj + 0x00);
    int frame = (int)rd(cnvobj + 0x04);
    const int w = imbk_movie_width(m.mv), h = imbk_movie_height(m.mv);
    if (w != 1280 || h != 720 || buf < 0x10000u || frame < 0) return 0;
    if (m.frames > 0) frame %= m.frames;
    m.y.resize((size_t)w * h); m.u.resize((size_t)w * h / 4); m.v.resize((size_t)w * h / 4);
    if (!imbk_movie_frame(m.mv, frame, m.y.data(), w, m.u.data(), w / 2, m.v.data(), w / 2)) {
        /* past the end of a looping movie: learn its length and wrap */
        if (m.frames < 0 && frame > 0) {
            m.frames = frame;
            frame %= m.frames;
            if (!imbk_movie_frame(m.mv, frame, m.y.data(), w, m.u.data(), w / 2, m.v.data(), w / 2)) return 0;
        } else return 0;
    }
    uint8_t* Y = vm_base + buf;
    uint8_t* U = Y + 1280 * 736;
    uint8_t* V = U + 640 * 368;
    if (mode() == 2) {
        static int n = 0;
        if (n++ < 40) {
            std::vector<uint8_t> g((size_t)w * h);
            for (int r = 0; r < h; r++) memcpy(&g[(size_t)r * w], Y + (size_t)r * 1280, w);
            std::vector<uint8_t> gu((size_t)w * h / 4);
            for (int r = 0; r < h / 2; r++) memcpy(&gu[(size_t)r * w / 2], U + (size_t)r * 640, w / 2);
            fprintf(stderr, "[movie] check hn=0x%08X frame %d buf=0x%08X  Y PSNR %.1f dB  U PSNR %.1f dB\n",
                    hn, frame, buf, psnr(g.data(), m.y.data(), g.size()), psnr(gu.data(), m.u.data(), gu.size()));
        }
        return 0;
    }
    for (int r = 0; r < h; r++)     memcpy(Y + (size_t)r * 1280, &m.y[(size_t)r * w], w);
    for (int r = 0; r < h / 2; r++) memcpy(U + (size_t)r * 640, &m.u[(size_t)r * w / 2], w / 2);
    for (int r = 0; r < h / 2; r++) memcpy(V + (size_t)r * 640, &m.v[(size_t)r * w / 2], w / 2);
    return 1;
}
