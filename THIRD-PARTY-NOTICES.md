# Third-Party Notices

Win7Taskbar is licensed under the GNU General Public License v3 or later.

## Windhawk — Taskbar jump list on cursor pos

Adopted idea / code reference (since v2.62):

- Mod: **Taskbar jump list on cursor pos** (`taskbar-jump-list-on-cursor-pos`)
- Author: **m417z**
- Source: https://github.com/ramensoftware/windhawk-mods/blob/main/mods/taskbar-jump-list-on-cursor-pos.wh.cpp
- Mod page: https://windhawk.net/mods/taskbar-jump-list-on-cursor-pos
- License of the referenced mod: **GNU General Public License v3.0** (stated
  in the file header; GPL-3.0 is compatible with this project's GPL-3.0-or-later)

What was taken: the mod's core rule for jump-list positioning - the list is
anchored to the **mouse cursor** instead of the middle of the taskbar group
(the mod implements it by hooking `CTaskListWnd::_ComputeJumpViewPosition`
and setting the position's X to the cursor's X, read from
`GetMessagePos`). Win7Taskbar cannot hook taskbar.dll the same way (it has
its own popup, not Windows' jump view), so the rule is applied to its own
window in `native/src/JumpListWindow.cpp` (`DragMove`): the popup re-centers
on the cursor along the taskbar axis and is clamped to the work area, on
every move of the drag that opened it, instead of staying glued to the
button's left edge. The hook code itself (Windhawk/PEB plumbing, taskbar.dll
symbol hooking) was NOT copied: it does not apply to a standalone taskbar.

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
