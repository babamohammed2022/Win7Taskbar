"""Static contract checks for the Jump List trigger (v2.61).

The Jump List cannot be exercised in every development environment (no
Windows desktop, no Wine in the sandbox), so the parts of the interaction
that are pure structure are asserted here instead. Checks:

  - the arrow exists in the shared task button content template
    (Themes/Overrides.xaml): named, hit-testable, hover-bound, and the last
    child of the template root so it paints above the window-stack
    separators;
  - the C# side looks the arrow up by the SAME name and consumes the press
    (e.Handled) before the button can activate or reorder;
  - the trigger is the LEFT button only, and the right-click handler of a
    task button still contains no Jump List call at all;
  - the managed visibility probe and the click-outside purge speak the same
    language as the native popup (window class name, dismissal message);
  - every teardown site exists (group removed, reorder drag, theme swap,
    taskbar close), and the hover preview / tooltip keep their
    "one flyover at a time" guard.

Usage:  python3 tools/verify-jumplist-trigger.py
"""
from __future__ import annotations

from pathlib import Path
import re
import sys
import xml.etree.ElementTree as ET

root = Path(__file__).resolve().parents[1]
XAML = "{http://schemas.microsoft.com/winfx/2006/xaml}"
PRES = "{http://schemas.microsoft.com/winfx/2006/xaml/presentation}"

ARROW = "JumpListArrow"
failures: list[str] = []


def check(ok: bool, label: str, detail: str = "") -> None:
    print(("OK    " if ok else "FAIL  ") + label + (f" - {detail}" if detail and not ok else ""))
    if not ok:
        failures.append(label)


overrides = (root / "src/Win7Taskbar/Themes/Overrides.xaml").read_text(encoding="utf-8")
jumplist_cs = (root / "src/Win7Taskbar/TaskbarWindow.JumpList.cs").read_text(encoding="utf-8")
window_cs = (root / "src/Win7Taskbar/TaskbarWindow.xaml.cs").read_text(encoding="utf-8")
bridge_cs = (root / "src/Win7Taskbar/Interop/NativeBridge.cs").read_text(encoding="utf-8")
native_cpp = (root / "native/src/JumpListWindow.cpp").read_text(encoding="utf-8")

# --- the arrow in the shared content template -------------------------
tree = ET.fromstring(overrides)
template = None
for dt in tree.iter(f"{PRES}DataTemplate"):
    if dt.get(f"{XAML}Key") == "TaskButtonContentTemplate":
        template = dt
check(template is not None, "TaskButtonContentTemplate exists in Overrides.xaml")

arrow = None
if template is not None:
    for el in template.iter(f"{PRES}Border"):
        if el.get(f"{XAML}Name") == ARROW:
            arrow = el
check(arrow is not None, f"arrow element x:Name={ARROW} in the content template")

if arrow is not None:
    check(arrow.get("Background") == "Transparent",
          "arrow is hit-testable (Background=Transparent)",
          f"Background={arrow.get('Background')!r}")
    style = arrow.find(f"{PRES}Border.Style")
    trigger = None
    if style is not None:
        for st in style.iter(f"{PRES}Style"):
            for tg in st.iter(f"{PRES}DataTrigger"):
                binding = tg.get("Binding") or ""
                if "IsMouseOver" in binding and "AncestorType=Button" in binding:
                    trigger = tg
    check(trigger is not None,
          "arrow visibility is bound to the hover of the ancestor Button")
    grid = None
    for el in template.iter(f"{PRES}Grid"):
        if any(c is arrow for c in el):
            grid = el
            break
    check(grid is not None and list(grid)[-1] is arrow,
          "arrow is the last child of the template root (above the separators)")

# --- the C# trigger ----------------------------------------------------
check(f'"{ARROW}"' in jumplist_cs,
      "C# looks the arrow up by the same name (JumpListArrowName)")
check("TryBeginJumpListArrowPress" in window_cs and
      re.search(r"TaskButton_PreviewMouseDown[\s\S]{0,900}?TryBeginJumpListArrowPress",
                window_cs) is not None,
      "the press is armed inside TaskButton_PreviewMouseDown")
check(re.search(r"private bool TryBeginJumpListArrowPress[\s\S]{0,2400}?e\.Handled = true;",
                jumplist_cs) is not None,
      "the arrow press is consumed (e.Handled) before activation/reorder")
check(re.search(r"private bool TryBeginJumpListArrowPress[\s\S]{0,300}?"
                r"e\.ChangedButton != MouseButton\.Left", jumplist_cs) is not None,
      "the trigger answers the LEFT button only")
check("BeginPotentialJumpListDrag" not in window_cs and
      "BeginPotentialJumpListDrag" not in jumplist_cs,
      "the drag-up gesture that fought the icon reorder is gone")

right_click = re.search(r"private void TaskButton_MouseRightButtonUp[\s\S]{0,700}?\n        }",
                        window_cs)
check(right_click is not None and "JumpList" not in right_click.group(0),
      "the right-click handler contains no Jump List call")

# --- managed/native language parity ------------------------------------
m_class = re.search(r'JumpListPopupClass = "([^"]+)"', bridge_cs)
n_class = re.search(r"constexpr wchar_t kClassName\[\] = L\"([^\"]+)\"", native_cpp)
check(m_class is not None and n_class is not None and
      m_class.group(1) == n_class.group(1),
      "popup window class name matches between NativeBridge and the native core",
      f"{m_class.group(1) if m_class else None} != {n_class.group(1) if n_class else None}")
check("0x8000 + 0x177" in jumplist_cs and "WM_APP + 0x177" in native_cpp,
      "dismissal message constant matches the native kDismissOutsideMessage")

# --- teardown and one-flyover guards -----------------------------------
for site, pattern in (
    ("group removed", r"its group left the taskbar"),
    ("reorder drag", r"icon reorder drag started"),
    ("theme swap", r"theme swapped"),
    ("taskbar close", r"taskbar closing"),
):
    check(re.search(pattern, window_cs) is not None,
          f"teardown site present: {site}")
check(window_cs.count("IsJumpListGestureActive()") >= 2,
      "hover preview and tooltip keep the one-flyover guard")

print()
if failures:
    print(f"RESULT: {len(failures)} check(s) failed")
    raise SystemExit(1)
print("RESULT: the jump list trigger contract holds")
