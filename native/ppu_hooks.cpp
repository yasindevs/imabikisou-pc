/* Host hooks around lifted PPU functions (wrappers inserted by
 * tools/apply_ppu_hooks.py). imbk_hook_enter returns nonzero when the hook
 * performed the call itself and the original must not run.
 *
 * Sofdec (CRI MWSFD 4.46) entry points, found through their error strings:
 *   0x002BAE80 mwPlyGetCurFrm(hn, frmobj)      0x002BA848 mwPlyRelCurFrm(hn)
 *   0x002BB090 mwPlyGetFrm(hn, frmobj)         0x002B5470 mwPlyFxReqCnvFrmRgba_PS3SPU
 *   0x002BCC88 mwPlyStartAfs(hn, pid, fid)     0x002C17D0 mwPlyStartAfsLp
 *   0x002C15B0 mwPlyStartSeamless              0x002BC9B0 mwPlyStartFnameRange
 *   0x002C12B8 mwPlyEntryAfs(hn, pid, fid)     0x002BC388 mwSfdStop(hn)
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ppu_recomp.h"

extern "C" uint8_t* vm_base;
extern "C" uint64_t ps3_system_time_us(void);

static int hook_log_level(void)
{
    static int v = -1;
    if (v < 0) { const char* e = getenv("IMBK_HOOK_LOG"); v = e ? atoi(e) : 0; if (e && !v) v = 1; }
    return v;
}

static uint32_t be32(uint32_t ea)
{
    const uint8_t* p = vm_base + ea;
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static const char* hook_name(uint32_t id)
{
    switch (id) {
    case 0x002BAE80u: return "mwPlyGetCurFrm";
    case 0x002BB090u: return "mwPlyGetFrm";
    case 0x002BA848u: return "mwPlyRelCurFrm";
    case 0x002BCC88u: return "mwPlyStartAfs";
    case 0x002C17D0u: return "mwPlyStartAfsLp";
    case 0x002C15B0u: return "mwPlyStartSeamless";
    case 0x002BC9B0u: return "mwPlyStartFnameRange";
    case 0x002C12B8u: return "mwPlyEntryAfs";
    case 0x002BC388u: return "mwSfdStop";
    case 0x002B5470u: return "mwPlyFxReqCnvFrmRgba";
    default:          return "?";
    }
}

extern "C" void imbk_movie_install(void);
extern "C" void imbk_movie_on_start(uint32_t hn, uint32_t pid, uint32_t fid);
extern "C" void imbk_movie_on_stop(uint32_t hn);
extern "C" int  imbk_movie_on_convert(uint32_t hn, uint32_t cnvobj);

extern "C" int imbk_hook_enter(uint32_t id, ppu_context* ctx, uint64_t* save)
{
    for (int i = 0; i < 6; i++) save[i] = ctx->gpr[3 + i];
    { static bool inst = false; if (!inst) { inst = true; imbk_movie_install(); } }
    switch (id) {
    case 0x002BCC88u: case 0x002C17D0u: case 0x002C12B8u:   /* StartAfs, StartAfsLp, EntryAfs */
        imbk_movie_on_start((uint32_t)ctx->gpr[3], (uint32_t)ctx->gpr[4], (uint32_t)ctx->gpr[5]);
        break;
    case 0x002BC388u:                                        /* mwSfdStop */
        imbk_movie_on_stop((uint32_t)ctx->gpr[3]);
        break;
    case 0x002B5470u:                                        /* ReqCnvFrmRgba */
        imbk_movie_on_convert((uint32_t)ctx->gpr[3], (uint32_t)ctx->gpr[4]);
        break;
    }
    const int lv = hook_log_level();
    if (lv >= 3 && id == 0x002B5470u) {
        static int n = 0;
        const uint32_t ob = (uint32_t)ctx->gpr[4];
        if (n++ < 6 && ob >= 0x10000000u && ob < 0x20000000u) {
            fprintf(stderr, "[sofdec]   cnvobj@0x%08X:", ob);
            for (uint32_t k = 0; k < 48; k++) fprintf(stderr, "%s%08X", (k % 8) ? " " : " | ", be32(ob + k * 4));
            fprintf(stderr, "\n");
        }
    }
    if (lv && id != 0x002BAE80u && id != 0x002BB090u && id != 0x002BA848u) {
        fprintf(stderr, "[sofdec] t=%llums %s(r3=0x%08X r4=0x%08X r5=0x%08X r6=0x%08X r7=0x%08X) lr=0x%08X\n",
                (unsigned long long)(ps3_system_time_us() / 1000), hook_name(id),
                (uint32_t)ctx->gpr[3], (uint32_t)ctx->gpr[4], (uint32_t)ctx->gpr[5],
                (uint32_t)ctx->gpr[6], (uint32_t)ctx->gpr[7], (uint32_t)ctx->lr);
    }
    return 0;
}

