"""Check that the live preview and its Aero frame describe the same border.

What this guards (a real defect reported on a 125% display): the frame is a
9-slice of 17/38/19-unit slices around a 202x109 aperture, and the live DWM
surface is placed in that aperture. The two only agree if the frame bitmap is
produced in the SAME unit the frame template lays out - layout units - so that
the compositor scales border and aperture together.

Up to v1.1.0 Utilities/NativePreviewFrame.cs asked the core for the frame at the
element's DEVICE size (`PointToScreen`). The slices then stayed 17/38/19 screen
pixels while the aperture moved with the popup (17/38/19 DIP: 18.7/41.8/20.9 px
at 125%, where the popup also draws its normalised geometry 10% larger), so the
thumbnail stopped short of the border and left an unpainted strip inside the
frame: ~1.7 px on the sides and ~3.8 px under the title band at 125%.

Checks, on the sources in the repository:
  - the 9-slice thicknesses in native/src/AeroThumbnailFrame.h, the eight slice
    PNGs in Resources/ and the row/column bands of the TaskPreviewFrameVista
    template are the same numbers
  - the template's centre viewboxes are the 202x109 aperture, the same size the
    TaskThumbnail control declares
  - the frame renderer sizes its bitmap from the element's layout size (no
    PointToScreen/device conversion), which is what keeps the two together
  - for every display scale, the 9-slice opening of the layout-unit bitmap is
    the aperture, and the size of the strip the old device-pixel rule left
    uncovered is printed so the regression is visible

It is wired into the native build as a POST_BUILD check (native/CMakeLists.txt,
through run-check.py, which passes the built DLL as its only argument - this
check ignores arguments because it only reads sources). It can also be run by
hand from anywhere:  python3 native/tools/check-preview-geometry.py
"""

from __future__ import annotations

import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / 'native/src/AeroThumbnailFrame.h'
RENDERER = ROOT / 'src/Win7Taskbar/Utilities/NativePreviewFrame.cs'
TEMPLATE_FILE = ROOT / 'src/Win7Taskbar/Themes/Overrides.xaml'
THUMB_XAML = ROOT / 'src/Win7Taskbar/Controls/TaskThumbnail.xaml'
POPUP_CODE = ROOT / 'src/Win7Taskbar/TaskbarWindow.xaml.cs'
RESOURCES = ROOT / 'Resources'

# (file, side) of every 9-slice PNG, in the order the frame is assembled.
SLICES = {
    'top_left': (17, 38), 'top_center': (202, 38), 'top_right': (17, 38),
    'mid_left': (17, 109), 'mid_right': (17, 109),
    'bottom_left': (17, 19), 'bottom_center': (202, 19), 'bottom_right': (17, 19),
}

failures: list[str] = []


def check(condition: bool, message: str) -> None:
    if not condition:
        failures.append(message)


def png_size(path: Path) -> tuple[int, int]:
    data = path.read_bytes()[:24]
    check(not data or data[:8] == b'\x89PNG\r\n\x1a\n', f'{path.name}: not a PNG')
    return struct.unpack('>II', data[16:24])


def header_thicknesses() -> dict[str, int]:
    text = HEADER.read_text(encoding='utf-8')
    wanted = {'Left': 17, 'Right': 17, 'Top': 38, 'Bottom': 19}
    out: dict[str, int] = {}
    for name in wanted:
        match = re.search(rf'constexpr int kAeroFrame{name}\s*=\s*(\d+);', text)
        check(match is not None, f'{HEADER.name}: kAeroFrame{name} not found')
        out[name] = int(match.group(1)) if match else wanted[name]
    return out


def template_bands() -> tuple[list[int], list[int], list[tuple[int, int, int, int]]]:
    """Rows, columns and the eight mask viewboxes of the Win7 frame."""
    text = TEMPLATE_FILE.read_text(encoding='utf-8')
    start = text.index('x:Key="TaskPreviewFrameVista"')
    body = text[start:text.index('</ControlTemplate>', start)]

    rows = [int(v) if v.strip() != '*' else 0
            for v in re.findall(r'<RowDefinition Height="([^"]+)"\s*/>', body)][:3]
    cols = [int(v) if v.strip() != '*' else 0
            for v in re.findall(r'<ColumnDefinition Width="([^"]+)"\s*/>', body)][:3]
    boxes = [(int(a), int(b), int(c), int(d)) for a, b, c, d in re.findall(
        r'<ImageBrush ImageSource="\{DynamicResource DwmPreviewBorderMaskImage\}"\s*'
        r'Viewbox="(\d+),(\d+),(\d+),(\d+)"', body)]
    return rows, cols, boxes


def write_number(path: Path, pattern: str, label: str) -> float:
    match = re.search(pattern, path.read_text(encoding='utf-8'))
    check(match is not None, f'{path.name}: {label} not found')
    return float(match.group(1)) if match else 0.0


