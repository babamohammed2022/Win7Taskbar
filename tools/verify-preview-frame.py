"""Check that the Windows 8.1 preview frame is drawn in ONE colour.

The Windows 7 frame is eight slices of the "dwmborder" asset plus a grey
veil at 18% painted on top of the accent colour, which is what gives the
photographic chiaroscuro of Aero glass. Copying that veil into the 8.1
variant produced two colours on the same frame: the outer rim pixels of the
asset are dark photographic RGB (24-50) at full alpha while the body of the
band is mid grey (66-76) at alpha 207, so the rim composited to grey-green
(66,121,165) and the body to light blue (102,149,186).

Checks, on the runtime copy of the theme:
  - every rectangle of the frame is filled with the accent brush and masked
    with the asset (a fill using the asset itself is the grey veil)
  - no rectangle carries Opacity="0.18"
  - the two copies of the theme file are byte-identical
  - compositing the frame over the lightest background leaves every painted
    pixel on the accent<->background line, i.e. the hue never changes and only
    the alpha does

Run from the repository root:  python3 tools/verify-preview-frame.py
"""
from __future__ import annotations

import base64
import re
import struct
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
THEME = 'src/Win7Taskbar/Themes/Windows8.1.xaml'
THEME_COPY = 'Themes/Windows8.1.xaml'
BUNDLE = 'src/Win7Taskbar/Utilities/GraphicalResourceBundle.cs'
TEMPLATE = 'Win81TaskPreviewFrameVista'

ACCENT = (0x4C, 0x8F, 0xC5)
BACKGROUND = (255, 255, 255)
OX = {0: 0, 1: 17, 2: 219}
OY = {0: 0, 1: 38, 2: 147}
CW = {0: 17, 1: 202, 2: 17}
CH = {0: 38, 1: 109, 2: 19}


def bundle_member(member: str) -> bytes:
    """Decode one base64 member of GraphicalResourceBundle.cs."""
    lines = (ROOT / BUNDLE).read_text(encoding='utf-8').split('\n')
    start = next(i for i, line in enumerate(lines) if f'["{member}"]' in line) + 1
    payload = ''
    i = start
    while True:
        payload += ''.join(re.findall(r'"([^"]*)"', lines[i]))
        if lines[i].rstrip().endswith('+'):
            i += 1
            continue
        break
    return base64.b64decode(payload)


