# Third-Party Notices

Win7Taskbar is licensed under the GNU General Public License v3 or later.

## Windhawk — Taskbar jump list on cursor pos

Evaluated as positioning inspiration; adopted in the v2.62-alpha and
DISCARDED in the final v2.62 (no derived rule or code remains in the
released build):

- Mod: **Taskbar jump list on cursor pos** (`taskbar-jump-list-on-cursor-pos`)
- Author: **m417z**
- Source: https://github.com/ramensoftware/windhawk-mods/blob/main/mods/taskbar-jump-list-on-cursor-pos.wh.cpp
- Mod page: https://windhawk.net/mods/taskbar-jump-list-on-cursor-pos
- License of the referenced mod: **GNU General Public License v3.0** (stated
  in the file header; GPL-3.0 is compatible with this project's GPL-3.0-or-later)

What was evaluated: the mod's core rule for jump-list positioning - the list
anchored to the **mouse cursor** instead of the taskbar button (the mod
implements it by hooking `CTaskListWnd::_ComputeJumpViewPosition` and
setting the position's X to the cursor's X, read from `GetMessagePos`).
Win7Taskbar cannot hook taskbar.dll the same way (it has its own popup, not
Windows' jump view), so the v2.62-alpha applied the rule to its own window
in `native/src/JumpListWindow.cpp` (`DragMove`): the popup re-centered on
the cursor along the taskbar axis, clamped to the work area, on every move
of the drag that opened it. The alpha test showed that this position does
not match the real Windows 7 shell, which opens its jump view next to the
button (left-aligned above it), so the final v2.62 removed the
cursor-following re-anchoring entirely and the popup now keeps the
canonical button-anchored placement. The hook code itself (Windhawk/PEB
plumbing, taskbar.dll symbol hooking) was NEVER copied: it does not apply
to a standalone taskbar.

## Windhawk — Taskbar classic context menu

Reference / inspiration:

- Mod: **Taskbar classic context menu** (`taskbar-classic-menu`)
- Author: **m417z**
- Source repository: https://github.com/m417z/my-windhawk-mods
- Mod page: https://windhawk.net/mods/taskbar-classic-menu
- License of the referenced mod: **GNU General Public License v3.0**

Win7Taskbar does **not** copy the source code of this mod. Its taskbar
lifecycle/compatibility handling was studied as a reference for conservative
Windows-version detection, late taskbar component availability, and retrying
when Explorer's taskbar is rebuilt.

The corresponding Win7Taskbar implementation is an independent
reimplementation using Win32/UI Automation mechanisms already present in the
project.

## Windhawk — Separate System Tray Icons

Reference / inspiration:

- Mod: **Separate System Tray Icons** (`separate-system-tray-icons`)
- Project: **Windhawk**

The Win7Taskbar Windows 11 tray refresh guard also independently reimplements
the general idea of rediscovering disposable taskbar/XAML state after Explorer
rebuilds. No Windhawk source code is copied for that mechanism.

## Methodological provenance (native module integration)

The native integration layer in `native/src/` (rotation, resizing, tray
layout, registry policy) was written as a clean-room implementation from
public documentation (MSDN `SHAppBarMessage`, `Shell_NotifyIconW`,
`ITaskbarList3`, `SetWindowSubclass`) plus architectural intent extracted
from prior analysis notes kept in `docs/`. No decompiled code, symbol
names, addresses, vtable offsets or message numbers from third-party
binaries were copied into the codebase; the design notes that did derive
from such analysis are confined to `docs/native-module-notes.md` and
carry no build/run significance. Snippet material supplied for the
integration used the `w7tb` namespace and was renamed/adapted to this
project's `w7t` conventions; the snippet's vtable-offset map and its
references to undocumented COM interfaces were deliberately not carried
over (see the deviations section of the notes).

## Open-Shell-Menu (inspiration only)

Reference / inspiration for Start Menu pin storage, small folder icons,
IContextMenu usage, and All Programs tree measurements. Since v3.15, two
more techniques are taken as inspiration (reimplemented as original code,
following documented shell APIs and Open-Shell's design):

- Start Menu search icons are resolved through `IShellItemImageFactory`
  at every row size (16 px included) as a final fallback, the same way
  Open-Shell's item icons are always obtained from the shell rather than
  from legacy `ExtractIcon` paths.
- Control Panel / Settings catalog entries whose absolute parsing name
  is unavailable are kept (instead of dropped) by composing the parent's
  parsing name with the child's relative parsing name
  (`SIGDN_PARENTRELATIVEPARSING`), mirroring how the Open-Shell search
  maintains its Settings catalog, and verified by an actual
  `SHCreateItemFromParsingName` round-trip.

- Project: **Open-Shell-Menu**
- Source: https://github.com/Open-Shell/Open-Shell-Menu
- License: **MIT License**
- Copyright: Copyright (c) 2017-2018 Open-Shell
  (see https://github.com/Open-Shell/Open-Shell-Menu/blob/master/LICENSE)

Win7Taskbar does **not** copy Open-Shell source code, assets, or bitmaps.
Public shell APIs (`IContextMenu`, `SHGetFileInfo` with `SHGFI_SMALLICON`,
`ShellExecuteEx`) and published measurements were used as inspiration.
Start Menu pins are written only to `%AppData%\Win7Taskbar\Pinned\StartMenu`;
Explorer's User Pinned folder is never written.

## EJSnow / Windows-7-skin (inspected, not copied)

- Source: https://github.com/EJSnow/Windows-7-skin
- Inspected: 2026-09-24
- License: **none published** (no LICENSE file; GitHub default is all rights reserved)
- README states start-button images and a reflection bitmap were **extracted
  from Windows 7 / Windows 7 Professional SP1**. Those Microsoft assets are
  not copied.

Because the repository has no license and contains Microsoft-extracted
bitmaps, Win7Taskbar copies **no files, bitmaps, icons, or skin text** from
it. Search-time full-width white pane (outer frame unchanged) is a well-known
Windows 7 Start Menu behaviour, also described by Open-Shell's public skin
keys (`Main_bitmap_search`); it was reimplemented in our XAML from scratch.

MIT License text (verbatim from the Open-Shell LICENSE file):

```
MIT License

Copyright (c) 2017-2018 Open-Shell

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

---

## Windows 11 native-tray research credits (Phase 0, 2026-09)

The recon for consuming the Windows 11 tray without ExplorerPatcher
(recon document `docs/WIN11-NATIVE-TRAY-RESEARCH.md`, probe in
`tools/win11-native-tray-probe/`) stands on publicly available
research. Nothing is shipped from these references; our probe and our
readers are original Win7Taskbar code. The references remain credited
because their structural findings (which window chains are alive on
which build, how the legacy tray struct is laid out, what the XAML
taskbar refuses) inform the feasibility matrix:

* **Windhawk mods by m417z** (GPL-3.0, license-compatible research):
  `taskbar-notification-icon-spacing`, `taskbar-tray-system-icon-tweaks`,
  `windows-11-taskbar-styler`, `win10-taskbar-on-win11-24h2` and its
  fix-mods (repository `ramensoftware/windhawk-mods`).
  Source: https://github.com/ramensoftware/windhawk-mods
* **mnotify** by blendonl (the balloon-ownership and `TaskbarCreated`
  re-registration waypoints): https://github.com/blendonl/mnotify
* **KRR1751, "Windows 11' SECRET Taskbar!"** — community video that
  documented the legacy taskbar revival on Windows 11 24H2 and the
  need to keep the XAML overlay windows alive while the classic
  deskbands are shown. **The accompanying ``win32-classic-taskbar-revival``
  mod source is unlicensed: it is treated here strictly as a
  documentation/idea reference; Win7Taskbar contains zero lines from
  it.**
* **RetroBar / ManagedShell** (Apache-2.0) — the existing attribution
  for `TrayService`-style enumeration and `NotifyIconList` carries over;
  see the main section above.

