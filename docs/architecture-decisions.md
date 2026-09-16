# Architecture decisions

Short record of the choices that shape the project, kept in English so that the reasoning
is available to everyone working on it. Each entry states the decision, why it was taken,
and what would make us revisit it.

---

## 1. Two halves: native core plus WPF frontend

**Decision.** All Win32 work lives in `Win7TaskbarCore.dll` (C++), all visible UI lives in
the WPF application (C#). The two talk through a small `extern "C"` surface and P/Invoke.

**Why.** The parts that must be pixel-accurate or that have to reach into shell internals
(tray, flyouts, shell menus, AppBar registration) are easier and safer in C++/Win32;
everything that is layout, animation and settings is far faster to iterate in XAML. Keeping
the boundary narrow also means a crash in the UI thread cannot take the native tray with it
and vice versa.

**Revisit if.** The interop surface keeps growing with per-frame calls: the boundary is
meant for commands and state, not for hot paths.

## 2. The taskbar is our window, the native one is hidden

**Decision.** The project does not patch `explorer.exe` in process. It creates its own
AppBar window and hides the native taskbar.

**Why.** It keeps the project a normal application: no DLL injection, no symbol hooking
against private shell classes, no dependency on a specific Explorer build. The trade-off is
that system windows (flyouts, Start menu) cannot be recreated, so they are *hooked and
repositioned* instead - which is also why the Properties dialog describes them as
"repositioned, not recreated".

**Revisit if.** Recreating the flyouts in-process ever becomes a requirement; that would
mean a radically different, and much more fragile, delivery model.

## 3. The Windows 7 theme is loaded, not compiled

**Decision.** `Themes/Windows7.xaml` is loaded at runtime by `ThemeLoader.cs`, which merges
a base dictionary *in memory* before parsing. WPF theme images are resolved through
`GraphicalResourceBundle` (Base64 PNG payloads) rather than loose files under `Resources/`.

**Why.** Loading from disk (instead of compiling to BAML) keeps the theme editable as a
content file next to the executable. Image centralization removes binary noise from the
repository while preserving the original PNG bytes. The theme still has 16 `BasedOn`
styles that expect a base dictionary with same-named keys, and WPF skips
same-named entries instead of falling back to sibling dictionaries - which produced the
`Cannot find resource named 'TaskbarWindow'` failure. Injecting the base dictionary into
the in-memory document is the arrangement that works; the file on disk is never rewritten.

**Revisit if.** Upstream publishes a self-contained theme.

## 4. `Overrides.xaml` for everything we add

**Decision.** Application-specific additions (button templates, the preview frame, the
close button, icon metrics) live in `Themes/Overrides.xaml`, never in the upstream theme.

**Why.** It keeps the "byte-identical" constraint of point 3 possible, and it makes it
obvious which values are ours: measured frames, paddings and gradients in the overrides
file can be traced back to a changelog entry and a screenshot.

## 5. Window previews: direct DWM surface, parent-owned chrome

**Decision.** `Controls/TaskThumbnail.xaml.cs` contains only the essential
RetroBar DWM path: register the source window, fit it into the 202×109 photo
aperture, update the destination rectangle while rendering, and always
deregister on unload. Tiny sources are enlarged toward a 65% minimum while
preserving aspect ratio, avoiding a fixed frame that visually overwhelms them.
Sources beyond a 2.5:1 aspect threshold use a bounded 15% central crop of the
long edge; pixels remain uniformly scaled and the destination never enters the
fixed frame or close-button region.
`TaskbarWindow.xaml` continues to own the Aero frame, close button, layered
popup placement, activation and navigation.

The DWM destination must remain an unpainted WPF surface. In the layered
preview popup, even an explicit `Background="Transparent"` on
`TaskThumbnail` participates in WPF composition over that destination and
can tint the live thumbnail blue. Conversely, opaque brushes over the same
area in a non-layered popup can cover it with blue or black. Therefore the
thumbnail control leaves `Background` unset (`null`), and the central frame
cell has no background, opacity mask, or effect. Chrome belongs only to the
outer frame images and close button, following RetroBar's ownership model.
DWM supports the layered popup; using a non-layered popup is not a DWM
requirement.

A short-lived, static `Graphics.CopyFromScreen` capture may provide soft glass
behind the **outer chrome only**. `TaskPreviewPopup` clips that blurred image to
the top, left, right and bottom frame bands of every preview item. Above it, the
accent mask supplies the live Windows color while a low-opacity slice of the
unchanged source PNG retains the photograph's exact edge and shading detail.
The complete central aperture remains outside every chrome layer, so this
backdrop is not a thumbnail fallback and never paints, masks or applies an
effect to the DWM destination. The capture is made once in `Opened`; its GDI
bitmap has RAII ownership and the WPF source reference is cleared in `Closed`.

DWM registration is not treated as proof of rendering. After a one-shot 350 ms
delay, the control probes the on-screen destination. If it cannot verify a
composed frame, the single persisted `UseThumbnailCaptureFallback` switch
allows a BitBlt screen scrape of the source client area. That scrape is shown
only when five z-order sample points all belong to the source root window, no
long black two-pixel border indicates a composition race, and sampled edge
pixels contain real colour. Otherwise DWM is unregistered and the UI shows the
application icon plus current title—never an anonymous empty rectangle. All
DCs, selected GDI objects and HBITMAPs have deterministic RAII cleanup; every
failure path is guarded and leaves no registered thumbnail behind.

## 6. The overflow panel is a native popup, its behaviour mirrors Windows 7

**Decision.** The overflow panel (`TrayOverflowWindow.cpp`) is a `WS_POPUP` window,
topmost, tool-window, no-activate, with the Aero frame supplied by the compositor
(`ApplyAeroFlyoutStyle`, the same call the clock flyout uses). Its palette for the footer
comes from the Windows 7/8.1 Action Center recreation mod.

**Why.** The real panel is not a WPF surface and must survive the icon grid changing size
while it is open (an icon can be dropped into it). Native ownership means it can be shown
and hidden from the tray code without marshalling, and the DWM frame gives the correct
Aero edges on every DPI.

**Consequence.** Two topmost windows can be on screen at once (the panel and the tray drag
ghost). Inside the topmost band the window shown *last* wins, so the ghost re-asserts its
position with `SetWindowPos(HWND_TOPMOST)` while it follows the cursor: otherwise the icon
being dragged disappears exactly while it is over the panel.

## 7. Everything measurable is measured

**Decision.** Metrics that come from a reference (the red frame around a taskbar icon, the
gap between the taskbar and the volume flyout, gradient colours) are measured from the
screenshot or the image with a script, and the number recorded in the changelog.

**Why.** "Looks right" does not survive the next change. Several regressions in this
project came from a value being adjusted by eye; the entries that hold a measured
percentage or pixel value are the ones that stayed correct.

## 8. Changes are recorded in the release notes, not in the repository

**Decision.** Each version is described in its release notes (and in the commit history),
in the order the items were requested. No changelog file is kept in the repository.

**Why.** The project is iterated against concrete reports ("the volume flyout opens in the
top-left corner", "the white rectangles are still there"), and the diagnosis is what makes
it possible to tell a regression from a preference months later - so it is written down.
But a folder full of per-version text files is noise for anyone reading the repository, and
the release page is where a reader already looks for "what changed in this build".

## 9. The AppBar rect is the only source of truth for the taskbar geometry

**Decision.** The reservation is always made with the AppBar protocol
(`ABM_QUERYPOS`/`ABM_SETPOS`) on the physical rectangle of the monitor that hosts
the bar window, and the window is then moved onto the rect the shell confirmed.
Shell notifications on the AppBar callback message (`ABN_POSCHANGED`, `ABN_WINDOWARRANGE`,
`ABN_FULLSCREENAPP`) are handled, the AppBar is re-registered when Explorer restarts
(`TaskbarCreated`), and `ABM_WINDOWPOSCHANGED`/`ABM_ACTIVATE` keep the shell up to date.

**Why.** Earlier the reserved rect and the visible window were two separate
calculations (the AppBar call used system metrics in the native core, the window was
placed in DIPs by the frontend). Whenever the shell moved or re-stacked the AppBar -
Explorer's own bar still registered on the same edge, a DPI or monitor change, an
Explorer restart killing the old registration - the work area and the visible bar
diverged, and a strip of unused screen appeared between maximized windows and the
taskbar. Moving the window onto the shell-confirmed rect is the invariant
ManagedShell/RetroBar (`AppBarWindow.SetWindowPosition(abd.rc)`) is built on, and the
`ABN_*` notifications are how the shell keeps every AppBar converging on one layout.
No second work-area mechanism is used, and no other window is ever resized by us.

**Revisit if.** The bar ever needs per-monitor instances or non-bottom edges: the same
invariant still holds, the edge and monitor simply come from the window being positioned.

## 10. v1.2.0-alpha: the real shell first, recreation last

**Decision.** Every feature in this iteration follows the same order of
preference, and the code says so in one place each:

- **Battery flyout.** With the "Windows 7" preference a click on a battery
  icon is forwarded to the real `stobject.dll` icon when one exists (standard
  tray click protocol), otherwise to the Windows 11 battery button via UI
  Automation, and only when neither exists does the recreated panel appear.
  `UseWin32BatteryFlyout=1` (the ExplorerPatcher key) is re-asserted at click
  time, and the deferred check compares the SET of visible foreign windows,
  not a count of popup-styled ones: the real Win32 flyout sometimes arrives
  without `WS_POPUP`, which made the old check open the recreated panel on
  top of the real one.
- **Network flyout list.** When the Wi-Fi scan returns no networks but an
  interface is connected, the current connection is queried and added to the
  list, so a connected machine never shows an empty list; opening the flyout
  with an empty list orders a `WlanScan`, and the reasons for an empty list
  are written to the core log (`[NET]` lines).
- **Input language indicator.** The indicator is a managed control fed by the
  same API chain as RetroBar's `InputLanguage` (focused-thread `HKL` via
  `GetGUIThreadInfo`/`GetKeyboardLayout`, switching via
  `WM_INPUTLANGCHANGEREQUEST` broadcast); the picker menu is the native
  `ShowContextMenuEx` menu used by every other taskbar entry. Three styles
  (Windows 7, Windows 8.1 tile, Windows 10/11 code slightly enlarged) are
  chosen from Properties (`InputLanguageMode`, COPYDATA offset 52).
- **Recreated system icons get Windows 7 context menus.** The synthetic
  volume/network/battery icons translate a right-click into the same menu
  Windows 7 showed, built from the core string tables (all 11 languages) and
  shown with `ShowContextMenuEx`; the commands launch the classic panels
  (`SndVol.exe`, `mmsys.cpl`, Network and Sharing Center, Mobility Center,
  Power Options).
- **Group separators are graphics only.** The stack indicator in the task
  button template draws at most TWO separators (#1F314F, shifted further
  right by a `RenderTransform`), visible with the rule 1 window -> 0,
  2 -> 1, 3+ -> 2; grouping, window detection and layout are untouched.
- **Hidden native taskbar stays hidden during captures.** The hide watcher
  no longer acts only on accessibility events: every 500 ms tick it checks
  the native taskbar's visibility and re-hides it, which covers the Snipping
  Tool case where Windows re-shows the bar without an event we receive.

**Why.** Recreated panels are a fallback, not a goal; where Windows already
has the real Windows 7 surface (the Win32 battery flyout, the real tray
icons, the native menus) it is used, and the recreation only covers the gap.
The empty network list and the double battery flyout were both cases where a
fallback was chosen although the real surface was reachable.

**Revisit if.** Windows removes the Win32 battery flyout entirely (it is
already absent from the newest Windows 11 Insider builds): then the battery
entry keeps the chain but always lands on the recreated panel.

## 11. v1.4/1.5: the language entry is the switcher port; the battery click is physically delivered

**Language entry.** The v1.3 native indicator slot (`LanguageBar.cpp`) is gone.
The tray entry is a thin WPF `Border` (`InputLanguageBar`) that draws the active
abbreviation and opens the core's native popup — the full port of the
"Windows 7/8.1 Language Switcher Restorer" mod in `LanguageSwitcher.cpp`
(GDI/GDI+ Win32 window, Win7 menu or Win8.1 card, layout switching for the
window that had focus). One lesson is encoded twice: a `DependencyProperty`
ignores no-op writes, so the mode is applied through an unconditional
`ApplyMode()` that re-syncs visibility even when the value did not change —
that exact case kept the entry collapsed forever with the default mode.

**Battery ("Windows 10" option).** The real Windows-10-style Win32 battery
flyout still lives in explorer's own `stobject.dll` and is reachable with the
`UseWin32BatteryFlyout` legacy value, which we assert ONLY around the open
attempt and restore afterwards (the registry is left untouched between
attempts; a purely in-memory override cannot work here, because the reader of
that value is explorer's process, not ours). The missing piece was delivery:
the accessibility patterns of the Windows 11 battery button are silent, and a
synthetic click at the button's coordinates hit OUR taskbar, which covers the
native one. v1.5 makes our windows at that point mouse-transparent for the
instant of the click (`WS_EX_TRANSPARENT` + opaque layered style, restored by
a reader-thread timer together with the cursor), so the click lands on the
real button.

**Why not a downloaded Windows 10 `stobject.dll`.** Evaluated and rejected for
now: `stobject.dll` exports only the standard COM surface
(`DllGetClassObject`/`DllRegisterServer`/...), so hosting the Win10 binary
in-process would require undocumented RVAs into C++ objects with no stable
contract — strictly more fragile than the sanctioned in-memory key plus a
physically delivered click on the real button. Revisit only if a Windows
build removes the legacy flyout path from explorer's own `stobject.dll`.

## 12. v1.6: our own windows swallow hardware faults (anti-mod hardening)

**Decision.** The language-switcher popup's window procedure and the public
entry points of its module run under the portable `W7T_SEH_*` guard: a
hardware exception raised inside them (typically a third-party Windhawk mod
hooking the same system APIs we call, and faulting) is logged and swallowed.
Because swallowing skips the `Enter`/`Leave` pairs, every critical-section
acquisition in the module goes through depth-counting helpers and the catch
path releases whatever is left open; an interrupted paint validates its
update region so Windows does not spin in an endless repaint. The entry
points are guarded twice (here and at the C exports) on purpose.

**Why.** A field crash happened with the language popup open: an unhandled
exception from a window procedure kills the process that hosts it, and the
taskbar must survive third-party software it does not control. The tray
window procedure already had this shape (its `Inner` split dates from v2.6).

**Limits.** Swallowing skips C++ unwinding: objects alive at the fault point
leak once, and a `std::mutex` held across the fault would stay locked - that
is why the guarded module uses raw critical sections with heal-on-fault and
no locks are taken across the guarded boundary elsewhere.

**Revisit if.** A fault repeats in one spot: the log line names the module
phase, and the guard can then be narrowed to the exact call.

## 13. Jump Lists: incomplete and temporarily disabled

**Current status.** Jump Lists are incomplete, so the task-button entry-point
call is commented out and they cannot currently be opened. The managed and
native implementation is intentionally retained in place for completion.

**Decision.** When re-enabled, the Windows 7 Jump List opens exclusively from the left-button
press + drag-up gesture on a task button (never from the right-click menu),
and the implementation is split the way the rest of the project is:

- `TaskbarWindow.JumpList.cs` runs the small state machine
  (`Idle -> PotentialDrag -> Opening -> Open`) on the UI thread. Input uses
  ordinary WPF mouse capture plus window-level tunneling handlers and manual
  hit-test forwarding - the same mechanism the tray drag has used since v2.7.
  No global mouse hook is introduced.
- The popup itself is a native `WS_POPUP | WS_EX_NOACTIVATE` window with the
  shared Aero flyout border (`JumpListWindow.cpp`), so it can show and
  repaint while the WPF window keeps the capture, exactly like the tray
  overflow panel (point 6).
- Data comes only from documented Shell APIs: application identity via
  `SHGetPropertyStoreForWindow` (window) and
  `SHGetPropertyStoreFromParsingName` (the pinned .lnk) for the
  AppUserModelID, and `IApplicationDocumentLists` for the Recent/Frequent
  destinations. When the Shell exposes no list, no document rows appear -
  nothing is invented.
- Every coordinate crossing the interop boundary is a **screen physical
  pixel**; the WPF side converts with `PointToScreen` only (both button
  corners, never a size multiplied by a scale a second time), and the native
  side scales its 96-DPI geometry with `GetDpiForScreenRect` for the monitor
  of the button. The release-activates rule replaces the "click inside the
  popup" flow: the popup never takes focus, so activation is the gesture's
  own left-button release, forwarded by position.

**Why.** The old v2.38/v2.40 shape (right-click opening a popup that grabs
foreground and reads the hardware cursor itself) fought the WPF capture
model, double-scaled coordinates at non-100% DPI, clamped only against the
primary monitor, and shipped the document list disabled. A separate
subsystem with one choke point per responsibility (identity, read, show,
hover, commit, cancel) is what lets every failure path end quietly:
capture always released, popup always hidden, exceptions always logged
under the `JUMPLIST` tag.

**Revisit if.** Windows removes or renames the automatic-destination read
APIs, or the bar ever needs per-monitor instances: the pixel-space contract
stays the same, only the monitor lookup of the anchor changes.

## 14. Notification Area settings: delegate to Windows

**Decision.** Every notification-area “Customize...” entry delegates directly
to the native Windows page through `W7T_OpenNotificationIconsSettings`. The
core first opens the shell namespace and retains the existing system fallbacks
for Windows versions that redirect that namespace. Win7Taskbar does not create,
register, host, or imitate a Control Panel applet.

Per-icon placement selected by dragging between the taskbar and overflow remains
portable in `trayicons.ini`; it is taskbar state, not a replacement settings UI.
