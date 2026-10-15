"""Let Sofdec's SPU picture decoder skip pixel work while movies are decoded natively.

usage: python apply_spu_movie_skip.py <private SPU output>/spu_0004_at_00431580/spu_recomp.c

The native port decodes every movie on the PC and writes each frame into the
YUV buffer Sofdec converts, so the SPU decoder's own pixels are thrown away.
Its parsing and bookkeeping (bitstream consumption, picture headers, time
stamps, frame-pool state) must still run, or Sofdec stops presenting frames.
This inserts, at the entry of the decoder's motion-compensation, intra
packing, reference-fetch and macroblock-output functions, an early return
taken while `g_imbk_spu_pixel_skip` is set (movie_bridge.cpp sets it while
every playing movie is native). The IDCT leaves are reached through indirect
branches and are skipped from movie_bridge.cpp through g_spu_ib_skip.

Idempotent: rerun after every SPU regeneration.
"""
import sys

# LS address -> what the early return removes
TARGETS = {
    '000169C8': 'reference list build + 280-byte GETL fetches',
    '00015298': 'single-reference motion compensation + output',
    '000174C8': 'two-reference motion compensation + output',
    '00016EF8': 'intra pixel packing + output',
    '00015100': 'reconstructed macroblock PUTL (list at LS 0x2F000)',
}
MARK = '/* imbk: native movie pixels */'

path = sys.argv[1]
src = open(path, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in src else '\n'
prefix = 'void spu_0004_at_00431580_spu_func_'
changed = 0
if 'g_imbk_spu_pixel_skip' not in src.split(prefix, 1)[0]:
    decl = 'extern volatile int g_imbk_spu_pixel_skip;  ' + MARK + nl
    first = src.index(prefix)
    src = src[:first] + decl + nl + src[first:]
    changed += 1
for addr, what in TARGETS.items():
    head = prefix + addr + '(spu_context* ctx) {'
    i = src.find(head)
    if i < 0:
        sys.exit('missing function ' + addr + ' (different SPU image layout?)')
    j = i + len(head)
    line = nl + '        if (g_imbk_spu_pixel_skip) SPU_RET(ctx);  /* imbk: native movie pixels: ' + what + ' */'
    if src[j:j + len(line)] == line:
        continue
    src = src[:j] + line + src[j:]
    changed += 1
open(path, 'w', encoding='utf-8', newline='').write(src)
print(f'{path}: {changed} insertion(s)' if changed else f'{path}: already applied')
