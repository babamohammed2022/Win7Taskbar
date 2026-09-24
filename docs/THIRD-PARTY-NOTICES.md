# License and third-party credits

Win7Taskbar is released under the **GNU General Public License v3.0** (see the
[`LICENSE`](./LICENSE) file for the full text).

Every source file in this repository carries the corresponding SPDX-style
notice: `GPL-3.0-or-later`.

## Why GPL-3.0

This project incorporates, derives from, or is inspired by code coming from
sources under different licenses:

- **[RetroBar](https://github.com/dremin/RetroBar)** — Apache License 2.0
  (permissive). Win7Taskbar started as a fork of RetroBar: the XAML theme
  structure, the `RetroBar.*` public surface required by that theme, and the
  algorithms behind several taskbar features come from there.
- **[ExplorerPatcher](https://github.com/valinet/ExplorerPatcher)** — GNU
  General Public License v2 (the source files carry no explicit "or later"
  header, so the "any version ever published by the FSF" clause of the GPL
  applies). The parts of Win7Taskbar that invoke the native immersive system
  flyouts (network, clock, battery, volume) are adapted from the logic of
  `ExplorerPatcher/ImmersiveFlyouts.c` and `ImmersiveFlyouts.h`.
- **[ManagedShell](https://github.com/cairoshell/ManagedShell)** — Apache
  License 2.0. Reference for the notification-area algorithms (tray icon
  enumeration through Explorer's toolbar, immersive shell interop).
- **[Windows7RetrobarTheme](https://github.com/babamohammed2022/Windows7RetrobarTheme)**
  — the Windows 7 theme (`Themes/Windows7.xaml` plus the PNG assets under
  `Resources/`), included unmodified. It is itself a derivative work of
  RetroBar and remains under the Apache License 2.0.
- **[Windows 7 Network Flyout Recreation](https://github.com/ramensoftware/windhawk-mods/blob/main/mods/win7-network-flyout-recreation.wh.cpp)**
  (Windhawk mod v5.0.0, author babamohammed) — MIT license (mods without an
  explicit license in the windhawk-mods repository are published under MIT,
  per that repository's submission policy). Ported into
  `native/src/Win7NetworkFlyout.cpp` (with the Windhawk loading/hooking
  plumbing and the Control Panel parts removed); all UI logic, strings and
  embedded graphic resources are preserved from the original. MIT is
  compatible with GPL-3.0. Copyright (c) babamohammed.
- **[Windows 7/8.1 Action Center Recreation](https://github.com/ramensoftware/windhawk-mods/blob/main/mods/win7-action-center-recreation.wh.cpp)**
  (Windhawk mod v2.2.0, author babamohammed) — MIT license (mods without an
  explicit license in the windhawk-mods repository are published under MIT,
  per that repository's submission policy). From this mod, v2.37 reuses ONLY
  two graphic assets, embedded as base64 in
  `native/src/SearchAssets.inc`: the UAC shield icon in its two variants
  (native 16x16 and 64x64 source) shown next to "Esegui come
  amministratore" in the app search window, together with the variant
  selection rule (draw size <= 16 px uses the native 16x16 asset,
  otherwise the 64x64 source is scaled). No other code or text from the
  mod was taken. MIT is compatible with GPL-3.0. Copyright (c)
  babamohammed.

- **[Windows 7/8.1 Language Switcher Restorer](https://windhawk.net/mods/win7-language-switcher-restorer)**
  (Windhawk mod v1.1.0, author babamohammed — the same author as this
  project) — GNU General Public License v3.0. Ported into
  `native/src/LanguageSwitcher.cpp` (v1.4): layout enumeration and
  switching (`KeyboardLayoutItem`, `RefreshKeyboardLayouts`,
  `SwitchToLayout`, `FindKlidByLayoutId`, `GetSubstituteKlid`,
  `GetLayoutDisplayName`, `GetLangAbbrev`, `g_LangAbbrevs`,
  `FormatWin7LayoutItemText`), the two popup renderings
  (`PaintWin7Menu`, `PaintWin8Flyout`, the GDI+/GDI checkmark), the 25
  languages footer table (`kLocalizedStrings`), tray-anchored
  multi-monitor placement (`PositionWindowNearTray`) and the popup window
  procedure (hover, click, keyboard, auto-hide on deactivation). NOT
  ported, because it only exists in an injected module: the low-level
  keyboard/mouse hooks, the tray-control subclassing, the ShowWindow hook,
  the "am I the main shell?" logic and the unload infrastructure. The
  popup window class is renamed (`W7T_LangSwitcherFlyout`) so the mod and
  this port can coexist.
- **[Taskbar jump list on cursor pos](https://windhawk.net/mods/taskbar-jump-list-on-cursor-pos)**
  (Windhawk mod v1.0, author m417z,
  [source](https://github.com/ramensoftware/windhawk-mods/blob/main/mods/taskbar-jump-list-on-cursor-pos.wh.cpp))
  — GNU General Public License v3.0 (stated in the file header; compatible
  with this project's GPL-3.0-or-later). Evaluated as positioning
  inspiration for v2.62: the mod's rule anchors the jump list to the mouse
  cursor (a hook of `CTaskListWnd::_ComputeJumpViewPosition` setting the
  position's X to the cursor's X, from `GetMessagePos`). The v2.62-alpha
  applied the same rule to Win7Taskbar's own popup window in
  `native/src/JumpListWindow.cpp` (`DragMove`), on every move of the drag
  that opened the list; the alpha test verdict was that the position did
  not match the real Windows 7 shell (which opens the jump view next to the
  button, left-aligned above it), so the final v2.62 removed the
  cursor-following re-anchoring and the popup keeps the canonical
  button-anchored placement. No rule or code derived from the mod remains
  in the released build. The Windhawk hooking plumbing was never copied (it
  does not apply to a standalone taskbar).

**The Apache License 2.0 (RetroBar, ManagedShell) is not considered
compatible by the FSF with GPL-2.0**, because of the patent-termination and
indemnification clauses present in Apache 2.0 and absent from GPL-2.0. It is
instead explicitly compatible with **GPL-3.0** (see the FSF's license list and
GPL-3.0 §11 / Apache-2.0 compatibility statements).

In order to legitimately combine code derived from RetroBar (Apache-2.0) with
code derived from ExplorerPatcher (GPL) in a single distributed work,
Win7Taskbar therefore adopts **GPL-3.0** rather than GPL-2.0. The
ExplorerPatcher upstream, being "GPL v2 or any later version", permits this
relicensing of the derived portions; the Apache-2.0 portions may be combined
into a GPL-3.0 work and remain individually available under Apache-2.0.

## Specific attributions

### RetroBar

Copyright of the respective RetroBar authors. Original code distributed under
the Apache License 2.0. Derived portions keep their attribution in the
relevant source files. The full Apache-2.0 text is reproduced in the appendix
of [`CREDITS.txt`](./CREDITS.txt), as required by Apache-2.0 §4.

With one documented exception, no line of RetroBar's C# source code was
copied verbatim: the logic (Superbar grouping, jump lists, notification area,
AppBar behaviour) was reimplemented from scratch in native C++ under
`native/`. What was taken from RetroBar is: the XAML theme file (a
pre-existing derivative work, included unmodified), the public type/member
names required for XAML binding resolution, and the *algorithms* of the
features listed in `CREDITS.txt`.

**The exception (since v1.21.37):** the "start with Windows" (reversible)
option of the Properties window is a complete port of RetroBar's autostart
implementation, copied into `src/Win7Taskbar/Utilities/AutoStart.cs` from
`RetroBar/PropertiesWindow.xaml.cs` (`LoadAutoStart`,
`CbAutoStart_OnChecked`) and `RetroBar/Utilities/ExePath.cs`
(`GetExecutablePath`): state read from the value names of
`HKCU\Software\Microsoft\Windows\CurrentVersion\Run`, enabling writes the
quoted executable path as the `Win7Taskbar` value, disabling deletes it,
every step guarded by try/catch as in the original. The checkbox labels are
RetroBar's own "autostart" strings from its language files. The port remains
under the Apache License 2.0, attributed in `CREDITS.txt`.

### ExplorerPatcher

Copyright Valentin-Gabriel Radu (valinet) and contributors. Original code
distributed under the GPL. Affected files in Win7Taskbar:

| Win7Taskbar file | What was adapted | What was rewritten independently |
|---|---|---|
| `native/src/ImmersiveFlyouts.h` | COM interface shapes (`IExperienceManager`, `IShellExperienceManagerFactory`), the four flyout runtime names, the CLSID/IID values | C++ virtual-interface declarations instead of explicit C vtables; the RAII helpers; the public module API |
| `native/src/ImmersiveFlyouts.cpp` | The COM call sequence `CoCreateInstance(CLSID_ImmersiveShell)` → `IServiceProvider::QueryService(CLSID_ShellExperienceManagerFactory)` → `GetExperienceManager(name)` → `QueryInterface(IID)` → `ShowFlyout`/`HideFlyout`; use of `WindowsCreateStringReference`; the `CLSCTX_NO_CODE_DOWNLOAD \| CLSCTX_LOCAL_SERVER` context flags | Caching of the factory and of the experience managers between calls (upstream recreates and releases them on every invocation); the single retry after dropping the cache, which survives an Explorer restart; runtime resolution of `combase.dll` so the DLL still loads on Windows 7; error propagation through return values instead of nested `if (SUCCEEDED(hr))` blocks |
| `native/src/FlyoutLauncher.cpp` | — (delegates the immersive path to `ImmersiveFlyouts`) | Aero Clock fallback, flyout repositioning, COM apartment handling |
| `native/include/Win7TaskbarCore.h` | The numeric values of `W7T_FLYOUT_*` / `W7T_FLYOUT_SHOW|HIDE`, mirroring `INVOKE_FLYOUT_*` | Everything else |

The GUIDs themselves are interoperability identifiers for system COM
interfaces — factual data about the Windows API, not copyrightable creative
material — and are the same values that appear in ExplorerPatcher and in the
public declarations of ManagedShell.

One documented deviation from upstream: ExplorerPatcher declares
`GetExperienceManager(HSTRING* experience, ...)`, but passes an `HSTRING`;
Win7Taskbar uses the correct by-value signature. `ShowFlyout` is declared as
taking a pointer to `Windows.Foundation.Rect` (four floats), exactly as
upstream does — not a Win32 `RECT`.

### ManagedShell

Copyright Cairo Shell contributors. Apache License 2.0. Source of the
notification-area enumeration algorithm (`native/src/ExplorerTrayReader.cpp`)
and of the immersive-shell interop shape used before the ExplorerPatcher
adaptation. C++ code written from scratch; from the C# source only the
sequence of system calls and the COM identifiers were taken. Since v1.21.39
it is also the behavioural reference for the balloon notifications
(`NotificationBalloon` icon fallbacks and timeout rules,
`NotifyIcon.SendMessage` NIN_BALLOON* feedback with the version-4
wParam/lParam layout, the promotion of overflow icons while a balloon is
shown): original C# code in `src/Win7Taskbar/Controls/NotifyBalloon.xaml.cs`,
`Controls/BalloonHost.cs` and `TaskbarWindow.xaml.cs`, derived algorithm.

The balloon *pipeline* follows the same reference: a notification object with a
`Handled` flag raised through an event, the per-icon `MissedNotifications`
collection for a notification nobody showed, one visible balloon with the rest
waiting, and the promotion of an overflow icon for the balloon's lifetime. Taken
from `NotificationArea.handleBalloonData`, `NotifyIcon.TriggerNotificationBalloon`
/ `NotifyIcon.MissedNotifications` and `NotificationBalloonEventArgs`; original
C# code in `src/Win7Taskbar/Controls/NotificationBalloon.cs`, derived algorithm
(the queue bound, the drop-oldest policy, the expiry of a pending notification
and the RAII timer are this project's own, modelled on `CoreState::QueueEvent`
and `Utilities/TimerLease.cs`).

### Aero Tray (Windhawk mod)

Copyright aubymori (github.com/aubymori), GNU GPL-3.0 (license of the
Windhawk mod distribution). Reference for the tray overflow popup layout
adapted in `src/Win7Taskbar/TaskbarWindow.xaml(.cs)`: link-area height
(43 px at 96 DPI), link padding (16 px), icon-area padding (7 px), icon
spacing (18 px), fixed 3-icons-per-row grid, 8 px flyout offset from the
monitor edges, hover/pressed states of the "Personalizza..." link and the
Control-Panel namespace used to open the notification-area icons page.
Layout/behavior logic reimplemented in WPF/C# **without any hooking** (the
mod hooks explorer.exe internals, which Win7Taskbar does not have); the
symbol-hook part of the mod was deliberately not ported.

### Taskbar jump list on cursor pos (Windhawk mod)

Copyright m417z (github.com/m417z), GNU General Public License v3.0 (the
mod's file header states "Source code is published under The GNU General
Public License v3.0"). Evaluated (v2.62) as the source of an alternative
Jump List positioning: the mod anchors the jump list to the mouse cursor
instead of the taskbar button — it sets the jump-view position's X to the
cursor's X (from `GetMessagePos`) on every open. The v2.62-alpha applied
the same rule to Win7Taskbar's own popup window in
`native/src/JumpListWindow.cpp` (`DragMove`, called from the managed drag
gesture in `src/Win7Taskbar/TaskbarWindow.JumpList.cs`): the popup
re-centered on the cursor along the taskbar axis, clamped to the work area
of the monitor that hosts the button, on every move of the drag that opened
the list. The alpha test showed the position does not match the real
Windows 7 shell, which opens its jump view next to the button
(left-aligned above it), so the final v2.62 removed the cursor-following
re-anchoring and the popup keeps the canonical button-anchored placement;
no rule or code derived from the mod remains in the released build. The
hook itself (`WindhawkUtils::SYMBOL_HOOK` against `taskbar.dll`) was
deliberately NOT ported at any point: Win7Taskbar is a standalone taskbar
with its own popup window, so the Windhawk injection/hooking plumbing does
not exist here and cannot be reused. No line of the mod's C++ was ever
copied verbatim.

### ViGlance

Copyright lee-soft (github.com/lee-soft/ViGlance). Referenced (v2.3) for the
battery-refresh approach: when Explorer's legacy notification toolbar stays
frozen (Windows 10/11 XAML tray), the battery glyph is redrawn from the real
`GetSystemPowerStatus` state (AC line, percent) as in ViGlance's
force-refresh pattern and in the user-provided `ForceBatteryIconRefresh`
module. No ViGlance source code (VB6) was copied.

### ExplorerEx

Copyright kfh83 (github.com/kfh83/ExplorerEx). Inspiration (v2.3) for the
three shell toolbars (Address / Links / Desktop bands) implemented as a WPF
application layer over public shell APIs (`SHGetFileInfo`, `ShellExecute`).
No ExplorerEx source code was copied.

### Windows7RetrobarTheme

`Themes/Windows7.xaml` and the original PNG artwork (now stored byte-for-byte
as Base64 in `GraphicalResourceBundle.cs`, with 8 native Aero slices still
under `Resources/`) are included from the upstream theme. Credits
declared by the theme author: WinBoeing 777 (show-desktop button icon),
Traindere (supervision), 3Ds (inspiration).

### Language contributions

The translations of the interface are not third-party code: they are
contributions to this project, released under the same GPL-3.0-or-later
licence. The Arabic translation (`Languages/Arabic.xaml`,
`src/Win7Taskbar/Languages/Arabic.xaml` and the Arabic tables in
`native/src/Strings.cpp`) was contributed by MAHMOGAMER
(github.com/mahmogamer). `CREDITS.txt` lists the contributors; corrections to a
translation are welcome as a pull request on the single file involved.

### Other third-party components

| Component | Copyright | License |
|---|---|---|
| .NET 8 / WPF | Microsoft Corporation | MIT |

## How to keep this file up to date

Whenever a new portion of code is adapted or derived from an external source,
add an entry to this document containing:

- the name of the upstream project and its license;
- the affected Win7Taskbar files/functions;
- a short description of what was derived (logic, data structures, GUIDs,
  call sequences) and of what was instead rewritten independently.

`CREDITS.txt` holds the same information in Italian, in more detail, together
with the full Apache-2.0 text required for the Apache-licensed portions.
