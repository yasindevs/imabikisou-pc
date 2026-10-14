"""Convert an ID-to-English JSON mapping to imbk patch UTF-8 TSV input."""
import argparse
import json
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('input',type=Path)
p.add_argument('output',type=Path)
args=p.parse_args()
entries=json.loads(args.input.read_text(encoding='utf-8'))
if not isinstance(entries,dict): p.error('Expected an object mapping stable IDs to text.')
lines=[]
for key,value in entries.items():
    if not isinstance(key,str) or not isinstance(value,str): p.error('IDs and text must be strings.')
    if not key or key.startswith('#') or any(c in key for c in '\r\n\t'): p.error('Invalid script ID.')
    if any(c in value for c in '\r\n\t'): p.error('Use literal \\n / \\t escapes inside script text.')
    lines.append(key+'\t'+value)
args.output.parent.mkdir(parents=True,exist_ok=True)
args.output.write_text('\n'.join(lines)+'\n',encoding='utf-8')
print(f'Exported {len(lines)} draft entries to {args.output}')
