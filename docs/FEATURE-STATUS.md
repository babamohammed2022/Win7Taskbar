# Feature Status

This document tracks the current feature status of Win7Taskbar and the main areas that still need improvement.

## Overall accuracy

Win7Taskbar currently has **high overall Windows 7 visual and behavioral accuracy**, but it is not a complete reproduction of every Windows 7 taskbar feature.

Some parts are already close to the original Windows 7 experience, while other parts are still being implemented or refined.

## Current status

| Feature | Status | Notes |
|---|---|---|
| Windows 7 taskbar layout | ✅ | The main taskbar layout is already close to Windows 7. |
| Windows 7 toolbars | ✅ | The three Windows 7-style toolbars are present. |
| Application icons | ✅ | The general appearance is accurate, but icon accuracy is not yet complete for every application and system icon. |
| Open-application indicators | ✅ | Windows 7-style indicators are present and can be refined further. |
| Context menus | ✅ | Context menus are generally close to the Windows 7 behavior and appearance, with some details still to improve. |
| Overflow | ✅ | The overflow experience is reasonably close to Windows 7, although further refinement is possible. |
| Flyouts | ✅ | The main flyouts work, but positioning still needs improvement. |
| Clock flyout | ❌ | The clock flyout position needs to be made more accurate. On Windows 11, the native Windows 10 flyout can sometimes appear first and must be prevented or bypassed. |
| Windows 11 system tray | ❌ | The Windows 11 system tray requires further work for reliable and complete handling of all tray icons and shell states. |
| Thumbnail previews | ❌ | Windows 7-style taskbar thumbnail previews are not implemented yet. |
| Taskbar rotation | ❌ | Rotating the taskbar to other screen edges is not implemented yet. |

## Main missing features

### Taskbar rotation

Taskbar rotation to the top, left, or right side of the screen is still missing.

### Thumbnail previews

Windows 7-style **taskbar thumbnail previews** are still missing. This includes the preview experience shown when hovering over an open application button.

### Complete Windows 11 system tray support

Windows 11 uses a substantially different system tray architecture from Windows 7. Current support works, but it is **not yet complete or robust enough to be considered finished**.

The remaining work includes improving reliability, handling shell changes and Explorer restarts, and making tray icon discovery and updates consistently fast.

## Areas that are already in good shape

- The three Windows 7-style toolbars are present.
- Context menus are generally good.
- The overflow experience is generally good.
- Flyouts are generally functional and visually close to the target.
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