#ifdef _WIN32
#include <windows.h>
/* IMBK_FRM_DUMP=<frmid>: when mwPlyGetFrm returns that frame id, write
 * 1280*720*3/2 bytes from each pointer field of the frame object to
 * frm_<field>_<ea>.bin in the working directory (committed memory only). */
static void frm_dump(uint32_t fo)
{
    static int done = 0;
    if (done) return;
    const char* e = getenv("IMBK_FRM_DUMP");
    if (!e || be32(fo) != (uint32_t)atoi(e)) return;
    done = 1;
    for (uint32_t off = 0x28; off < 0xA0; off += 4) {
        const uint32_t p = be32(fo + off);
        if (p < 0x10000000u || p >= 0x20000000u) continue;
        uint32_t len = 1280u * 720u * 3u / 2u, ok = 0;
        for (uint32_t a = p & ~0xFFFFu; a < p + len; a += 0x10000u) {
            MEMORY_BASIC_INFORMATION mbi;
            if (!VirtualQuery(vm_base + a, &mbi, sizeof mbi) || mbi.State != MEM_COMMIT) break;
            ok = a + 0x10000u - p;
        }
        if (ok < len) len = ok;
        char fn[96]; snprintf(fn, sizeof fn, "frm_%02X_%08X.bin", off, p);
        FILE* f = fopen(fn, "wb");
        if (f) { fwrite(vm_base + p, 1, len, f); fclose(f); }
        fprintf(stderr, "[sofdec] dumped frame field +0x%02X ea=0x%08X %u bytes\n", off, p, len);
    }
}
#else
static void frm_dump(uint32_t) {}
#endif

extern "C" void imbk_hook_leave(uint32_t id, ppu_context* ctx, uint64_t* save)
{
    if ((id == 0x002BAE80u || id == 0x002BB090u) && save[1]) frm_dump((uint32_t)save[1]);
    const int lv = hook_log_level();
    if (!lv) return;
    if (id == 0x002BAE80u || id == 0x002BB090u) {
        /* frame object: 0xA0 bytes at r4 (the original memsets it on error) */
        static int n = 0;
        const uint32_t fo = (uint32_t)save[1];
        if (n < lv * 40 && fo) {
            n++;
            fprintf(stderr, "[sofdec] t=%llums %s(hn=0x%08X) frmobj@0x%08X:",
                    (unsigned long long)(ps3_system_time_us() / 1000), hook_name(id),
                    (uint32_t)save[0], fo);
            for (uint32_t o = 0; o < 0xA0; o += 4) fprintf(stderr, "%s%08X", (o % 32) ? " " : " | ", be32(fo + o));
            fprintf(stderr, "\n");
            /* IMBK_HOOK_LOG>=3: one level down -- 24 words behind each pointer field. */
            if (lv >= 3) for (uint32_t o = 0x28; o < 0x50; o += 4) {
                const uint32_t p = be32(fo + o);
                if (p < 0x10000000u || p >= 0x20000000u) continue;
                fprintf(stderr, "[sofdec]    +0x%02X -> 0x%08X:", o, p);
                for (uint32_t k = 0; k < 24; k++) fprintf(stderr, " %08X", be32(p + k * 4));
                fprintf(stderr, "\n");
            }
        }
    }
    (void)ctx;
}
