# Feature Status

This document tracks the current feature status of Win7Taskbar and the main areas that still need improvement.

## Overall accuracy

Win7Taskbar currently has **high overall Windows 7 visual and behavioral accuracy**, but it is not a complete reproduction of every Windows 7 taskbar feature.

Some parts are already close to the original Windows 7 experience, while other parts are still being implemented or refined.
If there are any imprecisions or problems, please report them to the author of this software.

## Current status

| Feature | Status | Notes |
|---|---|---|
| Windows 7 taskbar layout | ✅ | The main taskbar layout is already close to Windows 7. |
| Start button / Windows orb | ✅ | The Windows 7-style Start orb is present with normal, hover, and pressed states. |
| Pinned applications | ✅ | Pinned taskbar applications are supported. |
| Application grouping | ✅ | Grouped task buttons are supported as part of the Windows 7-style Superbar behavior. |
| Application icons | ✅ | The general appearance is accurate, but icon accuracy is not yet complete for every application and system icon. |
| Open-application indicators | ⚠️ | Active/running-state indicators work. Multi-window separator lines are a right-aligned overlay (1 line at 2 windows, 2 lines at 3+). Both lines are shifted another 2% right; at 3+ the inner line moves 1.5% toward the outer line to tighten the pair. Pending real-hardware DPI verification. |
| Application tooltips | ✅ | Application-name tooltips are available. |
| File drag & drop onto taskbar buttons | ⚠️ | Dropping a file onto a pinned/running app button to open it with that app (hover-to-activate + drop) has an initial implementation: standard WPF drag&drop (no COM `IDropTarget` needed, since this isn't injected into explorer.exe), with a fallback to `ShellExecute` when the known executable can't be launched directly, and an extension check (via registry `SupportedTypes`, permissive when unknown) driving the allowed/forbidden cursor feedback. Needs real-world testing (multi-file drops, apps without declared `SupportedTypes`, mixed-extension drops). |
| Thumbnail previews (DWM) | ⚠️ | The popup uses the direct RetroBar-style DWM path (`TaskThumbnail.xaml.cs`): source-size query, aspect-preserving 180×120 fit, render-time destination updates and guaranteed unload cleanup. Frame, close button and navigation remain in `TaskbarWindow.xaml`; no confirmation timer or icon fallback is interposed. The frame's accent layer comes from the core's native 9-slice renderer (`W7T_RenderAeroThumbnailFrame`, cached per size/accent by `Utilities/NativePreviewFrame.cs`) whenever the core can supply it; the XAML slice template draws it otherwise, and the grayscale overlay, the clipped static blur and the close button are shared by both paths. v1.21.18: the live surface rectangle is measured in physical pixels (`PointToScreen`, both corners, popup client origin subtracted) instead of multiplying a DIP rectangle by the monitor scale, and the preview popup keeps its **100%-DPI pixel geometry** at any scaling (one layout transform on the popup content, no-op at 100%), so the frame, the 202x109 aperture and the DWM rectangle agree by construction at 125%/150% too. Not yet compared side by side on real hardware at 100/125/150%. v1.21.20: on a display above 100% the normalised preview is drawn 10% larger (`PreviewHighDpiEnlargement`): the geometry is still the 100%-pixel one (same frame, same aperture, same measured rectangles) and only its size on screen changes, so every measurement taken from the screen follows automatically. At 100% the factor is 1. Not yet tried on hardware. |
| Jump Lists | ❌ | Despite the Windows 7-style Jump Lists being implemented as a dedicated subsystem (left-button press + drag-up on a task button opens the list; releasing the button over a row activates it), they are currently not enabled in the code of the software. The data comes from the real Shell APIs (`IApplicationDocumentLists` + the window/shortcut `AppUserModelID`), never from invented entries, and the right-click menu is unchanged. The popup is a native window with DPI-scaled geometry. Not yet verified against a real Windows desktop at every scale, so it is not marked complete. |
| Windows 7 toolbars | ✅ | The three Windows 7-style toolbars are present. |
| Notification area | ⚠️ | The notification area is implemented, but support for all modern Windows tray states is still partial. v1.7.6: per-icon behavior preferences moved from the legacy registry key to `trayicons.ini` (zero-footprint); the old key is imported and deleted on first run. |
| Notification Area settings page | ⚠️ | All Customize links now open the native Windows Notification Area page directly through its shell namespace, with system fallbacks for builds that redirect it. To make this page more functional on Windows 11, it is recommended to use ExplorerPatcher along with this software. |
| Windows 11 system tray support | ⚠️ | Windows 11 system tray support is implemented, but some tray icons are recreated because Windows 11 no longer exposes all classic tray elements directly. |
| Tray overflow | ✅ | The overflow experience is close to Windows 7. `taskmgr.exe` is intentionally excluded because its renewed tray registrations produced many duplicate rows and an excessively tall overflow menu on affected builds. |
| Battery indicator | ⚠️ | Battery status is implemented with a recreated taskbar icon, but the implementation is still partial rather than a complete native Windows 7 battery implementation. However, by using ExplorerPatcher with the "Windows 7" option, |
| Clock and date display | ✅ | The taskbar clock and date are present. |
| Language switcher (input language flyout) | ⚠️ | Windows 7/8.1-style layout switching is ported from the Windhawk language-restorer mod. The Windows 7 selection mark now uses the mod's runtime-loaded GDI+ path with GDI fallback; layout enumeration and real `WM_INPUTLANGCHANGEREQUEST` switching remain. Global hooks, shortcuts, and shortcut hints are deliberately absent. |
| System flyouts | ✅ | The main flyouts work, but positioning and some Windows-version-specific behavior still need improvement. |
| Clock flyout | ✅ | The Windows 7-style clock flyout is now considered complete. |
| Aero Peek / Show Desktop | ✅ | Windows 7-style Aero Peek and the Show Desktop area are included in the software. |
| Context menus | ✅ | Context menus are generally close to the Windows 7 behavior and appearance, with some details still to improve. |
| Taskbar Properties | ✅ | A Windows 7-style Properties interface is available, although some options and behaviors can still be refined. |
| Extra settings tab (Properties) | ⚠️ | v1.21.7: fourth tab “Impostazioni extra” with the flyout colour, the connection-flyout privacy mode, the skin selector and the icon-order section. Same settings system as every other option (`settings.json`), same localisation (11 languages), same Win32 dialog. First release: needs real-world testing.. v1.21.21: changing the language now reloads the WPF language dictionary (`Languages/*.xaml`, the source of the taskbar context menus) from the single place every change passes through — the `Settings.Language` event — instead of only from the extra-settings packet, so the menus can no longer stay in the previous language; the new dictionary is loaded *before* the old one is removed, and  v1.21.22: l'etichetta e il testo di aiuto del colore dicono, **in tutte e 11 le lingue**, che il colore riguarda **solo il flyout di connessione ricreato di Windows 8** e non gli altri flyout (l'etichetta e' diventata "Colore del flyout di connessione:" e la precisazione apre il testo di aiuto, cosi' si legge anche se il riquadro taglia le righe in eccesso). Il selettore di lingua della barra (`LanguageSwitcher`) e' stato rivalutato contro la documentazione Microsoft: i nomi delle lingue arrivano da `GetLocaleInfoW(... LOCALE_SLOCALIZEDDISPLAYNAME)` con ripiego su `LOCALE_SENGLISHDISPLAYNAME` (il comportamento documentato per i nomi localizzati) e le sue due voci di menu sono gia' una tabella per lingua con ripiego inglese, quindi non c'era nulla da applicare. |
| Flyout colour (recreated flyout) | ⚠️ | v1.21.16: “system colour” (accent read from the OS on demand) or a custom colour chosen with the standard Windows colour picker. The recreated **Windows 8** flyout is part of this build (`Win8NetworkFlyout.cpp`, selected with “Windows 8 (recreated)” in the network-flyout dropdown) and paints itself with this colour, live: a pane left open repaints when the setting changes. Windows 7 flyouts are untouched by design: the setting can never change their appearance, and nothing in Windows (accent colour, theme, personalisation) is written. |
| Recreated Windows 8 network pane | ⚠️ | v1.21.18: the pane speaks the language of the program (every string of the pane is localised in the 11 languages of the flyout's pack, and a language announced before the module was initialised is no longer dropped). When the WLAN scan list is empty the connected network is read straight from the driver, and the Connections section is never left blank. The connect/disconnect paths carry the same SEH + C++ shields as the shell launches. v1.21.19: when the scan list is still empty, the connected network is now read from the **Network List Manager** as a last resort — the same COM source the Windows 7 pane already uses on this PC (`WlanGetAvailableNetworkList` fails with error 5 there) — so a Wi-Fi machine shows at least its own connection; the driver path now logs *why* it returned nothing. The pane's repaint buffer is released even when a frame throws (an early exit used to leave the memory bitmap selected in the DC, so `DeleteObject` failed silently and the object leaked on every size change). **Still to be confirmed on hardware**: the empty-list and crash reports of v1.21.17/v1.21.18 could not be reproduced here (no Windows machine). |
| Native core build stamp | ✅ | v1.21.18: the commit SHA is compiled into the core, written to `log-core.txt` on the first log line (`=== Win7Taskbar core build: … ===`) and checked by the release workflow inside the packaged `Win7TaskbarCore.dll`: a package carrying a stale core now fails CI instead of shipping. `publish.ps1 -SkipNative` (the mode the release workflow uses) also takes the DLL from the CMake build tree (`native/build/<Config>/`) before the tracked `dist/` folder, so the package uses the binary that was just compiled. v1.21.19: the `[CRASH]` line in `log-core.txt` and the crash report also carry the first call frames read from the faulting stack (`module+0xoffset`), so a bare `msvcrt.dll +0x7BDBF` no longer identifies nothing. |
| Connection flyout privacy mode | ⚠️ | v1.21.7: normal (real network names) or privacy (generic “Rete 1”, “Rete 2”…). Applied to the recreated connection flyout and live while it is open. Presentation only: no network API is called and no Windows setting is changed. Needs verification on real hardware (Ethernet + Wi-Fi + multiple saved networks). |
| Theme selector (Windows 7 / Windows 8.1) | ⚠️ | v1.21.19: **both skins are implemented**. Windows 7 stays the default and the fallback; `TaskbarThemeIds.IsImplemented` accepts 0 and 1, `ThemeLoader.ThemeFileNameFor` maps 1 → `Themes/Windows8.1.xaml` (shipped as `Content`, loaded from disk like the Windows 7 one) and the Properties dropdown saves the choice. Changing it re-applies the theme at once — `ThemeLoader.ReapplyNow` assigns `Application.Resources` exactly as `App.xaml.cs` does at startup — and if the dictionary cannot be built the previous theme stays and the log says a restart is needed. The Windows 8.1 Start button uses the two sprites embedded in `GraphicalResourceBundle` (`startwin81flag` / `startwin81flagscaled`, 100% and scaled); the source PNGs are no longer in the repository. v1.21.21: the Start button is 53 DIP wide (about 2% below the Windows 7 orb, the size the 8.1 flag actually has) and sits 1 DIP further left (≈1.5%), drawn 1:1 from the sprite; its hover/pressed cut-outs are filled with the Windows accent colour by an opaque layer placed **under** the tile (`OrbCutOutFill`, the same live accent the previews use), so inside the tile the flag is tinted, not white and not a hole. The hover cross-fade is gone: both the hover and the pressed state are plain static setters (idle → hover → pressed is instant, as on Windows 8.1, and no animation value can stay attached and make the state flip back to idle). Also v1.21.21: the recreated 8.1 taskbar has no Aero glass texture at all — `Win7_TaskbarTextureBrush` and its vertical twin are flat uniform tints (`#59000000`, generic transparency) and the accent is only read through the window; the PNG texture stays in the bundle for the Windows 7 skin.  v1.21.22: con la skin 8.1 la barra e le anteprime sono **squadrate**, come l'interfaccia piatta che ha sostituito il vetro Aero; v1.21.23: le tessere di hover delle icone della barra e dell'area di notifica tornano gli **asset del bundle Windows 7** (i due "baffi" di luce ai lati dell'icona, l'effetto che la v2.53 aveva ripristinato nel tema 7) perche' le due tessere piatte della 1.21.22 rendevano il passaggio del mouse quasi invisibile, e la **cornice dell'anteprima viva** tiene la trasparenza della cornice Windows 7 - accento allo 0,81 con il bordo luminoso piu' la velatura in scala di grigio al 18 per cento - cambiando solo la forma: le quattro fette d'angolo dell'asset (che contengono l'arco del vetro) sono sostituite da una striscia presa al centro della banda corrispondente dello stesso asset, quindi stesso colore e stessa opacita' con lo spigolo a 90 gradi, mentre le quattro fette dei lati restano quelle originali. Le cornici dei pulsanti (`TaskButtonFrameHover/Active/Notification`), la tessera di hover/pressione (`TrayTileHoverImage`, ora un rettangolo vettoriale) e la cornice dell'anteprima (`Win81TaskPreviewFrameVista`, quattro bande piene di colore accento senza maschere) sono varianti dichiarate **nel solo file del tema 8.1**; `ThemeLoader.ShadowWindows81Keys` le copia nel dizionario principale quando la skin attiva e' la 8.1 (i dizionari mergiati cercano per ultimo aggiunto, quindi il tema da solo non potrebbe mai sovrascrivere Overrides.xaml). I raggi di frecce di scorrimento, task button del tema e freccia dell'overflow scendono a 0, le tessere di hover/pressione della superbar e dell'area di notifica sono rettangoli vettoriali piatti (niente piu' cornice tonda) e anche i nove raggi che vivevano in `Themes/Overrides.xaml` - tessera di hover dei pulsanti, sfondo premuto e alone - sono diventati chiavi `Superbar*CornerRadius` il cui valore predefinito e' quello Windows 7 (4/3/4/6, quindi il tema 7 non cambia di un pixel) e che la skin 8.1 porta a 0 con la stessa copia nel dizionario principale: senza quel passaggio i raggi scritti in Overrides non sarebbero raggiungibili, perche' quel dizionario e' mergiato dopo il tema. La X dell'anteprima 8.1 e' la 17x17 fornita dall'utente, incorporata in base64 in `Utilities/PreviewAssets.cs` (`Win81PreviewClose`) e **rimossa dal repository** (`Resources/win81x.png` non esiste piu'); la skin Windows 7 continua a usare la X tonda a 14 px e `Windows7.xaml` resta byte-identico. Con la skin 8.1 il percorso nativo a 9 slice non viene applicato (il template 8.1 non espone la parte), quindi la cornice rettangolare e' quella XAML e l'arco del vetro Aero non puo' ricomparire. ⚠️ = statically verified and built by CI, **not yet tried on a real Windows 8.1/10/11 desktop**. |
| Taskbar icon order (drag & drop) | ⚠️ | v1.21.21: the gesture that moves the icons of the **software** taskbar is the **RetroBar one** — OLE `DragDrop.DoDragDrop` on the button, insertion index from the half of the button under the pointer, insertion caret, drop on the new slot — which is the only armed gesture; the previous tray-style implementation (mouse capture, ghost icon, caret in its own window) stays compiled in `TaskbarWindow.TaskOrder.cs` but no longer captures the press (`TaskOrderDragEnabled`), exactly like the Jump List gesture. The order obtained with the RetroBar gesture is also **saved**: the drop calls `ApplyUserTaskbarOrder`, which writes the ordered list of stable application keys into `settings.json` and rebuilds it after a restart (v1.21.7 requirement). It never reads or writes the Windows taskbar, Explorer pinning, the registry or shell shortcuts. Not yet verified on real hardware, and not yet checked at non-100% scaling or with many buttons. |
| Taskbar positioning | ⚠️ | Taskbar positioning is supported in the current implementation, but edge-specific behavior still requires refinement. |
| Taskbar rotation | ❌ | Rotating the taskbar to other screen edges is not fully implemented. |
| Taskbar size / configuration | ⚠️ | Taskbar configuration is available through Properties, but full Windows 7 parity for all size and layout options is not guaranteed. |
| Taskbar locking | ⚠️ | Taskbar locking/configuration behavior is not yet guaranteed to match Windows 7 in every case. |
| Explorer restart / tray recovery | ⚠️ | Recovery and consistent tray/icon state after Explorer restarts and shell changes still require further work. |

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

### Jump Lists (incomplete, temporarily disabled)

Jump Lists are currently disabled: the task-button gesture entry point is
commented out while the remaining behavior is completed. The implementation
below remains in the source tree and is not removed.

The Jump List subsystem is intended to reproduce the Windows 7 interaction: press and hold the left button on a
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

Remaining work before this is marked ✅ and re-enabled: complete the unfinished behavior,
then verify it on a real Windows 10/11 desktop at 100/125/150/200% scaling and on a
mixed-DPI multi-monitor setup.

## Areas that are already in good shape

- The Windows 7-style Start orb and main taskbar layout are present.
- The Jump List gesture and Shell data path remain in source, but the incomplete feature is
  temporarily disabled (see above).
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
