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
| Open-application indicators | ✅ | Windows 7-style indicators are present and can be refined further. |
| Application tooltips | ✅ | Application-name tooltips are available. |
| Thumbnail previews | ❌ | Windows 7-style taskbar thumbnail previews are not currently implemented; the previous preview implementations were disabled because they were not reliable on real systems. |
| Jump Lists | ❌ | Jump Lists are not implemented yet. |
| Windows 7 toolbars | ✅ | The three Windows 7-style toolbars are present. |
| Notification area | ⚠️ | The notification area is implemented, but support for all modern Windows tray states is still partial. |
| Windows 11 system tray support | ⚠️ | Windows 11 system tray support is implemented, but handling of all tray icons and shell states is still partial and requires further refinement. |
| Tray overflow | ✅ | The overflow experience is reasonably close to Windows 7, although further refinement is possible. |
| Battery indicator | ⚠️ | Battery status is implemented with a recreated taskbar icon, but the implementation is still partial rather than a complete native Windows 7 battery implementation. |
| Clock and date display | ✅ | The taskbar clock and date are present. |
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

Windows 7-style **taskbar thumbnail previews** are still missing. This includes the preview experience shown when hovering over an open application button.

### Jump Lists

Windows 7-style **Jump Lists** are not implemented yet.

### Complete Windows 11 system tray support

Windows 11 uses a substantially different system tray architecture from Windows 7. Current support is implemented, but it is **still partial** and not yet complete enough to be considered finished.

The remaining work includes improving reliability, handling all shell states and Explorer restarts, and making tray icon discovery and updates consistently complete.

### Battery indicator

The battery indicator is implemented using a recreated taskbar icon. Further work is still needed for complete Windows 7 parity and robust handling of every battery state.

## Areas that are already in good shape

- The Windows 7-style Start orb and main taskbar layout are present.
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
