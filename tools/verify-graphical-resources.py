"""Validate the centralized PNG bundle after source PNG removal."""
from pathlib import Path
import base64, re, sys

root = Path(__file__).parents[1]
bundle = (root / "src/Win7Taskbar/Utilities/GraphicalResourceBundle.cs").read_text()
docs = (root / "docs/GRAPHICAL-RESOURCES.md").read_text()
theme = (root / "src/Win7Taskbar/Themes/Windows7.xaml").read_text()
# These are consumed by the native Aero 9-slice renderer, not WPF.
native_files = {"top_left.png", "top_center.png", "top_right.png", "mid_left.png",
                "mid_right.png", "bottom_left.png", "bottom_center.png", "bottom_right.png"}
remaining = {p.name for p in (root / "src/Win7Taskbar/Resources").rglob("*.png")}
keys = re.findall(r'\["([a-z0-9_]+)"\]\s*=', bundle)
docids = set(re.findall(r'^\| `([^`]+)` \|', docs, re.M))
errors = []
if len(keys) != 52 or len(set(keys)) != 52:
    errors.append(f"expected 52 unique bundle keys, found {len(keys)} entries/{len(set(keys))} unique")
if remaining != native_files:
    errors.append(f"unexpected remaining PNGs: {sorted(remaining - native_files)}")
if set(keys) != docids:
    errors.append("bundle keys and documentation IDs differ")
if "../Resources/" in theme or 'UriSource="' in theme:
    errors.append("Windows7.xaml still contains a legacy PNG URI")
# Decode every payload and validate the PNG signature without needing source files.
for key in keys:
    m = re.search(re.escape('["' + key + '"]') + r'\s*=\s*((?:"[A-Za-z0-9+/=]+"\s*\+?\s*)+),', bundle)
    if not m:
        errors.append("missing payload " + key)
        continue
    encoded = ''.join(re.findall(r'"([A-Za-z0-9+/=]+)"', m.group(1)))
    try:
        data = base64.b64decode(encoded, validate=True)
        if data[:8] != b'\x89PNG\r\n\x1a\n': errors.append("not a PNG: " + key)
    except Exception as exc:
        errors.append(f"invalid Base64 for {key}: {exc}")
# Runtime source references are forbidden; comments/documentation may mention provenance.
for path in (root / "src/Win7Taskbar").rglob("*.cs"):
    text = path.read_text(errors="ignore")
    if 'AppContext.BaseDirectory, "Resources"' in text or 'UriSource="../Resources/' in text:
        errors.append("runtime PNG path reference: " + str(path.relative_to(root)))
print(f"{len(keys)} bundle assets, {len(docids)} documented, {len(remaining)} intentional native PNGs")
if errors:
    print("\n".join(errors)); sys.exit(1)
print("OK: Base64/PNG integrity, unique keys, documentation, XAML and runtime references")
