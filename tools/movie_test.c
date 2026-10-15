#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <limits.h>
#include "../port/movie_native.h"

/* Usage: movie_test [sample-dir [ffmpeg.exe [output-dir]]]
   Run from the repository root by default. Only one worker decodes each movie;
   the calling thread copies output. Timing includes open, decode, copy, close,
   and file I/O, but excludes ffmpeg/reference comparison and validation. */
typedef struct {
    unsigned char *data;
    int w, h, cw, ch, yp, cp;
    size_t bytes;
} planes;

static int alloc_planes(planes *p, int w, int h) {
    p->w = w; p->h = h; p->cw = (w + 1) / 2; p->ch = (h + 1) / 2;
    p->yp = w + 17; p->cp = p->cw + 13;
    p->bytes = (size_t)p->yp * h + 2 * (size_t)p->cp * p->ch;
    p->data = (unsigned char *)malloc(p->bytes);
    if (p->data) memset(p->data, 0xa5, p->bytes);
    return p->data != NULL;
}

static int get_frame(imbk_movie *m, int index, planes *p) {
    unsigned char *u = p->data + (size_t)p->yp * p->h;
    return imbk_movie_frame(m, index, p->data, p->yp, u, p->cp,
                            u + (size_t)p->cp * p->ch, p->cp);
}

static int padding_ok(planes *p) {
    int plane, row, col;
    unsigned char *base = p->data;
    for (plane = 0; plane < 3; ++plane) {
        int w = plane ? p->cw : p->w, h = plane ? p->ch : p->h;
        int pitch = plane ? p->cp : p->yp;
        for (row = 0; row < h; ++row)
            for (col = w; col < pitch; ++col)
                if (base[(size_t)row * pitch + col] != 0xa5) return 0;
        base += (size_t)pitch * h;
    }
    return 1;
}

static uint64_t frame_hash(planes *p) {
    size_t i;
    uint64_t hash = UINT64_C(14695981039346656037);
    for (i = 0; i < p->bytes; ++i) hash = (hash ^ p->data[i]) * UINT64_C(1099511628211);
    return hash;
}

static unsigned long long file_size(const char *path) {
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &a)) return 0;
    return ((unsigned long long)a.nFileSizeHigh << 32) | a.nFileSizeLow;
}

static int reference(const char *ffmpeg, const char *path, const char *output) {
    char command[4096];
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    DWORD result;
    memset(&si, 0, sizeof(si)); si.cb = sizeof(si);
    memset(&pi, 0, sizeof(pi));
    snprintf(command, sizeof(command),
             "\"%s\" -hide_banner -loglevel error -y -i \"%s\" -an -f rawvideo -pix_fmt yuv420p -frames:v 60 \"%s\"",
             ffmpeg, path, output);
    if (!CreateProcessA(ffmpeg, command, NULL, NULL, FALSE, CREATE_NO_WINDOW,
                        NULL, NULL, &si, &pi)) return 0;
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &result);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return result == 0;
}

static int compare_reference(const char *path, const char *ffmpeg, const char *outdir) {
    char output[MAX_PATH];
    imbk_movie *m;
    planes p = {0};
    FILE *ref = NULL, *native = NULL;
    unsigned char *raw = NULL;
    int index, ok = 0;
    double minimum = 1e9;
    snprintf(output, sizeof(output), "%s\\chun_logo_ffmpeg_60.yuv", outdir);
    if (!reference(ffmpeg, path, output)) { puts("FAIL: ffmpeg reference generation"); return 0; }
    m = imbk_movie_open(path, 0, file_size(path));
    if (!m || !alloc_planes(&p, imbk_movie_width(m), imbk_movie_height(m))) goto done;
    ref = fopen(output, "rb");
    snprintf(output, sizeof(output), "%s\\chun_logo_native_60.yuv", outdir);
    native = fopen(output, "wb");
    raw = (unsigned char *)malloc((size_t)p.w * p.h + 2 * (size_t)p.cw * p.ch);
    if (!ref || !native || !raw) goto done;
    for (index = 0; index < 60; ++index) {
        uint64_t squared = 0;
        int x, y;
        size_t n = (size_t)p.w * p.h + 2 * (size_t)p.cw * p.ch;
        double psnr;
        if (fread(raw, 1, n, ref) != n || !get_frame(m, index, &p)) goto done;
        {
            int plane, row;
            const unsigned char *base = p.data;
            for (plane = 0; plane < 3; ++plane) {
                int pw = plane ? p.cw : p.w, ph = plane ? p.ch : p.h;
                int pitch = plane ? p.cp : p.yp;
                for (row = 0; row < ph; ++row)
                    if (fwrite(base + (size_t)row * pitch, 1, pw, native) != (size_t)pw) goto done;
                base += (size_t)pitch * ph;
            }
        }
        for (y = 0; y < p.h; ++y) for (x = 0; x < p.w; ++x) {
            int d = p.data[(size_t)y * p.yp + x] - raw[(size_t)y * p.w + x];
            squared += (uint64_t)(d * d);
        }
        psnr = squared ? 10.0 * log10(65025.0 * p.w * p.h / (double)squared) : HUGE_VAL;
        if (psnr < minimum) minimum = psnr;
        if (squared) printf("  frame %02d Y PSNR %.4f dB\n", index, psnr);
        else printf("  frame %02d Y PSNR inf (identical)\n", index);
    }
    printf("First 60 frames: minimum Y PSNR %.4f dB\n", minimum);
    ok = minimum > 45.0 && padding_ok(&p);
done:
    if (ref) fclose(ref);
    if (native) fclose(native);
    free(raw); free(p.data); imbk_movie_close(m);
    return ok;
}

