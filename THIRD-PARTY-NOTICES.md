# Third-Party Notices

Win7Taskbar is licensed under the GNU General Public License v3 or later.

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
