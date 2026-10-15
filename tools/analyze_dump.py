"""Summarise a crash/hang minidump written by the native port.

usage: python analyze_dump.py <file.dmp> <MyGameRecomp.map> [--all]

For every thread: its instruction pointer and the return addresses found on
its stack that fall inside ImabikisoPC.exe, named from the linker map (the map
must come from the same build). Recompiled guest functions show as their PPU
address (func_XXXXXXXX). The stack scan is heuristic: stale return addresses
can appear, the first few entries are the most reliable.
"""
import bisect
import re
import struct
import sys

dump, mapfile = sys.argv[1], sys.argv[2]
show_all = '--all' in sys.argv
data = open(dump, 'rb').read()
if data[:4] != b'MDMP':
    sys.exit('not a minidump')
nstreams, dir_rva = struct.unpack_from('<II', data, 8)
streams = {}
for i in range(nstreams):
    typ, size, rva = struct.unpack_from('<III', data, dir_rva + 12 * i)
    streams[typ] = (size, rva)

def utf16_at(rva):
    n = struct.unpack_from('<I', data, rva)[0]
    return data[rva + 4:rva + 4 + n].decode('utf-16-le', 'replace')

# Modules (stream 4): find the game executable.
mods = []
_, rva = streams[4]
for i in range(struct.unpack_from('<I', data, rva)[0]):
    m = rva + 4 + 108 * i
    base, size = struct.unpack_from('<QI', data, m)
    name_rva = struct.unpack_from('<I', data, m + 20)[0]
    mods.append((base, size, utf16_at(name_rva).split('\\')[-1]))
game = next((m for m in mods if m[2].lower().startswith('imabikisopc')), None)
if not game:
    sys.exit('ImabikisoPC.exe not in the dump')

# Thread names (stream 24, ThreadNamesStream), when present.
tnames = {}
if 24 in streams:
    _, rva = streams[24]
    for i in range(struct.unpack_from('<I', data, rva)[0]):
        tid, name_rva = struct.unpack_from('<IQ', data, rva + 4 + 12 * i)
        tnames[tid] = utf16_at(name_rva)

# Map symbols.
pref = 0x140000000
addrs, names = [], []
sym = re.compile(r'\s*0001:[0-9a-fA-F]{8}\s+(\S+)\s+([0-9a-fA-F]{16})\s')
sect = re.compile(r'\s*0001:([0-9a-fA-F]{8}) ([0-9a-fA-F]{8})H')
text_end = 0  # end of the code section, relative to its start (RVA 0x1000)
with open(mapfile, encoding='latin-1') as f:
    for line in f:
        sm = sect.match(line)
        if sm:
            text_end = max(text_end, int(sm.group(1), 16) + int(sm.group(2), 16))
        mm = sym.match(line)
        if mm:
            addrs.append(int(mm.group(2), 16))
            names.append(mm.group(1))
order = sorted(range(len(addrs)), key=addrs.__getitem__)
addrs = [addrs[i] for i in order]
names = [names[i] for i in order]

def where(va):
    for b, s, n in mods:
        if b <= va < b + s:
            if (b, s, n) != game:
                return '%s+%X' % (n, va - b)
            a = pref + (va - b)
            i = bisect.bisect_right(addrs, a) - 1
            if i < 0:
                return 'exe+%X' % (va - b)
            nm = names[i].lstrip('?').split('@@')[0].split('@')[0]
            return '%s+%X' % (nm, a - addrs[i])
    return None

# Threads (stream 3).
_, rva = streams[3]
count = struct.unpack_from('<I', data, rva)[0]
for i in range(count):
    t = rva + 4 + 48 * i
    tid = struct.unpack_from('<I', data, t)[0]
    stack_start, stack_size, stack_rva = struct.unpack_from('<QII', data, t + 24)
    ctx_size, ctx_rva = struct.unpack_from('<II', data, t + 40)
    rsp = struct.unpack_from('<Q', data, ctx_rva + 0x98)[0]
    rip = struct.unpack_from('<Q', data, ctx_rva + 0xF8)[0]
    frames = []
    off = max(0, rsp - stack_start)
    for q in range(off, stack_size - 7, 8):
        v = struct.unpack_from('<Q', data, stack_rva + q)[0]
        if game[0] + 0x1000 <= v < game[0] + 0x1000 + text_end:
            w = where(v)
            if w and (not frames or frames[-1] != w):
                frames.append(w)
        if len(frames) >= (60 if show_all else 14):
            break
    label = tnames.get(tid, '')
    if not frames and not show_all:
        continue  # system/driver threads idle in the kernel
    print('thread %5d %-20s ip=%s' % (tid, label, where(rip) or hex(rip)))
    for fr in frames:
        print('      ', fr)
