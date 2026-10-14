"""Name the RVAs in the port's [sample] lines from the linker map.

usage: python resolve_samples.py <run.err.txt> [MyGameRecomp.map]
"""
import bisect
import re
import sys

log = sys.argv[1]
mapfile = sys.argv[2] if len(sys.argv) > 2 else "build-native/MyGameRecomp.map"

base = 0x140000000
addrs, names = [], []
sym = re.compile(r"\s*[0-9a-fA-F]{4}:[0-9a-fA-F]{8}\s+(\S+)\s+([0-9a-fA-F]{16})\s")
with open(mapfile, encoding="latin-1") as f:
    for line in f:
        m = sym.match(line)
        if m:
            addrs.append(int(m.group(2), 16))
            names.append(m.group(1))
order = sorted(range(len(addrs)), key=addrs.__getitem__)
addrs = [addrs[i] for i in order]
names = [names[i] for i in order]


def name(rva):
    a = base + rva
    i = bisect.bisect_right(addrs, a) - 1
    if i < 0 or a - addrs[i] > 0x100000:
        return "?%X" % rva
    n = names[i].lstrip("?")
    if n.startswith("func_"):
        n = n[5:13]           # guest address
    return n if a == addrs[i] else "%s+%X" % (n, a - addrs[i])


with open(log, encoding="latin-1") as f:
    for line in f:
        if not line.startswith("[sample]"):
            continue
        parts = line.split()
        if len(parts) > 2 and parts[1].endswith("%"):
            tag = [p for p in parts[2:] if p.startswith("T")]
            frames = [p for p in parts[2:] if not p.startswith("T")]
            print(parts[1], *tag, " <- ".join(name(int(p, 16)) for p in frames))
        else:
            print(line.rstrip())
