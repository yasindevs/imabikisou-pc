/*
 * ps3recomp game project -- entry point template.
 *
 * This is the runner. It boots a lifted title through the toolkit's PPU
 * scaffold: allocate the flat guest VM, load the PPU ELF named on the command
 * line, register the lifted function table and the HLE NID handlers, start a
 * frame clock, and dispatch the entry OPD.
 *
 * The scaffold does the work; nothing below reimplements it. ppu_load_elf,
 * ppu_recomp_register, ppu_hle_init, ppu_sysprx_register, ppu_fs_register,
 * lv2_init_syscalls and ppu_run all come from the toolkit, compiled into this
 * project from PS3RECOMP_DIR (see CMakeLists.txt). What is left here is the
 * part a port owns: which backend to present through, how the frame clock is
 * paced, and whatever diagnostics the title turns out to need.
 *
 * runtime/ppu/tests/boot_main.cpp in the toolkit is the same boot in its
 * fully-instrumented form -- crash filter, hang watchdog, guest-PC sampling
 * profiler. Read it when a boot goes wrong; copy from it what the title
 * needs. It is deliberately not what a fresh project starts with.
 *
 * The one part that IS duplicated from it is the frame clock below, because a
 * port is expected to change its pacing and a shared one would be the wrong
 * shape for that. It was copied faithfully, comments and all. If a pacing bug
 * is fixed in one of the two, look at the other.
 *
 * This builds on Windows, macOS and Linux. CI builds and runs it against the
 * boot smoke title on the last two, and compile-checks it against clang-cl on
 * the first (tools/check_ppu_scaffold.py).
 */

/* The lifter's generated header, which declares ppu_context and the function
 * table. It comes first: everything below is written against that struct. */
#include "ppu_recomp.h"

/* The Win32 names -- Sleep, GetTickCount64, CreateThread, InterlockedIncrement,
 * VirtualAlloc, the scalar typedefs -- from one place. On Windows this is a
 * passthrough to <windows.h>; off Windows the toolkit's shim supplies the same
 * names over pthreads and mmap, so the call sites below are written once. */
#include "win32_compat.h"

#include "vm.h"   /* vm_stack_allocate, for the main thread's stack */
extern "C" vm_stack_alloc g_vm_stack_alloc;   /* sys_ppu_thread.c */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
/* timeBeginPeriod: <windows.h> arrives with WIN32_LEAN_AND_MEAN set, which
 * excludes the multimedia timer API, so it is asked for by name -- and after
 * windows.h, since timeapi.h uses UINT. No shim exists off Windows because
 * there is nothing to shim: the 15.6 ms default timer granularity this works
 * around is a Windows problem. */
#include <timeapi.h>
#include <tlhelp32.h>
#endif

/* ---------------------------------------------------------------------------
 * What the scaffold gives this file
 * -----------------------------------------------------------------------*/
extern "C" {
uint32_t ppu_load_elf(const char* path);      /* ELF -> guest RAM, returns entry OPD */
void     ppu_recomp_register(void);           /* generated: lifted table -> address map */
void     ppu_hle_init(void);                  /* firmware import NID -> HLE handler */
void     ppu_sysprx_register(void);           /* boot-critical CRT (sys_initialize_tls, ...) */
void     ppu_fs_register(void);               /* cellFs over the real game directory */
void     lv2_init_syscalls(void);             /* the lv2 syscall table */
int      ppu_run(uint32_t entry_opd, uint32_t stack_top);

/* This project's own: load whatever real system PRX modules the title needs
 * into guest RAM and register their exports. stubs.cpp has the empty version
 * a game with no lifted PRX wants. */
void     ps3_load_prx_modules(void);

extern const char* ppu_vfs_root;              /* host dir the PS3 mount points map into */

/* The status the guest handed to sys_process_exit. A title that exits that way
 * never returns through ppu_run, so this is only read on the path where the
 * entry function unwound instead. */
extern int     g_sys_process_exit_called;
extern int32_t g_sys_process_exit_code;

/* Host-provided symbols the runtime and the HLE libraries link against. */
uint8_t* vm_base = nullptr;
extern uint32_t ppu_vm_size;                  /* ppu_loader.cpp: the OOB guard */

/* Guest-callback dispatch. g_ps3_guest_caller is the hook the HLE runtime
 * calls back into recompiled code through -- cellSysutil events and the GCM
 * vblank/flip handlers. ppu_guest_call does the OPD -> dispatch. */
typedef void (*ps3_guest_caller_fn)(uint32_t, uint64_t, uint64_t, uint64_t, uint64_t,
                                    uint64_t, uint64_t, uint64_t, uint64_t);
extern ps3_guest_caller_fn g_ps3_guest_caller;
uint64_t ppu_guest_call(uint32_t, uint64_t, uint64_t, uint64_t, uint64_t,
                        uint64_t, uint64_t, uint64_t, uint64_t);

/* The GCM half of the frame clock. */
void     cellGcmTickVBlank(void);
void     cellGcmTickFlip(void);
int      cellGcm_take_flip_pending(void);
void     cellGcm_rsx_process_fifo(void);      /* drain get -> put */
unsigned cellGcm_flip_request_count(void);
}

