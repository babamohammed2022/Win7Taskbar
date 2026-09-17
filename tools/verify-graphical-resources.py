"""Validate the centralized graphical resource bundle and remaining on-disk assets.

Checks:
  - every Base64 payload in GraphicalResourceBundle.cs decodes and is a PNG
  - bundle keys are unique and match docs/GRAPHICAL-RESOURCES.md
  - the runtime WPF theme no longer references loose PNG URIs
  - only the eight intentional native Aero 9-slice PNGs remain as runtime files
  - root Resources/ and src Resources/ stay in sync for those native slices
  - documentation / README screenshots are not flagged as runtime assets
  - no new runtime graphical files appear outside the declared exceptions
  - no dead runtime references to removed PNG paths remain in C# / XAML
"""
from __future__ import annotations

from pathlib import Path
import base64
import re
import sys

root = Path(__file__).resolve().parents[1]

BUNDLE_PATH = root / "src/Win7Taskbar/Utilities/GraphicalResourceBundle.cs"
DOCS_PATH = root / "docs/GRAPHICAL-RESOURCES.md"
THEME_PATH = root / "src/Win7Taskbar/Themes/Windows7.xaml"
ROOT_THEME_PATH = root / "Themes/Windows7.xaml"
SRC_RESOURCES = root / "src/Win7Taskbar/Resources"
ROOT_RESOURCES = root / "Resources"

# Consumed by the native Aero 9-slice renderer (filesystem load via WIC).
NATIVE_SLICE_NAMES = frozenset(
    {
        "top_left.png",
        "top_center.png",
        "top_right.png",
        "mid_left.png",
        "mid_right.png",
        "bottom_left.png",
        "bottom_center.png",
        "bottom_right.png",
    }
)

# Build-time inputs embedded into the executable / native DLL.
# ApplicationIcon and the Win32 RC script require physical ICO files at build
# time; they are not loose runtime theme assets and must stay ICO (not PNG).
BUILD_INPUT_IMAGES = frozenset(
    {
        root / "src/Win7Taskbar/app.ico",
        root / "native/resources/app.ico",
    }
)

# Documentation-only images (README / docs presentation). Never runtime.
DOCUMENTATION_IMAGES = frozenset(
    {
        root / "docs/icon-256.png",
    }
)

IMAGE_SUFFIXES = {".png", ".jpg", ".jpeg", ".bmp", ".gif", ".ico", ".webp", ".tif", ".tiff", ".svg"}

PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
ICO_SIGNATURE = b"\x00\x00\x01\x00"

# v1.21.27: 52 chiavi originarie + bandierina Start Windows 8.1
# (startwin81flag / startwin81flagscaled) = 54.
EXPECTED_BUNDLE_COUNT = 54

SKIP_DIR_NAMES = {
    ".git",
    "bin",
    "obj",
    "dist",
    "dist-package",
    "build",
    "native/build",
    "__pycache__",
    "Unselected files",
}

errors: list[str] = []


def fail(msg: str) -> None:
    errors.append(msg)


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def should_skip(path: Path) -> bool:
    rel_parts = path.relative_to(root).parts
    if ".git" in rel_parts:
        return True
    if any(part in {"bin", "obj", "dist", "dist-package", "__pycache__"} for part in rel_parts):
        return True
    # native/build and similar cmake trees
    for i, part in enumerate(rel_parts):
        if part == "build" and i > 0 and rel_parts[i - 1] in {"native", "compilation files"}:
            return True
        if part.startswith("build-"):
            return True
    return False


def collect_image_files() -> list[Path]:
    images: list[Path] = []
    for path in root.rglob("*"):
        if not path.is_file():
            continue
        if path.suffix.lower() not in IMAGE_SUFFIXES:
            continue
        if should_skip(path):
            continue
        images.append(path)
    return sorted(images)


def parse_bundle(bundle_text: str) -> dict[str, bytes]:
    pattern = re.compile(
        r'\["([a-z0-9_]+)"\]\s*=\s*((?:"[A-Za-z0-9+/=]+"\s*\+?\s*)+),',
        re.MULTILINE,
    )
    payloads: dict[str, bytes] = {}
    for key, body in pattern.findall(bundle_text):
        if key in payloads:
            fail(f"duplicate bundle key: {key}")
        encoded = "".join(re.findall(r'"([A-Za-z0-9+/=]+)"', body))
        try:
            data = base64.b64decode(encoded, validate=True)
        except Exception as exc:  # noqa: BLE001
            fail(f"invalid Base64 for {key}: {exc}")
            continue
        if data[:8] != PNG_SIGNATURE:
            fail(f"not a PNG payload: {key}")
        payloads[key] = data
    return payloads


def pngs_in(directory: Path) -> set[str]:
    if not directory.is_dir():
        return set()
    return {p.name for p in directory.rglob("*.png")}