def decode_png(blob: bytes):
    """Minimal PNG reader (RGBA / palette / grey), returns (w, h, get)."""
    pos, idat, plte, trns = 8, b'', None, None
    while pos < len(blob):
        ln = struct.unpack('>I', blob[pos:pos + 4])[0]
        typ, data = blob[pos + 4:pos + 8], blob[pos + 8:pos + 8 + ln]
        if typ == b'IHDR':
            w, h, _bd, ct = struct.unpack('>IIBB', data[:10])
        elif typ == b'IDAT':
            idat += data
        elif typ == b'PLTE':
            plte = data
        elif typ == b'tRNS':
            trns = data
        pos += 12 + ln
    raw = zlib.decompress(idat)
    nch = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ct]
    stride = w * nch
    out, prev, p = bytearray(), bytearray(stride), 0
    for _y in range(h):
        f = raw[p]
        p += 1
        line = bytearray(raw[p:p + stride])
        p += stride
        if f == 1:
            for k in range(nch, stride):
                line[k] = (line[k] + line[k - nch]) & 255
        elif f == 2:
            for k in range(stride):
                line[k] = (line[k] + prev[k]) & 255
        elif f == 3:
            for k in range(stride):
                a = line[k - nch] if k >= nch else 0
                line[k] = (line[k] + ((a + prev[k]) >> 1)) & 255
        elif f == 4:
            for k in range(stride):
                a = line[k - nch] if k >= nch else 0
                c = prev[k - nch] if k >= nch else 0
                b = prev[k]
                pp = a + b - c
                pa, pb, pc = abs(pp - a), abs(pp - b), abs(pp - c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[k] = (line[k] + pr) & 255
        out += line
        prev = line
    px = bytes(out)

    def get(x: int, y: int):
        x = max(0, min(w - 1, x))
        y = max(0, min(h - 1, y))
        if ct == 6:
            o = (y * w + x) * 4
            return px[o], px[o + 1], px[o + 2], px[o + 3]
        if ct == 3:
            o = y * w + x
            i = px[o]
            return plte[i * 3], plte[i * 3 + 1], plte[i * 3 + 2], (trns[i] if (trns and i < len(trns)) else 255)
        o = (y * w + x) * nch
        return px[o], px[o + 1], px[o + 2], (255 if nch < 4 else px[o + 3])

    return w, h, get


def main() -> int:
    theme = (ROOT / THEME).read_text(encoding='utf-8')
    start = theme.index(f'<ControlTemplate x:Key="{TEMPLATE}"')
    block = theme[start:theme.index('</ControlTemplate>', start)]
    rects = re.findall(r'<Rectangle Grid\.Row="\d" Grid\.Column="\d"[\s\S]*?</Rectangle>', block)

    problems = []
    accent = [r for r in rects if 'Fill="{DynamicResource DwmPreviewAccentBrush}"' in r]
    masked = [r for r in rects if 'ImageSource="{DynamicResource DwmPreviewBorderMaskImage}"' in r]
    photographic = [r for r in rects if 'ImageSource="{DynamicResource DwmPreviewBorderImage}"' in r]
    veil = [r for r in rects if 'Opacity="0.18"' in r]
    print(f'frame rectangles          : {len(rects)}')
    print(f'  filled with the accent  : {len(accent)}')
    print(f'  masked with the asset   : {len(masked)}')
    print(f'  filled with the asset   : {len(photographic)}  (must be 0: that is the grey veil)')
    print(f'  veils at 18%            : {len(veil)}  (must be 0)')
    if not (len(rects) == 12 and len(accent) == 12 and len(masked) == 12) or photographic or veil:
        problems.append('the frame is not built only from accent-filled, asset-masked rectangles')

    if (ROOT / THEME).read_bytes() != (ROOT / THEME_COPY).read_bytes():
        problems.append('the two copies of Themes/Windows8.1.xaml differ')

    _w, _h, sample = decode_png(bundle_member('dwmborder'))
    pixels: dict[tuple[int, int], tuple[float, float, float, float]] = {}
    for rect in rects:
        row = int(re.search(r'Grid\.Row="(\d)"', rect).group(1))
        col = int(re.search(r'Grid\.Column="(\d)"', rect).group(1))
        vb = [int(v) for v in re.search(r'Viewbox="([^"]+)"', rect).group(1).split(',')]
        strip = re.search(r'Width="3" HorizontalAlignment="(\w+)"', rect)
        dx = (0 if strip.group(1) == 'Left' else CW[col] - 3) if strip else 0
        dw = 3 if strip else CW[col]
        for sx in range(OX[col] + dx, OX[col] + dx + dw):
            for sy in range(OY[row], OY[row] + CH[row]):
                u = (sx - OX[col] - dx) / dw
                v = (sy - OY[row]) / CH[row]
                ax = vb[0] + min(vb[2] - 1, int(u * vb[2]))
                ay = vb[1] + min(vb[3] - 1, int(v * vb[3]))
                a = sample(ax, ay)[3] / 255.0
                if a <= 0:
                    continue
                pa, pr, pg, pb = pixels.get((sx, sy), (0.0, 0.0, 0.0, 0.0))
                pixels[(sx, sy)] = (
                    a + pa * (1 - a),
                    ACCENT[0] * a + pr * (1 - a),
                    ACCENT[1] * a + pg * (1 - a),
                    ACCENT[2] * a + pb * (1 - a),
                )

    worst, off_hue, alphas = 0.0, 0, set()
    for a, r, g, b in pixels.values():
        alphas.add(round(a, 3))
        fr = r + BACKGROUND[0] * (1 - a)
        fg = g + BACKGROUND[1] * (1 - a)
        fb = b + BACKGROUND[2] * (1 - a)
        t = (fr - BACKGROUND[0]) / (ACCENT[0] - BACKGROUND[0])
        for channel, value in ((1, fg), (2, fb)):
            expected = ACCENT[channel] * t + BACKGROUND[channel] * (1 - t)
            worst = max(worst, abs(value - expected))
            if abs(value - expected) > 1.5:
                off_hue += 1
    print()
    print(f'painted pixels            : {len(pixels)}')
    print(f'pixels off the accent hue : {off_hue}  (must be 0)')
    print(f'worst deviation           : {worst:.3f}/255')
    print(f'alpha values present      : {sorted(alphas)}')
    if off_hue:
        problems.append('some pixels are not the accent colour')

    print()
    for problem in problems:
        print(f'PROBLEM: {problem}')
    print('RESULT:', 'the preview frame is a single colour' if not problems else 'THE FRAME IS NOT UNIFORM')
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