/* ---------------------------------------------------------------------------
 * RSX present backend
 * -----------------------------------------------------------------------*/
/* D3D12 on Windows, Metal on Apple, and the null backend's headless software
 * path anywhere else -- the same selection runtime/host/host_posix.c and the
 * toolkit's boot harness make. All three expose the same three entry points,
 * so the frame clock below is backend-agnostic. */
#if defined(_WIN32)
extern "C" int  rsx_d3d12_backend_init(uint32_t w, uint32_t h, const char* title);
extern "C" void rsx_d3d12_backend_present(void);
extern "C" int  rsx_d3d12_backend_pump_messages(void);
#  define rsx_backend_init    rsx_d3d12_backend_init
#  define rsx_backend_present rsx_d3d12_backend_present
#  define rsx_backend_pump    rsx_d3d12_backend_pump_messages
#  define RSX_BACKEND_NAME    "D3D12"
#elif defined(__APPLE__)
extern "C" int  rsx_metal_backend_init(uint32_t w, uint32_t h, const char* title);
extern "C" void rsx_metal_backend_present(void);
extern "C" int  rsx_metal_backend_pump_messages(void);
#  define rsx_backend_init    rsx_metal_backend_init
#  define rsx_backend_present rsx_metal_backend_present
#  define rsx_backend_pump    rsx_metal_backend_pump_messages
#  define RSX_BACKEND_NAME    "Metal"
#else
extern "C" int  rsx_null_backend_init(uint32_t w, uint32_t h, const char* title);
extern "C" void rsx_null_backend_present(void);
extern "C" int  rsx_null_backend_pump_messages(void);
#  define rsx_backend_init    rsx_null_backend_init
#  define rsx_backend_present rsx_null_backend_present
#  define rsx_backend_pump    rsx_null_backend_pump_messages
#  define RSX_BACKEND_NAME    "null (headless software)"
#endif

/* ---------------------------------------------------------------------------
 * Configuration
 * -----------------------------------------------------------------------*/
/* The flat VM must span every region the PS3 memory map uses, not just the
 * game image: sys_ppu_thread_create puts thread stacks at 0xD0000000, so a
 * smaller arena makes every spawned thread's stack access out of bounds. */
#define VM_SIZE       0x100010000ull   /* the full 32-bit guest space + a 64K guard */
#define STACK_TOP     0x0FF00000u      /* main-thread stack, below the 0x10000000 segment */
#define WINDOW_WIDTH  1280
#define WINDOW_HEIGHT 720

/* ---------------------------------------------------------------------------
 * Frame clock
 * -----------------------------------------------------------------------*/
/* On real hardware the RSX raises a vblank interrupt about 60 times a second
 * and that is what drives the game's frame loop. With no RSX it is synthesized
 * here: a host thread calls cellGcmTickVBlank/TickFlip, which invoke the
 * handlers the guest registered, and drains the GCM FIFO. Without this the
 * title initialises, registers its handlers and then waits forever.
 *
 * The ticks are driven off real elapsed time rather than off how long present()
 * took. A hidden or occluded window makes present block hard, and pacing the
 * ticks behind it paces the whole game behind it. */
