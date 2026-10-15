"""Regenerate the GitHub progress chart from docs/progress.json (Pillow)."""
from pathlib import Path
import json
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
data = json.loads((ROOT / 'docs/progress.json').read_text(encoding='utf-8'))
streams = data['streams']
assert sum(s['weight'] for s in streams) == 100
assert all(0 <= s['percent'] <= 100 for s in streams)
overall = round(sum(s['percent'] * s['weight'] for s in streams) / 100)

def font(size, bold=False):
    candidates = [Path('C:/Windows/Fonts') / ('segoeuib.ttf' if bold else 'segoeui.ttf'),
                  Path('/usr/share/fonts/truetype/dejavu') / ('DejaVuSans-Bold.ttf' if bold else 'DejaVuSans.ttf')]
    for p in candidates:
        if p.exists(): return ImageFont.truetype(str(p), size)
    return ImageFont.load_default(size=size)

img = Image.new('RGB', (1280, 970), '#0d1117')
d = ImageDraw.Draw(img)
white, muted, accent, empty = '#e6edf3', '#a8b3c2', '#56d6be', '#283442'
d.rounded_rectangle((1, 1, 1278, 968), radius=24, outline='#303b49', width=2)
d.text((56, 40), 'IMABIKISO / PC RECOMPILATION', font=font(21,True), fill=accent)
d.text((56, 79), 'Development snapshot', font=font(43,True), fill=white)
d.text((56, 143), 'Opening + first classroom tested. Full game remains unverified.', font=font(23), fill=muted)
d.rounded_rectangle((56, 201, 1224, 319), radius=16, fill='#161e29')
d.text((80, 215), f'{overall}%', font=font(54,True), fill=accent)
d.text((249, 225), 'Estimated overall readiness', font=font(27,True), fill=white)
d.text((249, 268), 'Weighted planning score, not story completion or a release forecast.', font=font(20), fill=muted)
for i,s in enumerate(streams):
    y=365+i*64
    p=s['percent']; text=f'{p:.2f}% draft' if p<1 else f'{p:.0f}%'
    d.text((58,y),s['label'],font=font(24,True),fill=white)
    x,bar_y,w=440,y+5,580
    d.rounded_rectangle((x,bar_y,x+w,bar_y+22),radius=9,fill=empty)
    if p>=1:
        d.rounded_rectangle((x,bar_y,x+w*p/100,bar_y+22),radius=min(9,w*p/200),fill=accent)
    else:
        d.rectangle((x,bar_y,x+2,bar_y+22),fill=accent)
    d.text((1050,y-1),text,font=font(22,True),fill=accent)
d.rectangle((58,839,74,855),fill=accent)
d.text((85,830),'Estimated ready / draft',font=font(20),fill=muted)
d.rectangle((409,839,425,855),fill=empty)
d.text((436,830),'Remaining or unverified work',font=font(20),fill=muted)
d.text((58,876),'English draft: 33 / 19,018 story entries + 27 UI labels counted separately.',font=font(21),fill=white)
d.text((58,917),f"Updated {data['updated']}  |  Occasional audio gaps remain; most scenes untested.",font=font(20),fill=muted)
out=ROOT/'docs/assets/progress.png';out.parent.mkdir(parents=True,exist_ok=True)
img.save(out)
print(f'{out}: overall {overall}%; story draft {33/19018*100:.2f}%')