def main() -> int:
    if not BUNDLE_PATH.is_file():
        print(f"missing bundle: {BUNDLE_PATH}")
        return 1
    if not DOCS_PATH.is_file():
        print(f"missing docs: {DOCS_PATH}")
        return 1
    if not THEME_PATH.is_file():
        print(f"missing theme: {THEME_PATH}")
        return 1

    bundle_text = read_text(BUNDLE_PATH)
    docs_text = read_text(DOCS_PATH)
    theme_text = read_text(THEME_PATH)
    root_theme_text = read_text(ROOT_THEME_PATH) if ROOT_THEME_PATH.is_file() else ""

    payloads = parse_bundle(bundle_text)
    keys = list(payloads.keys())
    key_set = set(keys)

    if len(keys) != EXPECTED_BUNDLE_COUNT or len(key_set) != EXPECTED_BUNDLE_COUNT:
        fail(
            f"expected {EXPECTED_BUNDLE_COUNT} unique bundle keys, "
            f"found {len(keys)} entries/{len(key_set)} unique"
        )

    doc_ids = set(re.findall(r"^\| `([^`]+)` \|", docs_text, re.M))
    if key_set != doc_ids:
        only_bundle = sorted(key_set - doc_ids)
        only_docs = sorted(doc_ids - key_set)
        fail(
            "bundle keys and documentation IDs differ; "
            f"only_bundle={only_bundle} only_docs={only_docs}"
        )

    # Theme must use the bundle, not loose image URIs.
    uri_re = re.compile(
        r'UriSource\s*=\s*"[^"]*\.(?:png|jpg|jpeg|bmp|gif|ico|webp|svg)"',
        re.IGNORECASE,
    )
    for label, text in (("src theme", theme_text), ("root theme", root_theme_text)):
        if not text:
            continue
        if uri_re.search(text):
            fail(f"{label} still contains a loose image UriSource")

    # Native slices must exist in both packaging locations and be byte-identical.
    src_pngs = pngs_in(SRC_RESOURCES)
    root_pngs = pngs_in(ROOT_RESOURCES)
    if src_pngs != NATIVE_SLICE_NAMES:
        fail(
            f"src Resources PNGs mismatch: {sorted(src_pngs)} "
            f"expected {sorted(NATIVE_SLICE_NAMES)}"
        )
    if root_pngs != NATIVE_SLICE_NAMES:
        fail(
            f"root Resources PNGs mismatch: {sorted(root_pngs)} "
            f"expected {sorted(NATIVE_SLICE_NAMES)}"
        )

    for name in sorted(NATIVE_SLICE_NAMES):
        src_file = SRC_RESOURCES / name
        root_file = ROOT_RESOURCES / name
        if not src_file.is_file():
            fail(f"missing native slice in src: {name}")
            continue
        if not root_file.is_file():
            fail(f"missing native slice in root Resources: {name}")
            continue
        src_bytes = src_file.read_bytes()
        root_bytes = root_file.read_bytes()
        if src_bytes != root_bytes:
            fail(f"native slice out of sync between src and root: {name}")
        if src_bytes[:8] != PNG_SIGNATURE:
            fail(f"native slice is not a PNG: {name}")
        key = name[: -len(".png")]
        if key in payloads and payloads[key] != src_bytes:
            fail(f"bundle payload for native slice key {key!r} differs from on-disk bytes")

    allowed_runtime = {SRC_RESOURCES / n for n in NATIVE_SLICE_NAMES} | {
        ROOT_RESOURCES / n for n in NATIVE_SLICE_NAMES
    }
    allowed = allowed_runtime | BUILD_INPUT_IMAGES | DOCUMENTATION_IMAGES

    for path in collect_image_files():
        if path in allowed:
            continue
        rel = path.relative_to(root).as_posix()
        # Any other image under docs/ is documentation (screenshots, etc.).
        if rel.startswith("docs/"):
            continue
        fail(f"undeclared runtime/graphical file (register in bundle or exceptions): {rel}")

    for ico in BUILD_INPUT_IMAGES:
        if not ico.is_file():
            fail(f"missing build-input icon: {ico.relative_to(root)}")
            continue
        data = ico.read_bytes()
        if data[:4] != ICO_SIGNATURE:
            fail(f"build-input icon is not ICO format: {ico.relative_to(root)}")
    # The two app.ico files must stay byte-identical (same brand asset).
    managed_ico = root / "src/Win7Taskbar/app.ico"
    native_ico = root / "native/resources/app.ico"
    if managed_ico.is_file() and native_ico.is_file():
        if managed_ico.read_bytes() != native_ico.read_bytes():
            fail("managed app.ico and native resources/app.ico differ")

    # No live UriSource attributes pointing at removed PNG trees.
    for path in (root / "src/Win7Taskbar").rglob("*"):
        if path.suffix.lower() not in {".cs", ".xaml"}:
            continue
        if path.name == "GraphicalResourceBundle.cs":
            continue
        text = path.read_text(encoding="utf-8", errors="ignore")
        if re.search(r'UriSource\s*=\s*"\.\./Resources/', text):
            fail(f"runtime PNG path reference: {path.relative_to(root)}")
        if 'AppContext.BaseDirectory, "Resources"' in text:
            fail(f"runtime PNG path reference: {path.relative_to(root)}")

    theme_loader = root / "src/Win7Taskbar/ThemeLoader.cs"
    if theme_loader.is_file():
        tl = theme_loader.read_text(encoding="utf-8")
        for name in (
            "DWMBorder.png",
            "win7search.png",
            "startwin7orb.png",
            "ActiveNormal.png",
            "win7taskbar.png",
        ):
            if name in tl:
                fail(f"ThemeLoader still names removed WPF PNG {name}")

    print(
        f"{len(key_set)} bundle assets, {len(doc_ids)} documented, "
        f"{len(NATIVE_SLICE_NAMES)} intentional native PNGs "
        f"(mirrored in src and root Resources), "
        f"{len(BUILD_INPUT_IMAGES)} build-input ICO, "
        f"{len(DOCUMENTATION_IMAGES)} documentation image(s)"
    )

    if errors:
        print("\n".join(errors))
        return 1

    print(
        "OK: Base64/PNG integrity, unique keys, documentation, XAML, "
        "native slices, build-input ICO exceptions, no undeclared runtime images"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