extern "C" int rsx_live_draw_enabled(void);
extern "C" int rsx_live_draw_init(void*, uint32_t, uint32_t,
    const uint8_t* (*)(void*, uint32_t, uint32_t, uint32_t), void*);
extern "C" void rsx_live_draw_present(uint32_t);
extern "C" int rsx_null_backend_init(uint32_t, uint32_t, const char*);
extern "C" int rsx_null_backend_pump_messages(void);
extern "C" void* rsx_null_backend_get_hwnd(void);
extern "C" void rsx_null_backend_suppress_present(int);
extern "C" uint32_t cellGcmResolveLocated(int, uint32_t);
extern "C" uint32_t cellGcmResolveIO(uint32_t);
extern "C" uint32_t cellGcmGetCurrentDisplayBufferId(void);
static bool s_live_renderer = false;
static const uint8_t* live_guest_ptr(void*, uint32_t location, uint32_t offset, uint32_t bytes) {
    uint32_t ea = location == 0 ? cellGcmResolveLocated(1, offset) : cellGcmResolveIO(offset);
    if (!ea && location != 0) ea = cellGcmResolveLocated(0, offset);
    if (!ea || ea == 0xFFFFFFFFu || (uint64_t)ea + bytes > 0x100000000ull) return nullptr;
    return vm_base + ea;
}
static void present_renderer() {
    const ULONGLONG begin = GetTickCount64();
    if (s_live_renderer) rsx_live_draw_present(cellGcmGetCurrentDisplayBufferId());
    else rsx_backend_present();
    static ULONGLONG report = 0, spent = 0;
    static unsigned count = 0;
    const ULONGLONG end = GetTickCount64();
    if (!report) report = begin;
    spent += end-begin; ++count;
    if (end-report >= 5000) {
        fprintf(stderr, "[port-fps] %.1f presents/s, present_avg=%.2fms over %llums\n",
            count*1000.0/(end-report), count ? (double)spent/count : 0.0,
            (unsigned long long)(end-report));
        report=end; spent=0; count=0;
    }
}
static volatile LONG g_frames_presented = 0;

/* Frames handed to the backend at a guest FLIP boundary -- one per frame the
 * guest actually finished. The presents made before the guest's first flip, so
 * a fresh window is not left blank through a long boot, carry no guest frame
 * and are deliberately not counted. */
extern "C" unsigned ppu_boot_frames_presented(void)
{
    return (unsigned)g_frames_presented;
}

static void present_guest_frame(void)
{
    present_renderer();
    /* This thread increments and guest threads read, so interlocked rather
     * than a volatile ++, which on arm64 is neither atomic nor a fence. */
    InterlockedIncrement(&g_frames_presented);
}

// Hardware display timing must not run on the thread compiling shaders or
// waiting for the GPU. CRI uses these notifications to synchronize movie sound.
static DWORD WINAPI vblank_clock(LPVOID)
{
    LARGE_INTEGER freq, next; QueryPerformanceFrequency(&freq); QueryPerformanceCounter(&next);
    const long long period = freq.QuadPart * 1001 / 60000;
    unsigned count = 0; ULONGLONG t0 = GetTickCount64();
    long long max_late = 0;
    const bool log = getenv("IMBK_VBLANK_LOG") != nullptr;
    for (;;) {
        Sleep(1);
        LARGE_INTEGER now; QueryPerformanceCounter(&now);
        if (now.QuadPart - next.QuadPart > freq.QuadPart / 4) next = now;
        while (now.QuadPart >= next.QuadPart) {
            if (now.QuadPart - next.QuadPart > max_late) max_late = now.QuadPart - next.QuadPart;
            cellGcmTickVBlank(); cellGcmTickFlip();
            next.QuadPart += period;
            ++count;
        }
        if (log && GetTickCount64() - t0 >= 5000) {
            fprintf(stderr, "[vblank] %.2f ticks/s, max_late=%.2fms\n",
                count * 1000.0 / (GetTickCount64() - t0), max_late * 1000.0 / freq.QuadPart);
            count = 0; max_late = 0; t0 = GetTickCount64();
        }
    }
}

