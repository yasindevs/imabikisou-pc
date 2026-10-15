"""Wrap selected lifted PPU functions with host hooks (idempotent).

For each address A in HOOKS, the generated definition
    void func_A(ppu_context* ctx) {
becomes
    void func_A__orig(ppu_context* ctx) {
and a wrapper with the original name is appended to the same .cpp:
    void func_A(ppu_context* ctx) {
        uint64_t s[8] = {0};
        if (imbk_hook_enter(0xA, ctx, s)) return;   // nonzero: hook replaced the call
        func_A__orig(ctx);
        imbk_hook_leave(0xA, ctx, s);
    }
The hook bodies live in port/ppu_hooks.cpp, so changing them never rebuilds the
huge generated translation units. Rerun after regenerating recomp/.

usage: python tools/apply_ppu_hooks.py [recomp_dir]
"""
import re
import sys
from pathlib import Path

HOOKS = {
    0x002BAE80: "mwPlyGetCurFrm",
    0x002BB090: "mwPlyGetFrm",
    0x002BA848: "mwPlyRelCurFrm",
    0x002BCC88: "mwPlyStartAfs",
    0x002C17D0: "mwPlyStartAfsLp",
    0x002C15B0: "mwPlyStartSeamless",
    0x002BC9B0: "mwPlyStartFnameRange",
    0x002C12B8: "mwPlyEntryAfs",
    0x002BC388: "mwSfdStop",
    0x002B5470: "mwPlyFxReqCnvFrmRgba_PS3SPU",
}

MARK = "/* imbk ppu hooks */"

def main():
    recomp = Path(sys.argv[1] if len(sys.argv) > 1 else "recomp")
    for cpp in sorted(recomp.glob("ppu_recomp_*.cpp")):
        src = cpp.read_text(encoding="utf-8", errors="surrogateescape")
        changed = False
        wrappers = []
        for addr, name in HOOKS.items():
            fn = "func_%08X" % addr
            head = "void %s(ppu_context* ctx) {" % fn
            if head not in src:
                continue
            body_start = src.index(head)
            body_end = src.index("\n}\n", body_start)
            if "g_trampoline_fn" in src[body_start:body_end]:
                print("warning: %s (%s) tail-calls through the trampoline; leave hook runs early" % (fn, name))
            src = src.replace(head, "void %s__orig(ppu_context* ctx) {" % fn, 1)
            wrappers.append(
                "void %s(ppu_context* ctx) { /* %s */\n"
                "    uint64_t s[8] = {0};\n"
                "    if (imbk_hook_enter(0x%08Xu, ctx, s)) return;\n"
                "    %s__orig(ctx);\n"
                "    imbk_hook_leave(0x%08Xu, ctx, s);\n"
                "}\n" % (fn, name, addr, fn, addr))
            changed = True
            print("hooked %s %s in %s" % (fn, name, cpp.name))
        if changed:
            if MARK not in src:
                src += ("\n%s\nextern \"C\" int  imbk_hook_enter(uint32_t id, ppu_context* ctx, uint64_t* save);\n"
                        "extern \"C\" void imbk_hook_leave(uint32_t id, ppu_context* ctx, uint64_t* save);\n" % MARK)
            src += "".join(wrappers)
            cpp.write_text(src, encoding="utf-8", errors="surrogateescape")

if __name__ == "__main__":
    main()
