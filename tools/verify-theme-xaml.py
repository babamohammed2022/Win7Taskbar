"""Validate the runtime theme dictionaries before they are shipped.

Checks:
  - every theme XAML file parses (the two copies of Windows 7 / Windows 8.1)
  - the src/ and root/ copies of each theme are byte-identical
  - every TargetName / Storyboard.TargetName resolves to an x:Name declared
    inside the same template (or an enclosing one)
  - no x:Name appears twice inside the same template
  - no stray text appears where an element is expected (an opening tag
    replaced by a comment leaves its attributes behind as text: the XML stays
    well-formed, WPF fails at load with "Initialization of 'System.Windows.Setter'
    threw an exception")

The last two checks are the ones that would have caught the v1.21.24 startup
failure (a `<Setter TargetName="HoverTileImg">` whose element had lost its name).
"""
from __future__ import annotations

from pathlib import Path
import sys
import xml.etree.ElementTree as ET

root = Path(__file__).resolve().parents[1]

XAML_NS = "{http://schemas.microsoft.com/winfx/2006/xaml}"

# Element types whose content is a value written as text.
TEXT_VALUE_TAGS = frozenset({
    "String", "Char", "Boolean", "Byte", "Int16", "Int32", "Int64", "Single",
    "Double", "Decimal", "TimeSpan", "Duration", "Color", "Point", "Size",
    "Thickness", "CornerRadius", "FontFamily", "FontWeight", "FontStretch",
    "FontStyle", "GridLength", "Geometry", "PathFigure", "LineSegment",
    "PolyLineSegment", "BezierSegment", "QuadraticBezierSegment", "ArcSegment",
})

TEMPLATE_TAGS = frozenset({"ControlTemplate", "DataTemplate", "ItemsPanelTemplate"})

THEMES = [
    ("Windows 7", root / "Themes/Windows7.xaml",
     root / "src/Win7Taskbar/Themes/Windows7.xaml"),
    ("Windows 8.1", root / "Themes/Windows8.1.xaml",
     root / "src/Win7Taskbar/Themes/Windows8.1.xaml"),
]


def local(tag: str) -> str:
    return tag.split("}")[-1]


def declared_name(node) -> str | None:
    """x:Name and Name both register in the template's name scope."""
    return node.get(XAML_NS + "Name") or node.get("Name")


def collect_names(element) -> set[str]:
    names = set()
    for node in element.iter():
        name = declared_name(node)
        if name:
            names.add(name)
    return names


def scan(path: Path) -> list[str]:
    problems: list[str] = []

    try:
        tree = ET.parse(path)
    except ET.ParseError as exc:
        return [f"XML non valido: {exc}"]

    root_element = tree.getroot()

    # Nested templates: a name declared in an outer template is not reachable
    # from the inner one, so the scope list is the chain of enclosing
    # templates, innermost first.
    def walk(element, scopes: list[set[str]], scope_names: dict[int, list[str]]):
        tag = local(element.tag)

        if tag not in TEXT_VALUE_TAGS:
            if element.text and element.text.strip():
                problems.append(f"testo dove serve un elemento: "
                                f"{element.text.strip()[:70]!r}")
            for child in element:
                if child.tail and child.tail.strip():
                    problems.append(f"testo dove serve un elemento: "
                                    f"{child.tail.strip()[:70]!r}")

        if tag in TEMPLATE_TAGS:
            names = collect_names(element)
            duplicates = sorted({n for n in names
                                 if len([1 for node in element.iter()
                                         if declared_name(node) == n]) > 1})
            for duplicate in duplicates:
                problems.append(f"x:Name duplicato nel template: {duplicate}")
            scopes = [names] + scopes
            scope_names[id(element)] = names

        for attribute in ("TargetName", "Storyboard.TargetName"):
            target = element.get(attribute)
            if target and scopes and not any(target in scope for scope in scopes):
                problems.append(f"TargetName '{target}' non esiste nel template")

        for child in element:
            walk(child, scopes, scope_names)

    walk(root_element, [], {})
    return problems


def main() -> int:
    failed = False

    for label, root_copy, src_copy in THEMES:
        for path in (root_copy, src_copy):
            if not path.is_file():
                failed = True
                print(f"ERRORE {label}: file mancante: {path.relative_to(root)}")
                continue
            problems = scan(path)
            if problems:
                failed = True
                print(f"ERRORE {label}: {path.relative_to(root)}")
                for problem in problems:
                    print(f"   - {problem}")
            else:
                print(f"OK    {label}: {path.relative_to(root)}")

        if root_copy.is_file() and src_copy.is_file():
            if root_copy.read_bytes() != src_copy.read_bytes():
                failed = True
                print(f"ERRORE {label}: le due copie del tema differiscono "
                      f"({root_copy.relative_to(root)} vs {src_copy.relative_to(root)})")
            else:
                print(f"OK    {label}: le due copie del tema sono identiche")

    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