static DWORD WINAPI frame_clock(LPVOID)
{
    const char* title = getenv("PS3_TITLE");
    if (!title || !*title) title = "ps3recomp";

    int rsx_ok;
    if (rsx_live_draw_enabled()) {
        rsx_ok = rsx_null_backend_init(WINDOW_WIDTH, WINDOW_HEIGHT, title) == 0;
        if (rsx_ok) rsx_ok = rsx_live_draw_init(rsx_null_backend_get_hwnd(), WINDOW_WIDTH,
            WINDOW_HEIGHT, live_guest_ptr, nullptr) == 0;
        s_live_renderer = rsx_ok;
        if (rsx_ok) rsx_null_backend_suppress_present(1);
        fprintf(stderr, "[rsx] live renderer: %s\n", rsx_ok ? "ready" : "failed");
    } else rsx_ok = rsx_backend_init(WINDOW_WIDTH, WINDOW_HEIGHT, title) == 0;
    fprintf(stderr, "[rsx] %s backend init %s\n", RSX_BACKEND_NAME,
            rsx_ok ? "OK -- window open" : "FAILED");


    /* How long the clock sleeps between FIFO drains. The game's command-buffer
     * wrap (func_001DB3A0) spins on usleep(30) until the RSX `get` leaves the
     * segment it wants to reuse, so every drain it waits for costs it up to a
     * whole sleep: at 4 ms this was most of the main thread's time.
     * IMBK_CLOCK_MS overrides (0 = yield only). */
    static int clock_ms = -1;
    if (clock_ms < 0) { const char* e = getenv("IMBK_CLOCK_MS"); clock_ms = e ? atoi(e) : 1; }

    for (;;) {
        if (clock_ms > 0) Sleep((DWORD)clock_ms); else SwitchToThread();

        /* Drain at the outer cadence too, not only on the 16 ms tick: titles
         * fence every render pass on a label the drain writes, and one wave of
         * those at 16 ms apiece paces the guest into single-figure frame rates.
         * The real RSX writes them in microseconds. */
        if (rsx_ok) {
            if (cellGcm_take_flip_pending()) {
                present_guest_frame();
            }
            cellGcm_rsx_process_fifo();

            if ((s_live_renderer ? rsx_null_backend_pump_messages() : rsx_backend_pump()) != 0) {
                rsx_ok = 0;              /* window closed */
                continue;
            }
            /* Present on a guest flip. A present on a fixed clock can catch the
             * drain mid-frame and flash a partial one. Before the first flip
             * present freely, so the window is not blank during boot. */
            unsigned fc = cellGcm_flip_request_count();
            // Pending flips above are the sole presentation source. The request
            // count can change before its pending flag is published: presenting
            // from both used to display the same frame twice at uneven intervals.
            if (fc == 0) {
                present_renderer();
            }
        }
    }
    return 0;
}

/* ---------------------------------------------------------------------------
 * VFS root
 * -----------------------------------------------------------------------*/
/* Derive the directory holding PS3_GAME from the EBOOT path
 * <root>/PS3_GAME/USRDIR/EBOOT.elf -> <root>. $PS3_VFS_ROOT overrides. */
static char s_vfs_root[1024];

static void derive_vfs_root(const char* eboot)
{
    const char* env = getenv("PS3_VFS_ROOT");
    if (env && *env) { ppu_vfs_root = env; return; }

    strncpy(s_vfs_root, eboot, sizeof s_vfs_root - 1);
    for (char* p = s_vfs_root; *p; p++) if (*p == '\\') *p = '/';
    /* strip EBOOT.elf, USRDIR and PS3_GAME */
    for (int i = 0; i < 3; i++) { char* s = strrchr(s_vfs_root, '/'); if (s) *s = 0; }
    if (!s_vfs_root[0]) strcpy(s_vfs_root, ".");
    ppu_vfs_root = s_vfs_root;
}