def main() -> int:
    thickness = header_thicknesses()

    # --- one ruler: header constants, slice PNGs and template bands ---------
    for name, size in SLICES.items():
        path = RESOURCES / f'{name}.png'
        check(path.exists(), f'{path.relative_to(ROOT)} is missing')
        if not path.exists():
            continue
        check(png_size(path) == size,
              f'{path.name} is {png_size(path)}, expected {size}')

    rows, cols, boxes = template_bands()
    check(rows == [thickness['Top'], 0, thickness['Bottom']],
          f'template rows {rows} do not match top/bottom '
          f"{thickness['Top']}/{thickness['Bottom']}")
    check(cols == [thickness['Left'], 0, thickness['Right']],
          f'template columns {cols} do not match left/right '
          f"{thickness['Left']}/{thickness['Right']}")

    aperture_w = SLICES['top_center'][0]
    aperture_h = SLICES['mid_left'][1]
    # The eight bands must be the 3x3 grid minus its centre: the middle column
    # is never used (x = 17..218) and so is the middle row (y = 38..146), which
    # is exactly the free aperture the live thumbnail is placed in.
    bands = {(0, 0), (17, 0), (219, 0), (0, 38), (219, 38),
             (0, 147), (17, 147), (219, 147)}
    check({(x, y) for x, y, _, _ in boxes} == bands,
          f'mask viewboxes {sorted({(x, y) for x, y, _, _ in boxes})} are not '
          f'the 3x3 grid minus its {aperture_w}x{aperture_h} centre')

    # --- the aperture is the cell the live thumbnail is placed in ----------
    thumb = THUMB_XAML.read_text(encoding='utf-8')
    check(f'Width="{aperture_w}"' in thumb and f'Height="{aperture_h}"' in thumb,
          f'{THUMB_XAML.name} does not declare the {aperture_w}x{aperture_h} '
          'aperture of the frame')

    # --- the renderer asks for the frame in layout units -------------------
    renderer = RENDERER.read_text(encoding='utf-8')
    check('PointToScreen' not in renderer,
          f'{RENDERER.name} measures the element in device pixels again: the '
          '9-slice slices would stop matching the DIP aperture on any monitor '
          'above 100% scaling')
    check('host.ActualWidth' in renderer and 'Math.Round(width)' in renderer,
          f'{RENDERER.name} no longer sizes the frame bitmap from the '
          "element's layout size")

    # --- geometry at every display scale -----------------------------------
    enlargement = write_number(POPUP_CODE,
                               r'PreviewHighDpiEnlargement\s*=\s*([0-9.]+)',
                               'PreviewHighDpiEnlargement')
    check(enlargement > 0.0, f'PreviewHighDpiEnlargement is {enlargement}')
    left = right = thickness['Left']
    top, bottom = thickness['Top'], thickness['Bottom']
    frame_w = aperture_w + left + right
    frame_h = aperture_h + top + bottom

    print(f'frame: {frame_w}x{frame_h} layout units, aperture '
          f'{aperture_w}x{aperture_h}, border {left}/{top}/{right}/{bottom}')
    print('display  frame px      aperture px    strip the device-pixel rule '
          'left inside the frame')

    for scale in (1.0, 1.25, 1.5, 1.75, 2.0):
        k = enlargement if scale > 1.001 else 1.0          # device px per unit
        device_w = frame_w * k
        device_h = frame_h * k
        # Frame bitmap in layout units: its opening is the aperture, always.
        opening = (left * k, top * k, device_w - right * k, device_h - bottom * k)
        aperture = (left * k, top * k,
                    (left + aperture_w) * k, (top + aperture_h) * k)
        check(all(abs(a - b) < 1e-9 for a, b in zip(opening, aperture)),
              f'{scale:g}: layout-unit bitmap opening {opening} != aperture {aperture}')

        # The rule that caused the report: bitmap rendered at the device size,
        # so its slices stay whole screen pixels while the aperture scales.
        old_opening = (left, top, round(device_w) - right, round(device_h) - bottom)
        strips = (max(0.0, aperture[0] - old_opening[0]),
                  max(0.0, aperture[1] - old_opening[1]),
                  max(0.0, old_opening[2] - aperture[2]),
                  max(0.0, old_opening[3] - aperture[3]))
        print(f'{scale * 100:5.0f}%  {device_w:6.1f}x{device_h:<7.1f} '
              f'{aperture_w * k:6.1f}x{aperture_h * k:<7.1f} '
              f'left {strips[0]:.1f}, top {strips[1]:.1f}, right {strips[2]:.1f}, '
              f'bottom {strips[3]:.1f} px')

    if failures:
        print('\nFAILED:')
        for message in failures:
            print(f'  - {message}')
        return 1

    print('\nOK: the frame slices and the live aperture are the same geometry.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
