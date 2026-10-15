// Imabikiso's cellFont imports use the PS3 ABI; the runtime's convenience
// functions use different argument order and renderer handles. Keep the
// translation here instead of editing generated EBOOT code.
#define NOMINMAX
#include "ppu_recomp.h"
#include "vm.h"
#include "libs/font/cellFont.h"
#include <stdio.h>
#include "libs/input/cellPad.h"
#include <algorithm>
#include <cstring>
#include <vector>
#include <cmath>
extern "C" vm_stack_alloc g_vm_stack_alloc;
extern "C" void ps3_hle_register_ctx(uint32_t, const char*, void (*)(ppu_context*));
extern "C" s32 cellFontOpenFontInstance(CellFont*, CellFont*);
static uint32_t library_ea;
template<class T> static T* ptr(uint64_t a) { return reinterpret_cast<T*>((uintptr_t)(uint32_t)a); }
static void result(ppu_context* c, s32 rc) { c->gpr[3] = (uint64_t)(int64_t)rc; }
static void initialize(ppu_context* c) {
    result(c, cellFontInit(ptr<CellFontConfig>(c->gpr[4])));
}
static void library_init(ppu_context* c) {
    uint32_t out = (uint32_t)c->gpr[5];
    if (!out || !c->gpr[4]) { result(c, CELL_FONT_ERROR_INVALID_PARAMETER); return; }
    if (!library_ea) library_ea = vm_stack_allocate(&g_vm_stack_alloc, VM_PAGE_SIZE);
    if (!library_ea) { result(c, CELL_FONT_ERROR_ALLOCATION_FAILED); return; }
    vm_write32(library_ea, 0); vm_write32(library_ea + 4, 0); vm_write32(library_ea + 8, 0);
    vm_write32(out, library_ea);
    fprintf(stderr, "[font-abi] library output=0x%08X -> 0x%08X\n", out, library_ea);
    result(c, CELL_OK);
}
static void library_end(ppu_context* c) { library_ea = 0; result(c, CELL_OK); }
static void open_memory(ppu_context* c) {
    uint32_t out = (uint32_t)c->gpr[8];
    s32 rc = cellFontOpenFontMemory(ptr<CellFont>(out), ptr<void>(c->gpr[4]),
        (u32)c->gpr[5], (u32)c->gpr[6], (s32)c->gpr[7]);
    fprintf(stderr, "[font-abi] open font=0x%08X size=%u sub=%u -> 0x%08X\n",
        out, (u32)c->gpr[5], (u32)c->gpr[6], (u32)rc);
    result(c, rc);
}
static void instance(ppu_context* c) {
    result(c, cellFontOpenFontInstance(ptr<CellFont>(c->gpr[3]), ptr<CellFont>(c->gpr[4])));
}
static void create_renderer(ppu_context* c) {
    uint32_t out = (uint32_t)c->gpr[5];
    s32 rc = cellFontCreateRenderer((u32)c->gpr[3], ptr<CellFontRenderer>(out));
    fprintf(stderr, "[font-abi] renderer=0x%08X -> 0x%08X\n", out, (u32)rc);
    result(c, rc);
}
static void bind_renderer(ppu_context* c) {
    if (!c->gpr[4]) { result(c, CELL_FONT_ERROR_INVALID_PARAMETER); return; }
    result(c, cellFontBindRenderer(ptr<CellFont>(c->gpr[3]), vm_read32(c->gpr[4])));
}
static void destroy_renderer(ppu_context* c) {
    if (!c->gpr[3]) { result(c, CELL_FONT_ERROR_INVALID_PARAMETER); return; }
    result(c, cellFontDestroyRenderer(vm_read32(c->gpr[3])));
}
static void scale(ppu_context* c) {
    result(c, cellFontSetScalePixel(ptr<CellFont>(c->gpr[3]), (float)c->fpr[1], (float)c->fpr[2]));
}
static void render_horizontal(ppu_context* c) {
    // Floating arguments occupy FPR1/2 AND slots r6/r7. The two following
    // output pointers therefore arrive in r8/r9, not r6/r7.
    u32 surf = (u32)c->gpr[5], metrics = (u32)c->gpr[8], transfer = (u32)c->gpr[9];
    const u32 game_surf = surf;
    // The glyph image goes to a private guest buffer, never the title's
    // surface: Imabikiso hands libfont the start of its own text atlas as the
    // surface (object+0x524 aliases object+0x400) and copies the glyph from
    // TransInfo.Image into that same atlas. Rendering into the surface wrote
    // over glyphs already packed there -- striped letters from the second
    // atlas row on. The private surface keeps the title's size and scissor.
    // FONT_GAME_SURFACE=1 restores the old behaviour.
    static int own = -1;
    if (own < 0) own = getenv("FONT_GAME_SURFACE") ? 0 : 1;
    if (own && transfer && surf) {
        static u32 priv = 0, priv_bytes = 0;
        const u32 w = vm_read32(surf+12), h = vm_read32(surf+16), px = std::max(1u, vm_read32(surf+8));
        const u32 need = 64 + w * h * px;
        if (w && h && w <= 4096 && h <= 4096 && px <= 4) {
            if (need > priv_bytes) {
                priv = vm_stack_allocate(&g_vm_stack_alloc, need);
                priv_bytes = priv ? need : 0;
                fprintf(stderr, "[font-abi] private glyph surface %ux%u -> 0x%08X\n", w, h, priv);
            }
            if (priv) {
                for (u32 i = 0; i < 9; ++i) vm_write32(priv + i * 4, vm_read32(surf + i * 4));
                vm_write32(priv, priv + 64);
                vm_write32(priv + 4, w * px);
                surf = priv;
            }
        }
    }
    s32 rc = cellFontRenderCharGlyphImage(ptr<CellFont>(c->gpr[3]), (u32)c->gpr[4],
        ptr<CellFontRenderSurface>(surf), (float)c->fpr[1], (float)c->fpr[2],
        ptr<CellFontGlyphMetrics>(metrics), nullptr);
    if (rc == CELL_OK && transfer) {
        // CellFontImageTransInfo is six words, unlike the runtime's
        // five-word GlyphImage. The image was rendered into `surf`.
        auto read_float = [](u32 a) { u32 bits=vm_read32(a); float f; std::memcpy(&f,&bits,4); return f; };
        u32 pitch = vm_read32(surf+4), pixel = vm_read32(surf+8);
        int sx = (int)vm_read32(surf+12), sy = (int)vm_read32(surf+16);
        int dx = (int)((float)c->fpr[1] + (metrics ? read_float(metrics+8) : 0));
        int dy = (int)((float)c->fpr[2] - (metrics ? read_float(metrics+12) : 0));
        int width = metrics ? (int)std::ceil(read_float(metrics)) : 0;
        int height = metrics ? (int)std::ceil(read_float(metrics+4)) : 0;
        int x0=std::clamp(dx,0,std::max(sx,0)), y0=std::clamp(dy,0,std::max(sy,0));
        width=std::max(0,std::min(dx+width,sx)-x0);
        height=std::max(0,std::min(dy+height,sy)-y0);
        u32 pixels=vm_read32(surf)+y0*pitch+x0*pixel;
        vm_write32(transfer, pixels);
        vm_write32(transfer+4, pitch);
        vm_write32(transfer+8, width);
        vm_write32(transfer+12, height);
        vm_write32(transfer+16, pixels);
        vm_write32(transfer+20, pitch);
        static int xl = -1, xn = 0;
        if (xl < 0) { const char* e = getenv("FONT_XFER_LOG"); xl = e ? atoi(e) : 0; }
        if (xn < xl) { xn++;
            fprintf(stderr, "[font-xfer] tid=%lu U+%04X surf=0x%08X pen=(%.1f,%.1f) box=(%d,%d) %dx%d bearY=%.2f h=%.2f\n",
                    GetCurrentThreadId(), (u32)c->gpr[4], vm_read32(game_surf), (float)c->fpr[1], (float)c->fpr[2], x0, y0, width, height,
                    metrics ? read_float(metrics+12) : 0.f, metrics ? read_float(metrics+4) : 0.f); }
    }
    static unsigned logs;
    if (logs++ < 4) fprintf(stderr, "[font-abi] render U+%04X -> 0x%08X\n", (u32)c->gpr[4], (u32)rc);
    result(c, rc);
}
static void surface(ppu_context* c) {
    u32 a = (u32)c->gpr[3];
    if (!a) return;
    for (unsigned i = 0; i < 5; ++i) vm_write32(a + i * 4, (u32)c->gpr[4 + i]);
    vm_write32(a+20, 0); vm_write32(a+24, 0);
    vm_write32(a+28, (u32)c->gpr[7]); vm_write32(a+32, (u32)c->gpr[8]);
}
static void scissor(ppu_context* c) {
    u32 a = (u32)c->gpr[3]; if (!a) return;
    s32 w = (s32)vm_read32(a+12), h = (s32)vm_read32(a+16);
    s32 x = (s32)c->gpr[4], y = (s32)c->gpr[5];
    vm_write32(a+20, std::clamp(x, 0, std::max(w, 0)));
    vm_write32(a+24, std::clamp(y, 0, std::max(h, 0)));
    vm_write32(a+28, (u32)std::clamp<int64_t>((int64_t)x+(u32)c->gpr[6], 0, std::max(w,0)));
    vm_write32(a+32, (u32)std::clamp<int64_t>((int64_t)y+(u32)c->gpr[7], 0, std::max(h,0)));
}
// PS3 lengths and UTF-16 output are big-endian guest values. These imports
// are absent from the generated table; returning success left all UI strings empty.
static void convert_text(ppu_context* c, UINT codepage, bool utf8_output) {
    u32 src = (u32)c->gpr[3], src_len = (u32)c->gpr[4];
    u32 dst = (u32)c->gpr[5], dst_len = (u32)c->gpr[6];
    if (!src_len || !dst_len) { result(c, 1); return; }
    u32 n = vm_read32(src_len), capacity = dst ? vm_read32(dst_len) : 0;
    u32 read = 0, written = 0;
    s32 rc = 0;
    while (read < n) {
        unsigned bytes = 1;
        u8 first = vm_read8(src+read);
        if (codepage == 932) bytes = IsDBCSLeadByteEx(932, first) ? 2 : 1;
        else if (first >= 0xF0) bytes = 4;
        else if (first >= 0xE0) bytes = 3;
        else if (first >= 0xC0) bytes = 2;
        if (bytes > n-read) { rc = 1; break; }
        char encoded[4]; for (unsigned i=0; i<bytes; ++i) encoded[i] = (char)vm_read8(src+read+i);
        wchar_t wide[2];
        int units = MultiByteToWideChar(codepage, MB_ERR_INVALID_CHARS, encoded, bytes, wide, 2);
        if (!units) { rc=1; break; }
        char out[8];
        int count = utf8_output ? WideCharToMultiByte(CP_UTF8, 0, wide, units, out, sizeof out, nullptr, nullptr) : units;
        if (!count) { rc=1; break; }
        if (dst && (u32)count > capacity-written) { rc=2; break; }
        if (dst) for (int i=0; i<count; ++i) {
            if (utf8_output) vm_write8(dst+written+i, (u8)out[i]);
            else vm_write16(dst+2*(written+i), (u16)wide[i]);
        }
        written += count; read += bytes;
    }
    if (rc) vm_write32(src_len, n-read);
    vm_write32(dst_len, written);
    static unsigned logs;
    if (logs++ < 4) fprintf(stderr, "[text-abi] cp%u %u bytes -> %u %s rc=%d\n",
        codepage,n,written,utf8_output ? "bytes" : "UTF16 units",rc);
    result(c,rc);
}
static void sjis_ucs2(ppu_context* c) { convert_text(c,932,false); }
static void sjis_utf8(ppu_context* c) { convert_text(c,932,true); }
static void utf8_utf16(ppu_context* c) { convert_text(c,CP_UTF8,false); }
extern "C" s32 cellPadGetInfo(CellPadInfo2*);
extern "C" void ppu_dump_guest_stack(ppu_context*, const char*);
static void pad_info_trace(ppu_context* c) {
    static unsigned n;
    unsigned obj=(unsigned)c->gpr[30];
    if (++n < 12 || n%120==0) fprintf(stderr,"[pad-abi] info n=%u obj=%08x active=%u lr=%08x\n",n,obj,vm_read8(obj+0xA3C),(unsigned)c->lr);
    /* IMBK_PAD_STACK=1: who calls the pad update (a virtual method, vtable 0x3A4020). */
    if (n == 1 && getenv("IMBK_PAD_STACK")) ppu_dump_guest_stack(c, "pad-info");
    result(c,cellPadGetInfo(ptr<CellPadInfo2>(c->gpr[3])));
}
static void pad_data_trace(ppu_context* c) {
    static unsigned n; unsigned out=(unsigned)c->gpr[4];
    int rc=cellPadGetData((unsigned)c->gpr[3],ptr<CellPadData>(out));
    if (++n<12 || n%120==0) fprintf(stderr,"[pad-abi] data n=%u rc=%x len=%u buttons=%04x/%04x\n",n,rc,vm_read32(out),vm_read16(out+8),vm_read16(out+10));
    result(c,rc);
}
// RESC uses GCM's display-completion interrupt. Firing at submission time
// releases the game's vsync semaphore before its frame has been displayed.
extern "C" void cellGcmSetFlipHandler(void (*handler)(u32));
static void resc_display_handler(ppu_context* c) {
    cellGcmSetFlipHandler(reinterpret_cast<void (*)(u32)>((uintptr_t)(u32)c->gpr[3]));
    result(c, 0);
}
extern "C" void imbk_register_font_bridge() {
    ps3_hle_register_ctx(0x2EA94661, "cellRescSetFlipHandler", resc_display_handler);
    ps3_hle_register_ctx(0x3AAAD464,"cellPadGetInfo",pad_info_trace);
    ps3_hle_register_ctx(0x8B72CDA1,"cellPadGetData",pad_data_trace);
    ps3_hle_register_ctx(0xF03DCC29, "cellFontInitializeWithRevision", initialize);
    ps3_hle_register_ctx(0x7A0A83C4, "cellFontInitLibraryFreeTypeWithRevision", library_init);
    ps3_hle_register_ctx(0x40D40544, "cellFontEndLibrary", library_end);
    ps3_hle_register_ctx(0x9E19072B, "cellFontOpenFontMemory", open_memory);
    ps3_hle_register_ctx(0x29329541, "cellFontOpenFontInstance", instance);
    ps3_hle_register_ctx(0x042E74E3, "cellFontCreateRenderer", create_renderer);
    ps3_hle_register_ctx(0x66A23100, "cellFontBindRenderer", bind_renderer);
    ps3_hle_register_ctx(0x21EBB248, "cellFontDestroyRenderer", destroy_renderer);
    ps3_hle_register_ctx(0x297F0E93, "cellFontSetScalePixel", scale);
    ps3_hle_register_ctx(0x227E1E3C, "cellFontSetupRenderScalePixel", scale);
    ps3_hle_register_ctx(0x90B9465E, "cellFontRenderSurfaceInit", surface);
    ps3_hle_register_ctx(0xB422B005, "cellFontRenderSurfaceSetScissor", scissor);
    ps3_hle_register_ctx(0x1A218FE4, "cellFontRenderCharGlyphImageHorizontal", render_horizontal);
    ps3_hle_register_ctx(0x88BE4799, "cellFontRenderCharGlyphImage", render_horizontal);
    ps3_hle_register_ctx(0xF9B1896D, "SJISstoUCS2s", sjis_ucs2);
    ps3_hle_register_ctx(0x33435818, "SJISstoUTF8s", sjis_utf8);
    ps3_hle_register_ctx(0xF7681B9A, "UTF8stoUTF16s", utf8_utf16);
}
