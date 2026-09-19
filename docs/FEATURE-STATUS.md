# Feature Status

This document tracks the current feature status of Win7Taskbar and the main areas that still need improvement.

## Overall accuracy

Win7Taskbar currently has high overall Windows 7 visual and behavioral accuracy, but it is not a complete reproduction of every Windows 7 taskbar feature.

Some parts are already close to the original Windows 7 experience, while other parts are still being implemented or refined. If there are any imprecisions or problems, please report them to the author of this software.

## Current status

| Feature | Status | Notes |
|---|---|---|
| Windows 7 taskbar layout | ✅ | The main taskbar layout is already close to Windows 7. |
| Start button / Windows orb | ✅ | The Windows 7-style Start orb is present with normal, hover, and pressed states. |
| Pinned applications | ✅ | Pinned taskbar applications are supported. |
| Application grouping | ✅ | Grouped task buttons are supported as part of the Windows 7-style Superbar behavior. |
| Taskbar-list compatibility (shell clients) | ⚠️ | **v1.21.32:** the taskbar window answers the private query `WM_USER + 236` that `ITaskbarList` clients send to the window of class `Shell_TrayWnd` (`HrInit` fails when it answers zero), and the shell-hook codes those clients then send are honoured as the documented `AddTab`/`DeleteTab`/`ActivateTab` effects. Toolkits that run this during window creation (tao/Tauri, Chromium/Electron, i.e. Windhawk and VS Codium) can therefore add and remove their own buttons instead of failing or waiting. Windows with an empty title are accepted when they carry `WS_CAPTION`, and a window whose title arrives later joins the bar on the name-change event instead of waiting for the next full enumeration. **Needs real-hardware confirmation** with Windhawk and VS Codium, which is what this PR's test build is for. |
| Color hot-track (hover accent) | ⚠️ | **v1.21.36:** the hover look of a task button is unchanged (tray hover tile + the theme's Aero glow); on top of it, a program that is **open** gets a **small spot** of its own colour, centred on the mouse, at **30%** strength. Pinned programs that are not running get no accent. History: v1.21.33 replaced the hover everywhere with a glossy tile at full strength (rejected), v1.21.34 cut it to 5% (too faint), v1.21.35 set the small spot at 12%, v1.21.36 raises the weight to 30% on the user's request while keeping the small spot. **Needs real-hardware confirmation** of the 30% value. |
| Open-application indicators | ⚠️ | Active/running-state indicators work. Multi-window separator lines are a right-aligned overlay (1 line at 2 windows, 2 lines at 3+). Both lines are shifted another 2% right; at 3+ the inner line moves 1.5% toward the outer line to tighten the pair. Pending real-hardware DPI verification. |
| Application tooltips | ✅ | Application-name tooltips are available. |
| File drag & drop onto taskbar buttons | ⚠️ | Dropping a file onto a pinned/running app button to open it with that app (hover-to-activate + drop) has an initial implementation: standard WPF drag&drop (no COM IDropTarget needed, since this isn't injected into explorer.exe), with a fallback to ShellExecute when the known executable can't be launched directly, and an extension check (via registry SupportedTypes, permissive when unknown) driving the allowed/forbidden cursor feedback. Needs real-world testing (multi-file drops, apps without declared SupportedTypes, mixed-extension drops). |
| Thumbnail previews (DWM) | ⚠️ | **Confirmed working on real hardware at 100% and at 125% DPI on a 1920x1080 display.** On a 1368x768 display at 125% DPI, the taskbar shows a brief flash (disappears and reappears) — likely a low-resolution/small-monitor edge case rather than a general 125% DPI issue; considered rare and not currently prioritized. The popup uses the direct RetroBar-style DWM path (TaskThumbnail.xaml.cs): source-size query, aspect-preserving 180×120 fit, render-time destination updates and guaranteed unload cleanup. Frame, close button and navigation remain in TaskbarWindow.xaml; no confirmation timer or icon fallback is interposed. v1.21.32: the close X copies the height of the title label and centres itself on it (falling back to the historical 14 px when the text cannot be measured), so it no longer depends on a magic value in the template. The frame's accent layer comes from the core's native 9-slice renderer (W7T_RenderAeroThumbnailFrame, cached per size/accent by Utilities/NativePreviewFrame.cs) whenever the core can supply it; the XAML slice template draws it otherwise, and the grayscale overlay, the clipped static blur and the close button are shared by both paths. v1.21.18: the live surface rectangle is measured in physical pixels (PointToScreen, both corners, popup client origin subtracted) instead of multiplying a DIP rectangle by the monitor scale, and the preview popup keeps its 100%-DPI pixel geometry at any scaling (one layout transform on the popup content, no-op at 100%), so the frame, the 202x109 aperture and the DWM rectangle agree by construction at 125%/150% too. v1.21.20: on a display above 100% the normalised preview is drawn 10% larger (PreviewHighDpiEnlargement): the geometry is still the 100%-pixel one (same frame, same aperture, same measured rectangles) and only its size on screen changes, so every measurement taken from the screen follows automatically. At 100% the factor is 1. Not yet tried at 150% or on multi-monitor/mixed-DPI setups. |
| Jump Lists | ❌ | Despite the Windows 7-style Jump Lists being implemented as a dedicated subsystem (left-button press + drag-up on a task button opens the list; releasing the button over a row activates it), they are currently not enabled in the code of the software (confirmed still disabled). The data comes from the real Shell APIs (IApplicationDocumentLists + the window/shortcut AppUserModelID), never from invented entries, and the right-click menu is unchanged. The popup is a native window with DPI-scaled geometry. Not yet verified against a real Windows desktop at every scale, so it is not marked complete. |
| Windows 7 toolbars | ✅ | The three Windows 7-style toolbars are present. |
| Notification area | ⚠️ | The notification area is implemented, but support for all modern Windows tray states is still partial. v1.7.6: per-icon behavior preferences moved from the legacy registry key to trayicons.ini (zero-footprint); the old key is imported and deleted on first run. |
| Balloon notifications | ⚠️ | **v1.21.39:** the balloon now anchors to the icon that generated it, as in Windows 7. Three defects were fixed after checking RetroBar/ManagedShell (`NotifyIcon.TrayIcon_NotificationBalloonShown`, `NotifyIconList` icon promotion, `NotificationBalloon` icon/timeout rules), ExplorerPatcher and Open-Shell (work-area aligned updater balloon, `SystemNotification` sound) against the Microsoft `NOTIFYICONDATAW` documentation: (1) an icon living in the overflow had no visible container, so every balloon fell back to the right edge of the whole notification area — now the icon is temporarily promoted to the bar for the balloon's duration (plus 500 ms), the Windows 7 "Only show notifications" behavior that RetroBar reproduces; the promotion lives in a side set, never touches the user's saved pin preference; (2) the arrow-tip offset used by the placement callback was 23.5 px while the theme's triangle tip sits 14 px from the balloon's right edge (13 px margin + 21−20 of the `M 0,0 l 20,20 V 0` vertex), so even anchored balloons landed ~9.5 px too far right — the tip is now centred on the icon; (3) the icon slot: the theme's text-indent trigger was still bound to ManagedShell's `Icon` property (nonexistent here, so the 36 px indent stayed as an empty gap), the icon is now assigned directly to the `Image` inside the detached popup, `NIIF_USER` falls back to the application's tray icon (the documented legacy `hIcon` behavior, same as ManagedShell; the `hBalloonIcon` handle cannot cross the process boundary), and per the documentation no icon is shown when `szInfoTitle` is empty. Also new, following the same references: duration from `SPI_GETMESSAGEDURATION` when the app's `uTimeout` is not usable (deprecated since Vista), the `SystemNotification` sound unless `NIIF_NOSOUND`, click on the balloon body forwarded to the application as `NIN_BALLOONUSERCLICK` (plus `NIN_BALLOONSHOW`/`BALLOONHIDE`/`BALLOONTIMEOUT`) with the version-4 wParam/lParam layout, and a retry of the anchor resolution after the layout pass for icons whose container is not generated yet (RetroBar's missed-notifications pattern). **v1.21.40 — balloon position enforced (Win32)**: the trial build of 1.21.39 parked the balloon at the top-left corner of the screen. Root cause: WPF `Popup`s whose `PlacementTarget` loses its presentation (icon containers recreated by the tray refresh / promoted-icon churn) lose their coordinates and fall back to (0,0); changing the target on an open popup does not reliably re-place it. The anchor validity check now also requires a live `PresentationSource` (a popup never opens on a detached target), and a watchdog — running for *every* balloon — reads the popup's real `GetWindowRect`, recomputes where the balloon belongs from the anchor's screen coordinates (tip centred on the icon, or right-aligned over the tray), and forces it with `SetWindowPos` (work-area clamped) whenever the two disagree. **Real-hardware testing of 1.21.39/1.21.40 still reported balloons at the top of the screen, so v1.21.41 temporarily DISABLES balloon display** (`TaskbarWindow.BalloonNotificationsEnabled = false`, same kill-switch pattern as `TaskPreviewsEnabled`): the native interception stays active, so `NIF_INFO` notifications produce no popup (ours or Windows') and no `NIN_BALLOON*` feedback — the same semantics as Windows with an app's notifications switched off. Anchoring, promotion, icons, duration, sound and feedback all remain in the code and come back by flipping the flag, once the placement issue is understood. |
| Notification Area settings page | ⚠️ | All Customize links now open the native Windows Notification Area page directly through its shell namespace, with system fallbacks for builds that redirect it. To make this page more functional on Windows 11, it is recommended to use ExplorerPatcher along with this software. |
| Windows 11 system tray support | ⚠️ | Windows 11 system tray support is implemented, but some tray icons are recreated because Windows 11 no longer exposes all classic tray elements directly. |
| Tray overflow | ✅ | The overflow experience is close to Windows 7. taskmgr.exe is intentionally excluded because its renewed tray registrations produced many duplicate rows and an excessively tall overflow menu on affected builds. |
| Battery indicator | ⚠️ | Battery status is implemented with a recreated taskbar icon, but the implementation is still partial rather than a complete native Windows 7 battery implementation. However, by using ExplorerPatcher with the "Windows 7" option, |
| Clock and date display | ✅ | The taskbar clock and date are present. |
| Language switcher (input language flyout) | ⚠️ | **Tested on real hardware: working correctly.** Windows 7/8.1-style layout switching is ported from the Windhawk language-restorer mod. The Windows 7 selection mark now uses the mod's runtime-loaded GDI+ path with GDI fallback; layout enumeration and real WM_INPUTLANGCHANGEREQUEST switching remain. Global hooks, shortcuts, and shortcut hints are deliberately absent. |
| System flyouts | ✅ | The main flyouts work, but positioning and some Windows-version-specific behavior still need improvement. |
| Clock flyout | ✅ | The Windows 7-style clock flyout is now considered complete. |
| Aero Peek / Show Desktop | ✅ | Windows 7-style Aero Peek and the Show Desktop area are included in the software. |
| Context menus | ✅ | Context menus are generally close to the Windows 7 behavior and appearance, with some details still to improve. |
| Taskbar Properties | ✅ | A Windows 7-style Properties interface is available, although some options and behaviors can still be refined. |
| Start with Windows (autostart) | ⚠️ | **v1.21.37:** new checkbox in the "About / Informazioni" tab of the Properties window. The implementation is copied from RetroBar (`PropertiesWindow.xaml.cs`: `LoadAutoStart` / `CbAutoStart_OnChecked`, plus `Utilities/ExePath.cs`): the state is read from the value names of `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`; enabling writes the quoted executable path as the `Win7Taskbar` value, disabling deletes it — fully reversible. Every step is wrapped in try/catch like the original, and the labels are RetroBar's own "autostart" strings in all 11 languages. Attribution in `docs/CREDITS.txt` and `docs/THIRD-PARTY-NOTICES.md`. Needs real-hardware confirmation of the logon start. |
| Extra settings tab (Properties) | ⚠️ | **Tested on real hardware: works decently.** v1.21.7: fourth tab "Impostazioni extra" with the flyout colour, the connection-flyout privacy mode, the skin selector and the icon-order section. Same settings system as every other option (settings.json), same localisation (11 languages), same Win32 dialog. v1.21.21: changing the language now reloads the WPF language dictionary from the Settings.Language event, so menus can no longer stay in the previous language. v1.21.22: colour label/help text across all 11 languages clarify it applies only to the recreated Windows 8 connection flyout. |
| Flyout colour (recreated flyout) | ⚠️ | **Tested on real hardware: works correctly.** v1.21.16: "system colour" (accent read from the OS on demand) or a custom colour chosen with the standard Windows colour picker. The recreated Windows 8 flyout is part of this build (Win8NetworkFlyout.cpp, selected with "Windows 8 (recreated)" in the network-flyout dropdown) and paints itself with this colour, live: a pane left open repaints when the setting changes. Windows 7 flyouts are untouched by design. |
| Recreated Windows 8 network pane | ⚠️ | **Tested on real hardware: work-in-progress, semi-functional** — not yet ready to be marked complete. v1.21.18: the pane is localised in the 11 languages of the flyout's pack. When the WLAN scan list is empty the connected network is read straight from the driver. v1.21.19: falls back to the Network List Manager (COM) when the driver path returns nothing. The pane's repaint buffer is released even when a frame throws. v1.21.24: the "Connetti automaticamente" row now has a hover state, and hover/selection veils are drawn with GDI+ where the surface supports blending, falling back to opaque fill otherwise. **WIP hardening:** the Microsoft WLAN profile/connect/disconnect boundaries now share SEH plus C++ exception barriers, and `WlanEnumInterfaces` ownership is RAII-managed with `WlanFreeMemory`; failures are reported as recoverable network errors instead of escaping the flyout. |
| Native core build stamp | ✅ | v1.21.18: the commit SHA is compiled into the core and checked by CI, so a package carrying a stale core fails CI instead of shipping. |
| Connection flyout privacy mode | ⚠️ | **Tested on real hardware: works correctly.** v1.21.7: normal (real network names) or privacy (generic "Rete 1", "Rete 2"…). Applied to the recreated connection flyout and live while it is open. Presentation only: no network API is called and no Windows setting is changed. |
| Theme selector (Windows 7 / Windows 8.1) | ⚠️ | **Tested on real hardware: theme switching works.** v1.21.19: both skins are implemented. Windows 7 stays the default and the fallback; the Properties dropdown saves the choice and reapplies the theme at once. The Windows 8.1 Start button, hover tiles, preview frame corners and accent handling have all been iterated through v1.21.21–v1.21.26 with a dedicated verification tool (tools/verify-theme-xaml.py) plus a preview-frame colour verification tool (tools/verify-preview-frame.py); both pass on the current themes. Not yet tried at every scale/multi-monitor combination. |
| App search window | ⚠️ | **Tested on real hardware: works correctly**, but the GDI+ rendering quality with the Windows 8.1 (metro) skin needs improvement. The optional application search (Properties → enable search) is a native layered window (AppSearchWindow.cpp). With the Windows 7 theme it keeps the translucent blue look; with the Windows 8.1 theme (v1.21.31) it is repainted as a flat, fully opaque metro panel in one single fixed violet (#512D78, Start-screen style), with a magnifier icon drawn via GDI+. Falls back to plain GDI when GDI+ is unavailable. |
| Taskbar icon order (drag & drop) | ⚠️ | **Tested on real hardware: works normally.** v1.21.21: the gesture is the RetroBar one — OLE DragDrop.DoDragDrop on the button, insertion index from the half of the button under the pointer, insertion caret, drop on the new slot. The order is saved to settings.json and rebuilt after a restart. It never touches the Windows taskbar, Explorer pinning, the registry or shell shortcuts. Not yet checked with many buttons or at non-100% scaling. |
| Taskbar positioning | ⚠️ | Taskbar positioning is supported in the current implementation, but edge-specific behavior still requires refinement. |
| Taskbar rotation | ❌ | Rotating the taskbar to other screen edges is not fully implemented. |
| Taskbar size / configuration | ⚠️ | Taskbar configuration is available through Properties, but full Windows 7 parity for all size and layout options is not guaranteed. |
| Taskbar locking | ⚠️ | Taskbar locking/configuration behavior is not yet guaranteed to match Windows 7 in every case. |
| Explorer restart / tray recovery | ⚠️ | Recovery and consistent tray/icon state after Explorer restarts and shell changes still require further work. |

## Main missing features

### Taskbar rotation

Taskbar rotation to the top, left, or right side of the screen is still missing. An experimental managed-side rotation shipped in v1.21.28-alpha but was withdrawn in v1.21.29-alpha because the persisted position value (0=Bottom,1=Top,2=Left,3=Right) was cast straight onto TaskbarEdge {Left=0,Top=1,Right=2,Bottom=3}, shifting every choice one edge over (choosing "Right" put the bar at the bottom) and feeding the wrong edge to SetThumbnailEdge, which mis-anchored the DWM previews. The bar is fixed at the bottom and the rotation code is kept commented out in TaskbarWindow.xaml.cs / PropertiesDialog.cpp with the explicit position→TaskbarEdge mapping that any future re-enable must use.

### Thumbnail previews

Windows 7-style taskbar thumbnail previews are back in source with live DWM thumbnails and **confirmed working on real hardware at 100% and 125% DPI on 1920x1080**. A flash (disappear/reappear) was observed at 125% on a 1368x768 display — treated as a rare, low-priority edge case rather than a blocking issue; worth a code-level TODO to isolate whether it's tied to low resolution rather than DPI itself. What remains is verification at 150% and on multi-monitor/mixed-DPI setups.

### Complete Windows 11 system tray support

Windows 11 uses a substantially different system tray architecture from Windows 7. Current support is implemented, but it is still partial and not yet complete enough to be considered finished.

The remaining work includes improving reliability, handling all shell states and Explorer restarts, and making tray icon discovery and updates consistently complete.

### Battery indicator

The battery indicator is implemented using a recreated taskbar icon. Further work is still needed for complete Windows 7 parity and robust handling of every battery state.

### Multi-window stacked indicator bars

The vertical separator lines that Windows 7 draws next to a grouped task button's icon when it holds 2 or more windows are implemented: the rendering logic lives in WPF value converters (WindowStackVisibility, WindowStackOuterBorderOffset) and the visual elements that reference them are present in the task button template (Themes/Overrides.xaml, two Image overlays whose visibility and X offset bind WindowCount). Verified in code (v1.21.31 audit). **Still unclear from real-hardware testing what the expected visual result should look like** — needs a clearer reference/definition of the intended behavior before it can be judged pass/fail, in addition to confirming the offsets at non-100% scaling.

### Recreated Windows 8 network pane

Confirmed on real hardware to be work-in-progress and semi-functional — not yet reliable enough to be marked as a finished feature. Further iteration is needed before it can move from ⚠️ toward ✅.

This iteration follows the public Microsoft WLAN API contracts for [`WlanSetProfile`](https://learn.microsoft.com/en-us/windows/win32/api/wlanapi/nf-wlanapi-wlansetprofile), [`WlanConnect`](https://learn.microsoft.com/en-us/windows/win32/api/wlanapi/nf-wlanapi-wlanconnect), [`WlanDisconnect`](https://learn.microsoft.com/en-us/windows/win32/api/wlanapi/nf-wlanapi-wlandisconnect), and [`WlanEnumInterfaces`](https://learn.microsoft.com/en-us/windows/win32/api/wlanapi/nf-wlanapi-wlanenuminterfaces). The calls remain asynchronous where the UI already requires it; each external boundary converts both structured faults and C++ exceptions into a Win32 failure code, while WLAN-owned buffers are released by RAII. The current-connection and interface-type handling were reviewed against the existing implementation: both were already present through guarded `WlanQueryInterface`/Network List Manager and `GetIfEntry2`-based filtering, so they were not duplicated. The useful missing behavior was added to the row context menu: Windows 10/11 Settings troubleshooting is tried first, with the legacy `msdt.exe` command retained only as a guarded compatibility fallback. Rotation is intentionally outside this work and remains unchanged.

### Jump Lists (incomplete, temporarily disabled)

Jump Lists are currently disabled (confirmed still the case): the task-button gesture entry point is commented out while the remaining behavior is completed. The implementation below remains in the source tree and is not removed.

The Jump List subsystem is intended to reproduce the Windows 7 interaction: press and hold the left button on a taskbar button, drag up past the system drag threshold, and the list opens above the button; moving the cursor through it highlights a row, and releasing the left button activates the row under the cursor. A press without a qualifying drag behaves exactly like before (normal activation, grouping, picker, hover, tooltip), and the right-click keeps only the Windows 7 context menu (never a Jump List command).

Implementation notes:

- The gesture is a small state machine (TaskbarWindow.JumpList.cs) using normal WPF mouse capture and manual hit-test forwarding, the same mechanism the tray drag uses - no global mouse hooks.
- The popup is a native no-activate window with the shared Aero flyout border (native/src/JumpListWindow.cpp). All coordinates crossing the interop boundary are screen physical pixels; the popup geometry is scaled by the DPI of the monitor under the button.
- Entries come only from the public Shell read APIs for jump list data (IApplicationDocumentLists, recent + frequent automatic destinations). Application identity is the window's AppUserModelID from the shell property store, else the pinned shortcut's metadata, else the default id Windows derives from the executable path. If the Shell exposes no list for an application, no document rows are shown - nothing is ever fabricated.
- Failure handling: every Shell/COM call is wrapped in try/catch with logging (managed DiagnosticLogger, category JUMPLIST) and controlled cancellation; native resources are RAII owned and hard faults are contained by the project's portable SEH barrier. A jump list failure can never take the taskbar down.

Remaining work before this is marked ✅ and re-enabled: complete the unfinished behavior, then verify it on a real Windows 10/11 desktop at 100/125/150/200% scaling and on a mixed-DPI multi-monitor setup.

### Taskbar-list compatibility (v1.21.32)

Windows implements `CLSID_TaskbarList`/`ITaskbarList` inside the shell, and
its `HrInit`/`AddTab`/`DeleteTab`/`ActivateTab` reach the taskbar through the
window found by class name (`Shell_TrayWnd`) - the same lookup
`Shell_NotifyIcon` performs. Because this program registers that class name
for its own notification area, the shell lookup can resolve to its tray
window, so the tray window answers the private query (`WM_USER + 236`) with a
live window of its own and that window honours the shell-hook codes the
clients send afterwards. Toolkits that build windows with this protocol
(`tao`/Tauri, used by Windhawk; Chromium/Electron, used by VS Codium and the
other editors) therefore see the documented behavior instead of an unanswered
query: `HrInit` succeeds, `AddTab`/`DeleteTab` add and remove the button, and
no application is left waiting on a reply from a window that does not exist.

Two related fixes ship with it: a window that is published before its title
arrives is accepted when it carries `WS_CAPTION` (Microsoft recommends that
style for taskbar windows), and a window whose title arrives later joins the
bar on the name-change event rather than at the next full enumeration.

Real-hardware confirmation with Windhawk and VS Codium is the purpose of the
test build attached to this pull request; the protocol path is also logged
once with the `TABPROT` tag in `log-core.txt`.

### Color hot-track (hover accent, v1.21.36)

Microsoft documents the effect precisely (Raymond Chen, official Microsoft blog,
2011-12-06): the hovered taskbar button "lights up in a color that matches the
colors in the icon itself", "the lighting effect is centered on the mouse", and
"the code just looks for the predominant color in the icon [...] black, white,
and shades of gray are not considered 'colors' for the purpose of this
calculation".

**Shape it has today**, after three rounds of feedback: the hover look of a task
button is left exactly as it was before the feature (the tray hover tile and the
theme's Aero glow, unchanged), and the colour is a **small spot** on top of it:

- **30%** strength - the `Hotlight` element's opacity is animated to `0.30` in
  the templates of an open program (running, active, flashing). The sequence of
  attempts is worth keeping: 5% (v1.21.34) was too faint, 12% (v1.21.35) was
  raised to 30% on request;
- the spot stays **small**: its radii are the `SpotRadiusX`/`SpotRadiusY`
  constants of `Utilities/HotlightColor.cs` (0.55 and 0.72 of the button), so it
  remains a spot of light around the cursor rather than a wash over the tile;
- it exists **only on programs that are open**: a pinned program that is not
  running keeps the plain hover, with no accent at all;
- it follows the mouse and takes the **dominant colour of the icon** (near-black
  and low-saturation pixels - black, white, greys - are excluded, the rest vote
  weighted by saturation, the winner is lightened toward white).

The colour extraction is cached per icon, the brush is per button (its centre
moves), an icon that cannot be sampled falls back to the neutral white-blue Aero
light, and on a vertical taskbar the light travels along the bar so it never sits
on the button's text. Nothing about the icon, the theme or Windows is modified.

**The two knobs** are the opacity animations of `Hotlight` in
`Themes/Overrides.xaml` (`To="0.30"` / `To="0"`) and the two spot radii in
`HotlightColor.cs`.

## Areas that are already in good shape

- The Windows 7-style Start orb and main taskbar layout are present.
- The Jump List gesture and Shell data path remain in source, but the incomplete feature is temporarily disabled (see above).
- Pinned and grouped task buttons are supported.
- The three Windows 7-style toolbars are present.
- Context menus are generally good.
- The overflow experience is generally good.
- Flyouts are generally functional and visually close to the target.
- The Windows 7-style clock flyout is now considered complete.
- Application-name tooltips are available.
- Icon reordering, DWM thumbnails (100%/125% on standard resolutions), theme switching, the language switcher, the extra settings tab, flyout colour and privacy mode have all now been confirmed on real hardware, not just by CI.
- Overall Windows 7 accuracy is already fairly high.

## Known flyout issues

The main flyout system is functional, but some non-clock flyouts may still need refinement in positioning and Windows-version-specific behavior.

The recreated Windows 8 network pane is confirmed work-in-progress/semi-functional on real hardware and needs further iteration.

The clock flyout itself is considered complete.

## Accuracy limitations

Win7Taskbar is a best-effort reimplementation of the Windows 7 taskbar. Modern Windows versions do not expose all of the same shell functionality that existed in Windows 7, so some features must be recreated using different APIs or approximated.

For this reason, high visual and behavioral accuracy is the goal, rather than claiming 100% feature parity with the original Windows 7 taskbar.
