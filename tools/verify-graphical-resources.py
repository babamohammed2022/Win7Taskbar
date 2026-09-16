"""Verify the lossless WPF PNG bundle, source inventory, and documentation."""
from pathlib import Path
import base64,re,sys
root=Path(__file__).parents[1]; assets=sorted((root/'src/Win7Taskbar/Resources').rglob('*.png'))
bundle=(root/'src/Win7Taskbar/Utilities/GraphicalResourceBundle.cs').read_text()
docs=(root/'docs/GRAPHICAL-RESOURCES.md').read_text()
def key(p): return re.sub(r'[^A-Za-z0-9_]','_',p.relative_to(root/'src/Win7Taskbar/Resources').with_suffix('').as_posix()).lower()
keys=set(re.findall(r'\["([a-z0-9_]+)"\]\s*=',bundle)); docids=set(re.findall(r'^\| `([^`]+)` \|',docs,re.M))
errors=[]
if len(assets)!=len(keys): errors.append(f'asset count {len(assets)} != bundle count {len(keys)}')
if {key(p) for p in assets} != keys: errors.append('source and bundle keys differ')
if keys != docids: errors.append('bundle and documentation keys differ')
for p in assets:
 needle='["'+key(p)+'"]'
 m=re.search(re.escape(needle)+r'\s*=\s*((?:"[A-Za-z0-9+/=]+"\s*\+?\s*)+),',bundle)
 if not m: errors.append('missing payload '+str(p)); continue
 encoded=''.join(re.findall(r'"([A-Za-z0-9+/=]+)"',m.group(1)))
 if base64.b64decode(encoded)!=p.read_bytes(): errors.append('payload differs '+str(p))
print(f'{len(assets)} source assets, {len(keys)} bundle keys, {len(docids)} documented')
if errors: print('\n'.join(errors)); sys.exit(1)
print('OK: source ↔ bundle ↔ documentation and byte integrity match')