/* Embed the complete sample between non-movie sentinels, then test range
   isolation, repeated/backward/forward access, EOF recovery, padded pitches,
   independent simultaneous handles, invalid input, and early worker close. */
static int validate_access(const char *path, const char *outdir, int count) {
    char embedded[MAX_PATH];
    unsigned char chunk[65536];
    FILE *in = NULL, *out = NULL;
    imbk_movie *a = NULL, *b = NULL, *bad = NULL;
    planes p = {0}, q = {0};
    size_t n;
    uint64_t first, later;
    int ok = 0;
    const unsigned long long offset = 137;
    unsigned long long size = file_size(path);
    snprintf(embedded, sizeof(embedded), "%s\\bounded_movie.bin", outdir);
    in = fopen(path, "rb"); out = fopen(embedded, "wb");
    if (!in || !out) goto done;
    memset(chunk, 0xcc, sizeof(chunk));
    if (fwrite(chunk, 1, (size_t)offset, out) != offset) goto done;
    while ((n = fread(chunk, 1, sizeof(chunk), in)) != 0)
        if (fwrite(chunk, 1, n, out) != n) goto done;
    memset(chunk, 0xcc, sizeof(chunk));
    if (fwrite(chunk, 1, sizeof(chunk), out) != sizeof(chunk)) goto done;
    fclose(in); in = NULL; fclose(out); out = NULL;
    a = imbk_movie_open(path, 0, size);
    b = imbk_movie_open(embedded, offset, size);
    if (!a || !b || !alloc_planes(&p, imbk_movie_width(a), imbk_movie_height(a)) ||
        !alloc_planes(&q, imbk_movie_width(b), imbk_movie_height(b))) goto done;
    if (!get_frame(a, 0, &p) || !get_frame(b, 0, &q)) goto done;
    first = frame_hash(&p);
    if (first != frame_hash(&q) || !get_frame(a, 0, &p) || first != frame_hash(&p)) goto done;
    if (!get_frame(a, 59, &p) || !get_frame(b, 59, &q)) goto done;
    later = frame_hash(&p);
    if (later != frame_hash(&q) || !get_frame(a, 0, &p) || frame_hash(&p) != first ||
        !get_frame(a, 59, &p) || frame_hash(&p) != later) goto done;
    if (get_frame(b, count, &q) || get_frame(b, count + 10, &q) ||
        !get_frame(b, count - 1, &q) || !get_frame(b, 0, &q) || frame_hash(&q) != first) goto done;
    if (get_frame(a, -1, &p) || imbk_movie_frame(a, 0, p.data, p.w - 1,
        p.data, p.cp, p.data, p.cp) || !padding_ok(&p) || !padding_ok(&q)) goto done;
    bad = imbk_movie_open(embedded, 0, offset); /* Must not discover following video. */
    if (bad) goto done;
    bad = imbk_movie_open(embedded, offset + size, sizeof(chunk));
    if (bad) goto done;
    bad = imbk_movie_open(path, size, 1);
    if (bad) goto done;
    bad = imbk_movie_open(path, ULLONG_MAX, 2);
    if (bad) goto done;
    bad = imbk_movie_open(path, 0, 0);
    if (bad) goto done;
    bad = imbk_movie_open(path, 0, size);
    if (!bad) goto done;
    imbk_movie_close(bad); bad = NULL; /* close with decode in flight */
    imbk_movie_close(a); a = NULL;
    imbk_movie_close(b); b = NULL;
    /* A range ending inside a PES/picture must match the same truncated
       standalone file prefix, despite valid video immediately beyond it. */
    a = imbk_movie_open(path, 0, 3 * 1024 * 1024);
    b = imbk_movie_open(embedded, offset, 3 * 1024 * 1024);
    if (!a || !b) goto done;
    {
        int index, ra, rb;
        for (index = 0; ; ++index) {
            ra = get_frame(a, index, &p); rb = get_frame(b, index, &q);
            if (ra != rb || (ra && frame_hash(&p) != frame_hash(&q))) goto done;
            if (!ra) break;
        }
        if (!index || index >= count) goto done;
    }
    ok = 1;
done:
    if (in) fclose(in);
    if (out) fclose(out);
    imbk_movie_close(a); imbk_movie_close(b); imbk_movie_close(bad);
    free(p.data); free(q.data);
    printf("Range/seek/repeat/EOF/pitch/handles/close validation: %s\n", ok ? "PASS" : "FAIL");
    return ok;
}

