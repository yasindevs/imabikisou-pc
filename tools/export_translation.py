"""Convert an ID-to-English JSON mapping to imbk patch UTF-8 TSV input.

--wrap FONT_ADV_LOG lays text out at word boundaries. The engine breaks a
line by glyph width, wherever the next glyph no longer fits, and ignores the
newline instruction in mid-sentence. So the tool pads the end of a line with
spaces until the next word cannot fit and moves to the next line whole.

Widths come from the game's dialogue font, measured on your own machine: run
the native port once with FONT_ADV_DUMP=1 and keep the
`[font-adv] slot=.. scale=48.00 ...` line (the log is not part of this repo).

--layout LAYOUT_TSV (from `imbk sns layout <game>`) says where each entry
starts: on a new page, a new line, or right after the previous text. Without
it every entry is assumed to start a line.
"""
import argparse
import json
import math
import re
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__,formatter_class=argparse.RawDescriptionHelpFormatter)
p.add_argument('input',type=Path)
p.add_argument('output',type=Path)
p.add_argument('--wrap',type=Path,metavar='FONT_ADV_LOG',help='file holding a FONT_ADV_DUMP line for the dialogue font')
p.add_argument('--layout',type=Path,metavar='LAYOUT_TSV',help='output of `imbk sns layout`')
p.add_argument('--scale',type=float,default=48.0,help='dialogue font pixel scale (default 48)')
# Fitted on 15 line breaks of the first classroom pages: the engine advances
# each glyph by its advance rounded up to whole pixels, a space by half the
# font scale (24 px, not the font's own space advance), and a line holds
# 1022-1023 px.
p.add_argument('--space-width',type=float,default=None,help='width of a space in px (default scale/2)')
p.add_argument('--line-width',type=float,default=1022.5,help='usable line width in px (default 1022.5)')
args=p.parse_args()
entries=json.loads(args.input.read_text(encoding='utf-8'))
if not isinstance(entries,dict): p.error('Expected an object mapping stable IDs to text.')

TAG=re.compile(r'<[0-9A-Fa-f]{2}:[0-9A-Fa-f]*>|\\n')
LEAD=re.compile(r'(?:\\n|<[0-9A-Fa-f]{2}:[0-9A-Fa-f]*>)*')

def load_advances(path, scale):
    for line in path.read_text(encoding='utf-8',errors='replace').splitlines():
        m=re.search(r'\[font-adv\] slot=\d+ scale=([0-9.]+)(.*)',line)
        if m and abs(float(m.group(1))-scale)<0.01:
            return {chr(int(k,16)):math.ceil(float(v)) for k,v in re.findall(r'([0-9A-F]{2}):([0-9.]+)',m.group(2))}
    p.error(f'No [font-adv] line with scale={scale} in {path}')

def wrap(text, x, adv, space, limit):
    """Pad so that no word is cut by the engine's break; returns (text, end x)."""
    def width(word):
        return sum(space if c==' ' else adv.get(c, args.scale) for c in TAG.sub('', word))
    lead=LEAD.match(text).group(0)
    if '\\n' in lead: x=0.0
    out=lead
    for i,word in enumerate(text[len(lead):].split(' ')):
        w=width(word)
        if i or x>0:
            sep=' ' if i else ''
            if word and x>0 and x+len(sep)*space+w>limit and w<=limit:
                k=int((limit-x)//space)          # spaces that still fit
                first=TAG.sub('',word)[:1]
                if adv.get(first,args.scale)<=limit-x-k*space:
                    # The word's first letter would still fit after them: one
                    # more space, which itself breaks the line and starts the
                    # next one (a one-space indent).
                    out+=' '*(k+1); x=space
                else:
                    out+=' '*k; x=0.0
            elif sep:
                out+=sep; x+=space
        out+=word; x+=w
        if x>limit: x=w                  # an over-long word broke anyway; rough
    return out,x

def scene(key):
    # S00A_0000_0310_CALL02#0 -> S00A_0000_0310
    return '_'.join(key.split('#')[0].split('_')[:3])

adv=load_advances(args.wrap,args.scale) if args.wrap else None
space=args.space_width if args.space_width is not None else args.scale/2
order=[]; breaks={}
if args.layout:
    for line in args.layout.read_text(encoding='utf-8').splitlines():
        if '\t' in line:
            k,b=line.split('\t',1); order.append(k); breaks[k]=b.strip()
for key,value in entries.items():
    if not isinstance(key,str) or not isinstance(value,str): p.error('IDs and text must be strings.')
    if not key or key.startswith('#') or any(c in key for c in '\r\n\t'): p.error('Invalid script ID.')
    if any(c in value for c in '\r\n\t'): p.error('Use literal \\n / \\t escapes inside script text.')

out={}
if adv:
    # Walk entries in script order so a continued line knows where it starts.
    # "cont" carries on from the previous translated entry of the same scene.
    # Choice outcomes (labels with _SEL_) continue that line too, but they are
    # alternatives: the text after them continues from the entry before.
    end={}; prev=None
    for key in order+[k for k in entries if k not in breaks]:
        if key not in entries or key in out: continue
        choice='_SEL_' in key
        x=0.0
        if breaks.get(key)=='cont' and prev and (choice or scene(prev)==scene(key)): x=end[prev]
        out[key],end[key]=wrap(entries[key],x,adv,space,args.line_width)
        if not choice: prev=key
else:
    out=dict(entries)
lines=[key+'\t'+out[key] for key in entries]
args.output.parent.mkdir(parents=True,exist_ok=True)
args.output.write_text('\n'.join(lines)+'\n',encoding='utf-8')
print(f'Exported {len(lines)} draft entries to {args.output}'+(' (word-wrapped)' if adv else ''))
