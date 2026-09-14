# Feature Status

This document tracks the current feature status of Win7Taskbar and the main areas that still need improvement.

## Overall accuracy

Win7Taskbar currently has **high overall Windows 7 visual and behavioral accuracy**, but it is not a complete reproduction of every Windows 7 taskbar feature.

Some parts are already close to the original Windows 7 experience, while other parts are still being implemented or refined.

## Current status

| Feature | Status | Notes |
|---|---|---|
| Windows 7 taskbar layout | ✅ | The main taskbar layout is already close to Windows 7. |
| Start button / Windows orb | ✅ | The Windows 7-style Start orb is present with normal, hover, and pressed states. |
| Pinned applications | ✅ | Pinned taskbar applications are supported. |
| Application grouping | ✅ | Grouped task buttons are supported as part of the Windows 7-style Superbar behavior. |
| Application icons | ✅ | The general appearance is accurate, but icon accuracy is not yet complete for every application and system icon. |
| Open-application indicators | ⚠️ | Active/running-state indicators work. The multi-window "stacked" separator lines (the vertical bars shown when a group has 2+ windows) are bound into the button template as a right-aligned overlay of the native soft-edged strip image (1 line at 2 windows, 2 lines at 3+, clamped - no case above 3). v1.7.6 shifts every line +2.5% of the button width to the right, and the OUTER line a further +3% when more than 2 windows are open. Pending a look on real hardware at various DPIs. |
| Application tooltips | ✅ | Application-name tooltips are available. |
| File drag & drop onto taskbar buttons | ⚠️ | Dropping a file onto a pinned/running app button to open it with that app (hover-to-activate + drop) has an initial implementation: standard WPF drag&drop (no COM `IDropTarget` needed, since this isn't injected into explorer.exe), with a fallback to `ShellExecute` when the known executable can't be launched directly, and an extension check (via registry `SupportedTypes`, permissive when unknown) driving the allowed/forbidden cursor feedback. Needs real-world testing (multi-file drops, apps without declared `SupportedTypes`, mixed-extension drops). |
| Thumbnail previews (DWM) | ⚠️ | The hover preview popup is implemented again with LIVE DWM thumbnails (`TaskThumbnail` + `DwmRegisterThumbnail`), with a positive 250 ms confirmation and an icon fallback when the compositor cannot prove the paint. v1.7.6 makes the DWM destination rectangle match the laid-out thumbnail area exactly and re-fits the popup's Win32 window to its content so no bare popup surface can show beside the Aero frame (the reported white strip). Not yet verified on a real Windows desktop at 100-200% scale. |
| Jump Lists | ⚠️ | Windows 7-style Jump Lists are implemented as a dedicated subsystem (left-button press + drag-up on a task button opens the list; releasing the button over a row activates it). The data comes from the real Shell APIs (`IApplicationDocumentLists` + the window/shortcut `AppUserModelID`), never from invented entries, and the right-click menu is unchanged. The popup is a native window with DPI-scaled geometry. Not yet verified against a real Windows desktop at every scale, so it is not marked complete. |
| Windows 7 toolbars | ✅ | The three Windows 7-style toolbars are present. |
| Notification area | ⚠️ | The notification area is implemented, but support for all modern Windows tray states is still partial. v1.7.6: per-icon behavior preferences moved from the legacy registry key to `trayicons.ini` (zero-footprint); the old key is imported and deleted on first run. |
| Notification Area Icons page | ⚠️ | The Windows 7 "Select which icons and notifications appear on the taskbar" page is re-implemented as a fully proprietary native dialog (`TrayCplDialog.cpp`, template in `resources/app.rc`): two-column list, three-state in-place combo per row, "Always show all icons" checkbox, "Turn system icons on or off" page and "Restore default icon behaviors" link; changes apply to the tray model immediately, Cancel rewinds. It controls ONLY this tray, never Windows' own, uses NO registry and appears in no Control Panel list. Openable from the clock/taskbar menu, the Properties *Customize...* button and the overflow link. Not yet verified on real Windows. |
| Windows 11 system tray support | ⚠️ | Windows 11 system tray support is implemented, but some tray icons are recreated because Windows 11 no longer exposes all classic tray elements directly. |
| Tray overflow | ✅ | The overflow experience is reasonably close to Windows 7, although further refinement is possible. |
| Notification area icon configuration page | ⚠️ | Implemented as a fully in-process native page (`TrayCplDialog` + a hand-written `resources/app.rc` dialog template, `trayicons.ini` storage): no `.cpl` host, no CLSID and no registry registration at all - the registry-free alternative from the spec - so the page never appears in the Control Panel and deleting the program leaves nothing behind. It lists only Win7Taskbar-managed icons (three-state behavior combo per row), the always-show checkbox, a system-icons on/off page and a restore-defaults link; applied live, Cancel rewinds. Openable from the tray overflow "Customize..." link, the clock/taskbar menu entry and the Properties button. Pending hardware verification. |
| Battery indicator | ⚠️ | Battery status is implemented with a recreated taskbar icon, but the implementation is still partial rather than a complete native Windows 7 battery implementation. |
| Clock and date display | ✅ | The taskbar clock and date are present. |
| Language switcher (input language flyout) | ⚠️ | Windows 7/8.1-style keyboard layout switcher (tray abbreviation + popup, ported from the "Windows 7/8.1 Language Switcher Restorer" Windhawk mod) is implemented in source (`LanguageSwitcher.cpp`, dedicated native thread, SEH-guarded popup). The export mismatch that broke shipped alphas (missing `W7T_LangSwitcher*` in `dist/Win7TaskbarCore.dll`) is resolved by the refreshed `dist/` built with `check-exports.py` passing (104/104). The Windows 7 and Windows 8.1 skins both work in the shipped code; runtime behavior on a real desktop still needs a final verification pass. The "Windows 10/11" indicator style is hidden from the Properties combo (v1.7.6); a previously saved 3 stays working until the next Properties apply, which normalizes it to Windows 8.1. |
| System flyouts | ✅ | The main flyouts work, but positioning and some Windows-version-specific behavior still need improvement. |
| Clock flyout | ✅ | The Windows 7-style clock flyout is now considered complete. |
| Aero Peek / Show Desktop | ⚠️ | Windows 7-style Aero Peek and the Show Desktop area are represented, but the implementation is not yet a complete recreation of the original shell behavior. |
| Context menus | ✅ | Context menus are generally close to the Windows 7 behavior and appearance, with some details still to improve. |
| Taskbar Properties | ✅ | A Windows 7-style Properties interface is available, although some options and behaviors can still be refined. |
| Taskbar positioning | ⚠️ | Taskbar positioning is supported in the current implementation, but edge-specific behavior still requires refinement. |
| Taskbar rotation | ❌ | Rotating the taskbar to other screen edges is not fully implemented. |
| Taskbar size / configuration | ⚠️ | Taskbar configuration is available through Properties, but full Windows 7 parity for all size and layout options is not guaranteed. |
| Taskbar locking | ⚠️ | Taskbar locking/configuration behavior is not yet guaranteed to match Windows 7 in every case. |
| Explorer restart / tray recovery | ❌ | Recovery and consistent tray/icon state after Explorer restarts and shell changes still require further work. |

## Main missing features

### Taskbar rotation

Taskbar rotation to the top, left, or right side of the screen is still missing.

### Thumbnail previews

Windows 7-style **taskbar thumbnail previews** are back in source with live
DWM thumbnails (see the table row above); what remains is verification on
real hardware - especially multi-monitor and non-100% scales - rather than
implementation.

### Complete Windows 11 system tray support

Windows 11 uses a substantially different system tray architecture from Windows 7. Current support is implemented, but it is **still partial** and not yet complete enough to be considered finished.

The remaining work includes improving reliability, handling all shell states and Explorer restarts, and making tray icon discovery and updates consistently complete.

### Battery indicator

The battery indicator is implemented using a recreated taskbar icon. Further work is still needed for complete Windows 7 parity and robust handling of every battery state.

### Multi-window stacked indicator bars

The vertical separator lines that Windows 7 draws next to a grouped task button's icon when it holds 2 or more windows are not visible yet. The rendering logic itself (how many separators to show, and where to offset them based on button width and window count) is fully implemented as WPF value converters, but the actual visual elements referencing those converters were never added to the task button's control template. This is a missing-markup issue, not a missing-asset issue — no icon or image is involved in this feature.

### Language switcher

The Windows 7/8.1-style input language switcher (tray abbreviation such as "ITA"/"ENG" plus the native popup for picking a keyboard layout) is implemented in the native core and wired up on the managed side, but the currently shipped `Win7TaskbarCore.dll` in alpha builds does not export the four functions this feature depends on. This points to a stale native build that predates the language-switcher code being added, not a logic bug in the feature itself. Rebuilding the native core and verifying the export table before packaging should resolve it.

### Jump Lists (implemented, pending hardware verification)

The Jump List subsystem reproduces the Windows 7 interaction: press and hold the left button on a
taskbar button, drag **up** past the system drag threshold, and the list opens above the button;
moving the cursor through it highlights a row, and releasing the left button activates the row under
the cursor. A press without a qualifying drag behaves exactly like before (normal activation,
grouping, picker, hover, tooltip), and the right-click keeps only the Windows 7 context menu
(never a Jump List command).

Implementation notes:

* The gesture is a small state machine (`TaskbarWindow.JumpList.cs`) using normal WPF mouse capture
  and manual hit-test forwarding, the same mechanism the tray drag uses - no global mouse hooks.
* The popup is a native no-activate window with the shared Aero flyout border
  (`native/src/JumpListWindow.cpp`). All coordinates crossing the interop boundary are screen
  physical pixels; the popup geometry is scaled by the DPI of the monitor under the button.
* Entries come only from the public Shell read APIs for jump list data
  (`IApplicationDocumentLists`, recent + frequent automatic destinations). Application identity is
  the window's `AppUserModelID` from the shell property store, else the pinned shortcut's metadata,
  else the default id Windows derives from the executable path. If the Shell exposes no list for an
  application, no document rows are shown - nothing is ever fabricated.
* Failure handling: every Shell/COM call is wrapped in try/catch with logging (managed
  `DiagnosticLogger`, category `JUMPLIST`) and controlled cancellation; native resources are RAII
  owned and hard faults are contained by the project's portable SEH barrier. A jump list failure
  can never take the taskbar down.

Remaining work before this is marked ✅: verification on a real Windows 10/11 desktop at
100/125/150/200% scaling and on a mixed-DPI multi-monitor setup.

## Areas that are already in good shape

- The Windows 7-style Start orb and main taskbar layout are present.
- The Jump List gesture and its Shell data path are implemented (see above; hardware verification
  still pending).
- Pinned and grouped task buttons are supported.
- The three Windows 7-style toolbars are present.
- Context menus are generally good.
- The overflow experience is generally good.
- Flyouts are generally functional and visually close to the target.
- The Windows 7-style clock flyout is now considered complete.
- Application-name tooltips are available.
- Overall Windows 7 accuracy is already fairly high.

## Known flyout issues

The main flyout system is functional, but some non-clock flyouts may still need refinement in positioning and Windows-version-specific behavior.

The clock flyout itself is considered complete.

## Accuracy limitations

Win7Taskbar is a best-effort reimplementation of the Windows 7 taskbar. Modern Windows versions do not expose all of the same shell functionality that existed in Windows 7, so some features must be recreated using different APIs or approximated.

For this reason, high visual and behavioral accuracy is the goal, rather than claiming 100% feature parity with the original Windows 7 taskbar.