/* ---------------------------------------------------------------------------
 * Guest VM
 * -----------------------------------------------------------------------*/
#ifdef _WIN32
/* Demand-paging for the flat VM: reserve the whole 4 GB guest space, which
 * costs no commit, and commit each 64 KB page on first access. Every 32-bit
 * guest offset is then valid, so a garbage guest pointer reads zero instead of
 * killing the process. Faults outside the arena fall through untouched. */
static LONG WINAPI vm_commit_veh(EXCEPTION_POINTERS* ep)
{
    if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) {
        ULONG_PTR fault = ep->ExceptionRecord->ExceptionInformation[1];
        uintptr_t base  = (uintptr_t)vm_base;
        if (vm_base && fault >= base && fault < base + VM_SIZE) {
            void* page = (void*)(fault & ~(uintptr_t)0xFFFF);
            if (VirtualAlloc(page, 0x10000, MEM_COMMIT, PAGE_READWRITE))
                return EXCEPTION_CONTINUE_EXECUTION;
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

static bool alloc_guest_vm(void)
{
#ifdef _WIN32
    AddVectoredExceptionHandler(1, vm_commit_veh);
    vm_base = (uint8_t*)VirtualAlloc(NULL, VM_SIZE, MEM_RESERVE, PAGE_READWRITE);
    ppu_vm_size = 0;              /* the whole space is backed; no OOB guard needed */
#else
    /* No vectored exception handlers off Windows, so the arena is committed up
     * front and lazily backed by the OS: only pages the title touches cost
     * anything. It stops below the 0xE0000000 mark, so ppu_vm_size arms the
     * loader's bounds check for what is left. */
    vm_base = (uint8_t*)calloc(1, 0xE0000000u);
    ppu_vm_size = 0xE0000000u;
#endif
    return vm_base != nullptr;
}

/* ---------------------------------------------------------------------------
 * Main-thread sampler (IMBK_SAMPLE=<report at s>[,<start at s>])
 * -----------------------------------------------------------------------*/
/* Where is the guest's main thread spending its time? Every 2 ms the host main
 * thread (which runs the guest's) is suspended and its host stack unwound; the
 * frames are printed as RVAs, which tools/resolve_samples.py names from
 * MyGameRecomp.map (lifted func_XXXXXXXX and runtime functions alike). */
#ifdef _WIN32
static HANDLE s_main_thread;

enum { SAMPLE_DEPTH = 32, SAMPLE_SLOTS = 8192 };
struct sample_stack { uint64_t rva[SAMPLE_DEPTH]; int n; uint32_t count; DWORD tid; };
static sample_stack s_samples[SAMPLE_SLOTS];

static int sample_frames(HANDLE th, uint64_t* out, uint64_t base)
{
    CONTEXT c;
    memset(&c, 0, sizeof c);
    c.ContextFlags = CONTEXT_FULL;
    if (!GetThreadContext(th, &c)) return 0;
    int n = 0;
    while (n < SAMPLE_DEPTH && c.Rip) {
        out[n++] = c.Rip - base;
        DWORD64 image = 0;
        PRUNTIME_FUNCTION fe = RtlLookupFunctionEntry(c.Rip, &image, NULL);
        if (!fe) { c.Rip = *(DWORD64*)c.Rsp; c.Rsp += 8; continue; }
        void* hd; DWORD64 frame;
        RtlVirtualUnwind(UNW_FLAG_NHANDLER, image, c.Rip, fe, &c, &hd, &frame, NULL);
    }
    return n;
}

static DWORD WINAPI guest_sampler(LPVOID)
{
    const char* e = getenv("IMBK_SAMPLE");
    double report = atof(e), start = 0;
    if (const char* comma = strchr(e, ',')) start = atof(comma + 1);
    uint64_t base = (uint64_t)GetModuleHandleA(NULL);

    Sleep((DWORD)(start * 1000));
    ULONGLONG until = GetTickCount64() + (ULONGLONG)((report - start) * 1000);
    uint32_t total = 0;
    /* IMBK_SAMPLE_ALL=1: every thread of the process (snapshot at start time),
     * each stack tagged T<host tid>. */
    enum { MAX_TH = 128 };
    HANDLE ths[MAX_TH]; DWORD tids[MAX_TH]; int nth = 0;
    if (getenv("IMBK_SAMPLE_ALL")) {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        THREADENTRY32 te; te.dwSize = sizeof te;
        for (BOOL ok = Thread32First(snap, &te); ok && nth < MAX_TH; ok = Thread32Next(snap, &te)) {
            if (te.th32OwnerProcessID != GetCurrentProcessId() || te.th32ThreadID == GetCurrentThreadId()) continue;
            HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION,
                                  FALSE, te.th32ThreadID);
            if (h) { ths[nth] = h; tids[nth] = te.th32ThreadID; nth++; }
        }
        CloseHandle(snap);
    } else { ths[0] = s_main_thread; tids[0] = 0; nth = 1; }

    while (GetTickCount64() < until) {
        Sleep(2);
        for (int t = 0; t < nth; t++) {
            uint64_t f[SAMPLE_DEPTH];
            if (SuspendThread(ths[t]) == (DWORD)-1) continue;
            int n = sample_frames(ths[t], f, base);
            ResumeThread(ths[t]);
            if (!n) continue;
            uint64_t h = 1469598103934665603ull ^ tids[t];
            for (int i = 0; i < n; i++) h = (h ^ f[i]) * 1099511628211ull;
            for (uint32_t k = 0; k < SAMPLE_SLOTS; k++) {
                sample_stack& s = s_samples[(h + k) % SAMPLE_SLOTS];
                if (s.n == 0) { memcpy(s.rva, f, sizeof f); s.n = n; s.tid = tids[t]; }
                if (s.n == n && s.tid == tids[t] && !memcmp(s.rva, f, n * sizeof f[0])) { s.count++; break; }
            }
        }
        total++;
    }

    fprintf(stderr, "[sample] %u samples of the main thread, top stacks (innermost first):\n", total);
    for (int rank = 0; rank < (nth > 1 ? 4000 : 25); rank++) {
        sample_stack* best = NULL;
        for (uint32_t k = 0; k < SAMPLE_SLOTS; k++)
            if (s_samples[k].count && (!best || s_samples[k].count > best->count)) best = &s_samples[k];
        if (!best) break;
        char line[1024];
        int p = snprintf(line, sizeof line, "[sample] %5.1f%% T%lu", 100.0 * best->count / (total ? total : 1),
                         (unsigned long)best->tid);
        for (int i = 0; i < best->n && p < (int)sizeof line - 32; i++) {
            /* raw RVAs: tools/resolve_samples.py names them from the map */
            p += snprintf(line + p, sizeof line - p, " %llX", (unsigned long long)best->rva[i]);
        }
        fprintf(stderr, "%s\n", line);
        best->count = 0;
    }
    fflush(stderr);
    return 0;
}
#endif

/* ---------------------------------------------------------------------------
 * Main
 * -----------------------------------------------------------------------*/
static void harness_guest_caller(uint32_t opd, uint64_t a0, uint64_t a1,
                                 uint64_t a2, uint64_t a3, uint64_t a4,
                                 uint64_t a5, uint64_t a6, uint64_t a7)
{
    ppu_guest_call(opd, a0, a1, a2, a3, a4, a5, a6, a7);
}

extern "C" void imbk_register_font_bridge();

int main(int argc, char** argv)
{
    if (argc < 2) {
        printf("usage: %s <PPU ELF>\n", argv[0]);
        return 2;
    }

#ifdef _WIN32
#pragma comment(lib, "winmm.lib")
    /* 1 ms timer resolution. The default granularity is about 15.6 ms, which
     * inflates every shorter wait the title makes and throttles the whole
     * thing. POSIX timers are already fine-grained. */
    // Imabikiso's offscreen/RESC path requires the live RSX renderer.
    // An explicit RSX_LIVE_DRAW=0 keeps the legacy path available for comparisons.
    if (!getenv("RSX_LIVE_DRAW")) _putenv_s("RSX_LIVE_DRAW", "1");
    if (!getenv("GCM_FLIPCB_ONTICK")) _putenv_s("GCM_FLIPCB_ONTICK", "1");
    timeBeginPeriod(1);
    setvbuf(stdout, NULL, _IONBF, 0);   /* unbuffered: do not lose prints on a kill */
#endif

    printf("=== ps3recomp game runner ===\n");

    if (!alloc_guest_vm()) {
        fprintf(stderr, "ERROR: could not allocate the guest address space\n");
        return 1;
    }

    uint32_t entry = ppu_load_elf(argv[1]);
    if (!entry) {
        fprintf(stderr, "ERROR: could not load %s\n", argv[1]);
        return 1;
    }

    derive_vfs_root(argv[1]);
    printf("[boot] VFS root: %s\n", ppu_vfs_root);

    ppu_recomp_register();   /* lifted function table -> address map */
    ps3_load_prx_modules();  /* this game's lifted system PRX, if it has any */
    ppu_hle_init();          /* firmware import NID -> HLE handlers */
    ppu_sysprx_register();   /* boot-critical CRT */
    ppu_fs_register();       /* cellFs over the game directory */
    imbk_register_font_bridge();
    lv2_init_syscalls();     /* the lv2 syscall table */

    /* Install the guest-callback hook, then start the frame clock. It no-ops
     * until the title registers its vblank and flip handlers during init. */
    g_ps3_guest_caller = harness_guest_caller;
    HANDLE vblank_thread = CreateThread(NULL, 1u * 1024 * 1024, vblank_clock, NULL, 0, NULL);
    if (!vblank_thread) { fprintf(stderr, "[boot] display clock creation failed\n"); return 1; }
    CloseHandle(vblank_thread);
    HANDLE clock_thread = CreateThread(NULL, 4u * 1024 * 1024, frame_clock, NULL, 0, NULL);
#ifdef _WIN32
    if (getenv("IMBK_SAMPLE")) {
        /* IMBK_SAMPLE_CLOCK=1: sample the frame clock (FIFO drain + renderer)
         * instead of the guest's main thread. */
        if (getenv("IMBK_SAMPLE_CLOCK")) s_main_thread = clock_thread;
        else DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(),
                             &s_main_thread, 0, FALSE, DUPLICATE_SAME_ACCESS);
        CreateThread(NULL, 1u * 1024 * 1024, guest_sampler, NULL, 0, NULL);
    }
#endif

    /* The main thread's stack goes where lv2 puts it, in the 0xD0000000 stack
     * region, not at STACK_TOP. The game can tell: its SPU copy routine
     * (0x00178E20) does the copy on the PPU when an address shares the stack's
     * top nibble, and then waits for a 0xDEADBEEF reply that only the raw SPU
     * path sends. With the stack at 0x0FF00000 every low address "is on the
     * stack" and the boot hangs (ps3recomp's boot harness has the same fix,
     * opt-in, as PS3_MAIN_STACK_LV2). */
    uint32_t stack_top = STACK_TOP;
    if (uint32_t base = vm_stack_allocate(&g_vm_stack_alloc, VM_PPU_STACK_SIZE))
        stack_top = base + VM_PPU_STACK_SIZE;

    printf("\n[boot] dispatching entry OPD 0x%08X (stack top 0x%08X)\n\n",
           entry, stack_top);

    int rc = ppu_run(entry, stack_top);
    printf("\n[boot] ppu_run returned %d (entry function unwound)\n", rc);

    /* A title that called sys_process_exit never reaches this line: that path
     * ends in the host exit(). Getting here means the entry function returned
     * instead, so hand back the status the guest published if it published
     * one. Returning a hardcoded 0 would report success for a run that never
     * got anywhere. */
    return g_sys_process_exit_called ? (int)g_sys_process_exit_code : 0;
}