int main(int argc, char **argv) {
    const char *samples = argc > 1 ? argv[1] : "movie_samples";
    const char *ffmpeg = argc > 2 ? argv[2] : "C:\\Program Files\\ffmpeg\\ffmpeg.exe";
    const char *outdir = argc > 3 ? argv[3] : "codex_tasks\\build_a";
    char pattern[MAX_PATH], path[MAX_PATH], logo[MAX_PATH] = {0};
    WIN32_FIND_DATAA entry;
    HANDLE find;
    LARGE_INTEGER freq, start, end;
    int failed = 0, files = 0, logo_count = 0;
    QueryPerformanceFrequency(&freq);
    CreateDirectoryA(outdir, NULL);
    snprintf(pattern, sizeof(pattern), "%s\\*.sfd", samples);
    find = FindFirstFileA(pattern, &entry);
    if (find == INVALID_HANDLE_VALUE) { puts("No sample movies found"); return 1; }
    do {
        imbk_movie *m;
        planes p = {0};
        int count = 0, w, h;
        double fps, seconds;
        if (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        ++files;
        snprintf(path, sizeof(path), "%s\\%s", samples, entry.cFileName);
        QueryPerformanceCounter(&start);
        m = imbk_movie_open(path, 0, file_size(path));
        if (!m || !alloc_planes(&p, imbk_movie_width(m), imbk_movie_height(m))) {
            printf("FAIL: %s open/allocation\n", path); imbk_movie_close(m); ++failed; continue;
        }
        w = imbk_movie_width(m); h = imbk_movie_height(m); fps = imbk_movie_fps(m);
        while (get_frame(m, count, &p)) ++count;
        imbk_movie_close(m);
        QueryPerformanceCounter(&end);
        seconds = (double)(end.QuadPart - start.QuadPart) / freq.QuadPart;
        printf("%s: %dx%d %.6f fps, %d frames, %.6f s, %.2f decode fps (one decode worker)\n",
               entry.cFileName, w, h, fps, count, seconds, count / seconds);
        if (!count || !padding_ok(&p)) ++failed;
        free(p.data);
        if (!_stricmp(entry.cFileName, "chun_logo.sfd")) {
            snprintf(logo, sizeof(logo), "%s", path); logo_count = count;
        }
    } while (FindNextFileA(find, &entry));
    FindClose(find);
    if (!logo[0]) { puts("FAIL: chun_logo.sfd missing"); ++failed; }
    else {
        if (logo_count != 150) { puts("FAIL: chun_logo must contain 150 frames"); ++failed; }
        if (!compare_reference(logo, ffmpeg, outdir)) ++failed;
        if (!validate_access(logo, outdir, logo_count)) ++failed;
    }
    printf("%s: %d samples, %d failed checks\n", failed ? "FAIL" : "PASS", files, failed);
    return failed ? 1 : 0;
}
