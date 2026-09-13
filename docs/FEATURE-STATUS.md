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
| Notification area | ❌ | The notification area is present, but complete and robust handling of all modern Windows tray states is not finished. |
| Windows 11 system tray support | ❌ | The Windows 11 system tray requires further work for reliable and complete handling of all tray icons and shell states. |
| Tray overflow | ✅ | The overflow experience is reasonably close to Windows 7, although further refinement is possible. |
| Battery indicator | ⚠️ | Battery status is represented by a recreated taskbar icon rather than being a complete native Windows 7 battery implementation. |
| Clock and date display | ✅ | The taskbar clock and date are present. |
| System flyouts | ✅ | The main flyouts work, but positioning and some Windows-version-specific behavior still need improvement. |
| Clock flyout | ❌ | The clock flyout position needs to be made more accurate. On Windows 11, the native Windows 10 flyout can sometimes appear first and must be prevented or bypassed. |
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

Windows 11 uses a substantially different system tray architecture from Windows 7. Current support works, but it is **not yet complete or robust enough to be considered finished**.

The remaining work includes improving reliability, handling shell changes and Explorer restarts, and making tray icon discovery and updates consistently fast.

## Areas that are already in good shape

- The Windows 7-style Start orb and main taskbar layout are present.
- Pinned and grouped task buttons are supported.
- The three Windows 7-style toolbars are present.
- Context menus are generally good.
- The overflow experience is generally good.
- Flyouts are generally functional and visually close to the target.
- Application-name tooltips are available.
- Overall Windows 7 accuracy is already fairly high.

## Known flyout issues

The flyout system still needs refinement, especially for positioning.

The clock flyout requires additional work to:

1. position the flyout more accurately relative to the clock icon;
2. prevent the Windows 10 native flyout from appearing before the Win7Taskbar flyout on Windows 11;
3. keep the behavior consistent when the taskbar moves, the DPI changes, or the taskbar is on another monitor.

## Accuracy limitations

Win7Taskbar is a best-effort reimplementation of the Windows 7 taskbar. Modern Windows versions do not expose all of the same shell functionality that existed in Windows 7, so some features must be recreated using different APIs or approximated.

For this reason, high visual and behavioral accuracy is the goal, rather than claiming 100% feature parity with the original Windows 7 taskbar.
