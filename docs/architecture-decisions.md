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

The frame and the live surface must be described in the same unit. The Aero
frame is a 9-slice with 17/38/19-unit slices around a 202x109 aperture, and the
frame template reserves exactly those bands around its `ContentPresenter`;
`NativePreviewFrame` therefore asks the core for the frame bitmap in the frame
element's **layout units** (the brush's `Stretch.Fill` scales border and
aperture together on whatever monitor the popup opens). Asking for it at the
element's device size instead keeps the slices whole screen pixels while the
aperture follows the popup - 18.7/41.8/20.9 px at 125% under the 10% high-DPI
enlargement - and leaves an unpainted strip inside the border. The destination
rectangle handed to DWM is measured in physical pixels and rounded outward, so
it can never be smaller than the aperture the frame painted.
`native/tools/check-preview-geometry.py` checks both halves of that agreement.

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

## 8. Release notes policy during active development (historical)

**Historical decision.** During active development, each version was described in its
release notes and commit history; no separate changelog file was kept in the repository.

**Historical rationale.** The project was iterated against concrete reports ("the volume
flyout opens in the top-left corner", "the white rectangles are still there"), and recording
the diagnosis helped distinguish a regression from a preference. The release page served
as the location for version-specific notes.

**Archive status.** Release notes are no longer maintained. All GitHub releases and tags
except `v1.3.26-alpha` have been removed as part of the project discontinuation.

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

## 13. Jump Lists: the drag away from the bar is the trigger

**Current status.** Jump Lists are enabled with the Windows 7 trigger:
LEFT press + drag away from the taskbar (up for a bottom bar, down for a
top bar, right for a left bar, left for a right bar) opens the list during
the drag; the list opens at the canonical Windows 7 position - directly
above the button, left-aligned with its left edge, small gap (mirrored per
bar edge) - and stays anchored there for the whole gesture, while the row
under the cursor is highlighted. Releasing on a row activates it; releasing
over the list or the button leaves the list open and persistent (row
clicks, Escape, click-outside); releasing outside the interaction area
cancels. The right-click of a task button keeps only the Windows 7
context menu and NEVER opens a Jump List. The v2.61 up-arrow secondary
trigger was removed in v2.62 on user request: the drag is the only trigger.

**Decision.** The trigger is the drag, not the right-click, and not the
arrow alone:

- The press of a task button arms TWO candidates on the same press - the
  jump-list candidate (`ArmJumpDragCandidate`) and the icon-reorder
  candidate (the RetroBar session reorder) - and nothing else. `TaskButton_-
  PreviewMouseMove` arbitrates on every move: the first axis to cross its
  threshold OWNS the press. Away from the bar past `JumpDragAwayThreshold`
  (6 DIP, deliberately above the system's 4 DIP so a wobbling click stays a
  click) starts the jump-list drag; along the bar past the system drag
  threshold starts the reorder; a diagonal drag is decided by the dominant
  axis. First-threshold-wins on shared axes is what makes the two gestures
  conflict-free: each press belongs to exactly one of them (or to the plain
  click when no threshold is crossed), and the winner is committed before
  any capture or OLE drag starts.
- `BeginJumpDrag` takes the mouse capture on the button at that moment, so
  the moves and the release come back to it from anywhere on the screen.
  The popup keeps the position the native `Place` computed at open -
  directly above the button, left-aligned with its left edge, small gap,
  clamped to the work area of the monitor that hosts the button: exactly
  where the Windows 7 shell opens its jump view, and the only position rule.
  Every move calls the native `W7T_JumpListSetHover`, which updates only the
  highlighted row. History: the v2.62-alpha carried a live cursor-following
  re-anchoring (`W7T_JumpListDrag` / `JumpListWindow::DragMove`), the
  cursor-position rule of the GPL-3.0 Windhawk mod
  "taskbar-jump-list-on-cursor-pos" (m417z); the alpha test showed the list
  in a position that does not match the shell's, so the final v2.62 drops
  that rule and keeps the canonical button-anchored placement. See
  THIRD-PARTY-NOTICES.md for the license note on the mod (whose idea was
  evaluated, implemented in the alpha and then discarded).
- The release (`TaskButton_PreviewMouseLeftButtonUp`, tunneling per button)
  ALWAYS consumes the click of the press that dragged (e.Handled before the
  button's own click handling), and then decides with two native answers:
  `W7T_JumpListHitRow` (the row under the point, no side effects) and the
  inside/outside answer of the last `W7T_JumpListSetHover`. Row -> activate
  (the native popup closes itself; a pin toggle refreshes the model);
  inside -> the list persists and `W7T_JumpListMakeInteractive` transfers
  ordinary input to it (the drag-up became a click list); outside ->
  `W7T_JumpListHide`. A core without `W7T_JumpListHitRow` falls back to the
  popup's window rectangle (FindWindow + GetWindowRect) for the release
  decision, so an older dist/ DLL degrades instead of breaking the gesture.
- The v2.61 arrow trigger (press on the arrow slot consumed in
  `TaskButton_PreviewMouseDown`, release opened through the same shared
  `OpenJumpListPopup`) was REMOVED in v2.62 on user request - the small
  triangle on the button should not exist. The XAML element it hit-tested no
  longer exists (`Themes/Overrides.xaml`), so the managed machinery, which
  locates the arrow by name and hit-tests its live layout slot, finds
  nothing and stays inert; it is kept so the trigger can be restored by
  re-adding the element.
- The opened list is persistent, like Windows 7: row clicks, Escape and
  click-outside come from the native popup once it owns ordinary input
  (`W7T_JumpListMakeInteractive`). The popup never behaves as a drag modal
  after the release, which is also what keeps it from fighting the icon
  reorder: the two gestures share a press but never a live pointer.
- The popup is a native `WS_POPUP | WS_EX_NOACTIVATE` window with the shared
  Aero flyout border (`native/src/JumpListWindow.cpp`), positioned once at
  open (the canonical Windows 7 place, per taskbar edge, clamped to the work
  area of the monitor under the button) and never moved again. During the
  drag it stays non-activating: WPF's capture keeps every mouse message on
  the taskbar window, so the popup never sees the drag's own up/down and the
  managed side drives its hover row explicitly.
- Data comes only from documented Shell APIs: application identity via
  `SHGetPropertyStoreForWindow` (window) and
  `SHGetPropertyStoreFromParsingName` (the pinned .lnk) for the
  AppUserModelID, and `IApplicationDocumentLists` for the Recent/Frequent
  destinations. When the Shell exposes no list, no document rows appear -
  nothing is invented.
- The content sections are RE-verified against the Windows 7 taskbar code
  (Windows Thin PC `explorer.exe` string/xref dump; full analysis in
  `docs/JUMPLIST-RE-VERIFICATION.md`). Recent/Frequent come from
  `IApplicationDocumentLists`. The **pinned (custom) section is NOT
  shown**: the dump evidences the Windows 7 taskbar reading the
  `HKCU\...\Explorer\ApplicationDestinations\<AppID>` store directly -
  the binary imports no destination-list COM interface at all - and MSDN
  confirms that on Windows 7 and later no public API reads or removes the
  pinned set (`IApplicationDestinations` only removes Recent/Frequent
  destinations; the pinned items "cannot be removed programmatically;
  only the user can remove them"). An early v2.62 iteration attempted the
  section through `IApplicationDestinations::GetObjectCount/GetObjectList`
  and an `ICustomDestinationList` GetObjectCollection/SetItemObjectList
  unpin; SDK compilation plus MSDN proved those method sets belong to the
  Vista revision of the interfaces, so the original "no public read API"
  comment was right and the iteration was withdrawn (section 7 of the
  RE note). The dump-evidenced `customopen` action is the row click
  (`ShellExecuteW` on the resolved path). The `Start_JumpListItems = 0`
  policy disables the jump lists (open code -4), and rows carry the
  Windows 7 path tooltip (name only when unresolvable - the
  `NoJumpListPathTooltip` case). The **Tasks** section
  (Minimize/Maximize/Restore/Move/Size for live window groups, reusing
  `WindowManager::ExecuteCommand`) is general Windows 7 knowledge - the
  dump carries no Tasks strings - and is flagged as such. Telemetry strings
  (`taskbarpin`/`startpin`/...) were recorded and deliberately NOT
  adopted.
- Every coordinate crossing the interop boundary is a **screen physical
  pixel**; the WPF side converts with `PointToScreen` only (both button
  corners, never a size multiplied by a scale a second time), and the native
  side scales its 96-DPI geometry with `GetDpiForScreenRect` for the monitor
  of the button.

**State of the managed side.** Whether the list is on screen is probed from
the window itself (`NativeBridge.IsJumpListPopupVisible`: class
`W7T_JumpList`, visibility, process id) instead of a new native export, so
a `dist/Win7TaskbarCore.dll` older than the sources keeps the bar alive -
the same "old core switches the novelty off" rule the AppBar protocol uses.
On such a core the rows and the hover still work (the native `WndProc`
answers them without the interactive bit) and the click-outside dismissal is
provided by the managed side with the `GlobalMouseHook` utility the clock
flyout already uses; Escape is not delivered there because the popup never
takes focus. Two ordering hazards are handled explicitly: a dismissal the
previous list posted to the reused popup window is peeled off the UI thread
queue right after each open (`PeekMessageW` filtered on the popup's own
message), and a click-outside callback still queued when a newer open
happens is made inert by an open-generation counter. Teardown
(`HideJumpList`) always releases the drag capture, the arrow capture, the
dismissal hook and the popup, so none of the group-removal, reorder-start,
theme-swap and window-close paths can leave a capture or a window behind.

**Why.** The v2.38/v2.40 shape (right-click opening a popup that grabs
foreground and reads the hardware cursor itself) fought the WPF capture
model, double-scaled coordinates at non-100% DPI, clamped only against the
primary monitor, and shipped the document list disabled; the v2.40 drag-up
that replaced it was then disabled again because it captured the same press
the icon reorder captured WITHOUT arbitrating between the two (either
threshold crossing started the OLE reorder, so dragging up reordered the
icons instead of opening the list - the exact conflict reported in the
field). The v2.61 arrow fixed the conflict but the trigger was not
discoverable and in the field the list did not appear as expected, so v2.62
restores the Windows 7 drag as the main trigger and fixes the conflict at
its root: one press, two candidates, first threshold wins, capture taken
only after the winner is committed. The v2.62-alpha had also carried the
cursor-following placement of the GPL-3.0 Windhawk mod
"taskbar-jump-list-on-cursor-pos" (m417z) - the list re-anchoring under the
cursor during the drag - and removed the arrow in the same round; the alpha
test verdict was that the position did not match the real Windows 7 shell,
which opens the jump view NEXT TO THE BUTTON (left-aligned above it), not
where the cursor is. The final v2.62 therefore keeps the drag trigger and
the arrow removal, and drops the cursor-following in favor of the shell's
canonical button-anchored placement. One choke point per responsibility
(arming, arbitration, open, hover, release, hand-over, teardown) is what
lets every failure path end quietly: capture always released, hook always
stopped, popup always hidden, exceptions always logged under the `JUMPLIST`
tag.

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

## 15. Extra settings: one configuration, one tab, no parallel system

**Decision.** The secondary options added in v1.21.7 (flyout colour for the
recreated flyout, privacy mode of the recreated connection flyout, skin
selection, icon order) are ordinary entries of the existing configuration:
fields in `src/RetroBar.Shim/Utilities/Settings.cs`, persisted in the same
`%AppData%\Win7Taskbar\settings.json`, edited in a fourth tab of the same
native Properties dialog (`Impostazioni extra`), published to the core with a
single export (`W7T_SetExtraSettings`) and labelled by the same 11 language
dictionaries. No new file, no registry key, no parallel settings window.

**Why.** A second configuration source would immediately diverge from the
first one on unpin/uninstall/language change, and would have to re-implement
atomic save, first-run detection and the language table. Extending the
existing one keeps every option testable with the same tools and makes the
whole tab removable in one commit.

**Consequences.** The native dialog only *reports* what the user picked
(`PropsApplyMsg`, appended tail fields - the receiver reads only the fields
the packet actually carries); the managed side validates, saves and
republishes. Settings that have no consumer in this build (the flyout colour)
are stored and queryable but change nothing, which is stated in
`docs/FEATURE-STATUS.md` instead of being silently applied to a flyout the
setting does not belong to.

**Revisit if.** A skin or flyout arrives that needs its own assets and
defaults: the plug-in point is the theme id → file map in `ThemeLoader` plus
`TaskbarThemeIds.IsImplemented`, not a new settings system.

## 16. Icon order is a layer above pinning - never Explorer's own order

**Decision.** Reordering the taskbar icons changes only the order in which the
software taskbar lays out its own buttons. Discovery stays what it always was
(shell pin folder first, then running applications) and is reordered by
`Models/VirtualTaskbarOrder.cs` from an ordered list of stable keys
(AppUserModelID, else executable path, else the launch `.lnk`) stored in the
same `settings.json`. The gesture is ordinary WPF mouse capture on the button
(the tray-drag mechanism): threshold, ghost icon, hand hit-test and an
insertion caret drawn as a 2 px window over the target button.

**Why.** The pinned-items folder, the shell shortcuts and the real taskbar
belong to Windows: writing to them from a reimplementation would change the
user's actual taskbar configuration, would be lost on the next Explorer
restart, and could not be undone by uninstalling the program. An internal
list keyed by application identity is reversible, survives restarts, has no
effect outside the program, and does not replace the pinning system - it is
applied on top of it. Keys that no longer resolve are kept, so a temporarily
missing application does not lose its place and no configuration is destroyed.

**Consequences.** Items with no stable identity are never reordered and stay
where discovery puts them; a drag that does not end as an exact permutation of
the shown buttons is discarded without touching the configuration; an empty
list is byte-for-byte the old behaviour. Ordering remains per-user because the
configuration is.

**Revisit if.** Windows exposes a documented per-application identity for
taskbar items that survives reinstall (for example a stable package identity
for all Win32 apps): the key function in `VirtualTaskbarOrder` is the single
place to change.

## 17. v1.21.32: the taskbar-list protocol is answered, not ignored

**Decision.** The window that registers the `Shell_TrayWnd` class (the tray
window, see `TrayService.cpp`) answers the private taskbar query
`WM_USER + 236` with a live window of its own (`W7T_TaskSwitch`, hidden and
owned by the tray window), and that window turns the shell-hook codes it then
receives into `ITaskbarList::AddTab` / `DeleteTab` / `ActivateTab` effects on
the window model (`HSHELL_WINDOWCREATED` / `HSHELL_WINDOWDESTROYED` /
`HSHELL_WINDOWACTIVATED`; `Common.cpp` keeps the resulting per-window
override and `IsTaskbarWindow` consults it).

**Why.** Microsoft documents `CLSID_TaskbarList` / `ITaskbarList` as
implemented by the shell ("You do not implement ITaskbarList; it is
implemented by the Shell"), and its methods' notes add that any type of window
can be added to the taskbar, and that a window added with `AddTab` must be
removed with `DeleteTab`. The shell implementation reaches the taskbar through
the class-name lookup that also serves `Shell_NotifyIcon`, so a program that
registers `Shell_TrayWnd` for its own notification area also receives the
taskbar-list calls. `ITaskbarList::HrInit` requires a non-zero answer from
that query, and the callers are toolkits that run it while they are building a
window (tao/Tauri, Chromium/Electron): an unanswered or failing taskbar list
surfaces as an application that opens late, opens without a button, or does not
open at all. Answering the query with a live window and honouring the two calls
keeps those applications compatible with this taskbar instead of merely
tolerating them.

**Consequences.** `DeleteTab` removes a button that the heuristics would have
shown (the documented way an application keeps a window off the taskbar, e.g.
`skip_taskbar`/`setSkipTaskbar`); `AddTab` publishes a window the heuristics
would have dropped, but never a hidden, cloaked or child window, so the style
filters that protect the bar (`WS_EX_TOOLWINDOW`, `WS_EX_NOACTIVATE`,
`WS_CHILD`) still apply first and no service window can be promoted into a
button. The response to the query takes no lock, touches no model and never
waits, because it is sent from another process's window build. An empty title
is now accepted for a window that carries `WS_CAPTION` (and a newly titled
window joins the bar on the name-change event, not on the next full
enumeration), which is what the same documentation recommends for taskbar
windows.

**Revisit if.** Windows documents a public replacement for the class-name
lookup (a documented query or interface for third-party taskbars): the query
handler and `TaskSwitchWndProc` are the only two places to change.

## 18. v1.21.36: Color hot-track as a small spot on open programs only

**Decision.** The hover look of a task button is left exactly as it was before
the feature (the tray hover tile plus the theme's Aero glow,
`Themes/Overrides.xaml`). On top of it, and only for a program that is **open**
(running, active or flashing), a separate `Hotlight` element paints a **small
spot** of the application colour - the "Color hot-track" Microsoft documents
(Raymond Chen, The Old New Thing, 2011-12-06: the button "lights up in a color
that matches the colors in the icon itself", "the lighting effect is centered on
the mouse", and "the code just looks for the predominant color in the icon [...]
black, white, and shades of gray are not considered 'colors'") - with its centre
moved to the cursor by `TaskButton_MouseMove`. Its size is the two
`SpotRadiusX`/`SpotRadiusY` constants of `Utilities/HotlightColor.cs` and its
weight is the opacity the theme animates (`0.30` on hover, `0` when the mouse
leaves). Pinned buttons that are not running carry no accent at all.

**Why.** The scope of this feature took several rounds of real use to settle,
and each round is recorded here because each was a correction of the previous
one. v1.21.33 replaced the hover tile everywhere with a glossy glass square and
tinted every button at full strength: rejected, because Windows 7 was far more
moderate and the light belonged to open programs. v1.21.34 restored the hover and
reduced the colour to a 5% accent, open programs only: too faint to be seen.
v1.21.35 kept the restored hover and the open-programs-only scope and set the
accent at 12%. v1.21.36 raises the weight to 30% on request, and leaves the spot
small, so the accent is clearly visible without becoming a wash over the tile.

**Consequences.** The `Hotlight` element is a no-op when it is not wanted (it
starts at opacity 0, and only the running/active/flashing templates animate it,
so a pinned program cannot light up by accident). Weight and size are independent
and each has a single home: the animations in `Overrides.xaml` for the weight,
the two radii in `HotlightColor.cs` for the size. The colour extraction is
unchanged and still cannot break a hover (an unsamplable icon falls back to the
neutral white-blue light), and no tile, border or brush of the original hover was
replaced.

**Revisit if.** The weight is still not right on real hardware: the marked
`To="0.30"` animations are the single place to change. If the accent should also
appear on pinned programs, the guard is the single `group.IsRunning` condition in
`TaskbarWindow.xaml.cs`.

## 19. v2.62-alpha: the Jump List section cap follows the user's shell configuration

**Decision.** The Recent/Frequent section cap is no longer a compile-time
constant: `JumpListWindow::ReadDocumentLists` asks
`GetJumpListSectionCap()` (cached; the tray window drops the cache on
`WM_SETTINGCHANGE`) and the value is resolved in this order -
`HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\ApplicationDestinations\MaxEntries`
(the per-application destination count), else
`HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\Advanced\Start_JumpListItems`
plus the four fixed rows the shell keeps for its standard entries, else the
project's internal default (ten, exactly today's behaviour). A tampered
value is clamped to 1..64 - project safety bounds, not the shell's. The
registry is only ever read; the read happens at most once per configuration
change (never on the hot path), and the log line declares which source
answered.

**Why.** Windows 7 sizes these sections from the user's own shell
configuration, and a user who configured it deserves to have the
reconstructed bar honour it. The read-only, cached, clamped design keeps the
fail-safe rule: with no value the bar behaves exactly as before, and no
registry failure or absurd value can worsen the current behaviour (the
worst case is the internal cap of ten).

**Consequences.** No ABI change (the cap never crosses the interop boundary),
no new public API. The 3/4 reduction Windows applies in its compact display
mode is deliberately not replicated: it exists to shrink a display the popup
does not have (its geometry is 96-DPI reference values scaled once by the
monitor DPI). **Not verified on real hardware**: the value's presence and
effect on the test machine (the project's probe script reports it) still need
to be checked; when the value is absent the behaviour is identical to before
by construction. Revisit if a real machine shows the cap must be resolved
per-section rather than per-open.

## 20. v2.62-alpha: the preview policy follows the user's configuration (G2)

**Decision.** The core reads (read-only, cached, invalidated by the tray
window on `WM_SETTINGCHANGE`) the user's own preview configuration from
`HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\Advanced`: the two
disable switches (live window previews, desktop peek) and the hover times.
The frontend consumes them at the existing decision points: the preview
popup's single choke point (`ShowTaskPreview`) returns early when the user
disabled the window previews; the thumbnail popup's first-open delay is the
user's `ThumbnailLivePreviewHoverTime` when set (the timer is rebuilt only
when the delay changes) and the project's measured 400 ms otherwise; the
show-desktop hover peek is suppressed when the user disabled the desktop
preview (the click still minimizes) and uses the user's
`DesktopLivePreviewHoverTime` as its delay when set (immediate, as today,
otherwise). The system-side "live preview allowed" gate uses a shell helper
that is not a public API: it is NOT replicated (fail-open) and the policy
log says the gate was not evaluated. No value anywhere => behaviour
identical to before (the fallbacks are the project's current defaults, not
observed numbers).

**Why.** Windows consults exactly these values before drawing the live
thumbnails and the peek; a user who configured them deserves to have the
reconstructed bar honour them, and the cached/absent-safe design keeps the
fail-safe rule: a missing value or an old core changes nothing.

**Consequences.** One new export (`W7T_GetPreviewPolicy`, ABI append-only;
a core without it is tolerated: the frontend keeps its defaults), one new
native module (read-only), no preview geometry touched.
`ExtendedUIHoverTime` is read, exposed and logged but has no consumer: the
project has no extended-UI equivalent. **Not verified on real hardware**:
the effect of real values on the test machine still needs a check; the
absent-value path is identical to before by construction. Revisit if the
user delay must also apply to the preview switching (today it applies to
the first open only, as the project does now).

## 21. v2.62-alpha: TaskbarButtonCreated and the taskband probe (G7)

**Decision.** When the bar owns the taskbar (the native taskbar is hidden),
the core registers the "TaskbarButtonCreated" message (RegisterWindowMessage,
one id per process) and broadcasts it to `HWND_BROADCAST` when a new
application window enters the bar - at most once per process id, and only
while the bar is the taskbar. The per-pid dedup is capped and reset when
the native taskbar becomes visible again. A diagnostic probe (environment
`W7T_TASKBAND_PROBE=1`) makes the tray window log - tag `PROBE`, log-only,
no reply - the registered id at start-up and every reception of that exact
id, so a machine running the real taskbar can show whether the shell itself
sends the message and via which id.

**Why.** Shell-aware tooling expects to be told about new taskbar buttons
through that message; a bar that owns the taskbar should honour the
contract. The probe answers a question that cannot be answered from this
machine: does the real shell broadcast it too, and with which id - the
information decides how a coexistence mode would behave.

**Consequences.** No ABI change, no managed change, no registry access. The
broadcast is fire-and-forget (a dead receiver cannot slow the window
enumeration). **Not verified on real hardware**: the probe's log on a
real machine is the open point; with the native taskbar visible nothing is
sent (by design, the shell owns the buttons then).

## 22. v2.62-alpha: TaskbarGlomLevel and small icons (G3)

**Decision.** Behind the settings switch `TaskbarGroupingPolicy` (default
OFF: with it off, and with the values absent, the behaviour is exactly the
current one), the model reads the user's own grouping configuration
(read-only, once per refresh, both candidate keys probed and the answering
one logged): `TaskbarGlomLevel` (clamped to 0..2, absurd values never
worsen the behaviour) and `TaskbarSmallIcons`. `TaskbarGlomLevel 0`
(never group) makes every window its own button: the grouping key becomes
per-window, such groups never attach to a pin, and the group commands use
the real AppId that stays on the windows (`EffectiveAppId`). Levels 1 and 2
leave the current grouping as is - the project's AppId grouping already
implements "group similar / always", which is the only distinction the bar
can make - and the mapping is documented rather than silently assumed.
`TaskbarSmallIcons` is read, exposed and logged but NOT applied: applying
it means a button geometry redesign (content-sized buttons, a different
minimum width), which is deferred until it can be measured on hardware
rather than guessed. (Gap G5 - the tooltip of a jump list row without a
path - needs no code: the existing tooltip already falls back to the name
when the path is absent, which is exactly the Windows 7 behaviour.)

**Why.** A user who configured the grouping level deserves to have the
bar honour it; the switch keeps the project in control of the rollout, and
the per-window identity is carried where the pipeline can use it without
touching the pin logic.

**Consequences.** The new buttons of a never-grouped app are single-window
groups (an existing capability, already used by idle pins); the group
minimize/close of such a group act on the window's real AppId; everything
else is untouched. **Not verified on real hardware**: the switch is off by
default and the absent-value path is identical to before by construction.

## 23. v2.62-alpha: the group icon criterion and the exceptions (G4)

**Decision.** Behind the same switch (`TaskbarGroupingPolicy`, default
OFF), the model reads the user's group-icon policy (read-only, once per
refresh, both candidate keys, the answering one logged):
`UseExecutableForTaskbarGroupIcon` - when set, the group button shows the
executable's own icon (new appended export `W7T_GetExeIconBitmap`, the
project's single icon pipeline; the pin's identity icon always wins) - and
`TaskbarExceptionsIcons` - the listed executables' windows are never
grouped (per-window groups, same mechanism as ADR 22). `TaskbarGroupIcon`
is read and logged only: its meaning is not fully documented, and the
project does not apply values it cannot explain.

**Why.** Those two values are the documented user controls over "what
decides the group icon" and "which apps must not be grouped"; honouring
them keeps the bar from contradicting an explicit choice, and the
read-and-log of the third one preserves the evidence for a future decision
instead of guessing.

**Consequences.** One new export (ABI append-only; a core without it is
tolerated: the icon falls back to the current one), no layout change.
**Not verified on real hardware**: the switch is off by default; the
absent-value path is identical to before by construction; the
`TaskbarGroupIcon` semantics remain an open question.

## 24. v2.62-alpha: the canonical pin verbs and the single .lnk write point (G6)

**Decision.** Behind the settings switch `CanonicalPinVerbs` (default OFF),
pin/unpin from the bar travels on the canonical native path: the shell's
canonical verbs are recognised (ordinal, case-insensitive: taskbarpin /
taskbarunpin / togglepin are executed; open / customopen / delete / runas
travel on the shell's own ShellExecute path; startpin / startunpin are
recognised and deliberately NOT executed - the Start menu belongs to the
shell). The .lnk of the real pin folder is now written in exactly ONE
place (PinVerbs, shared by the Jump List pin row, which was refactored onto
it), and the model refresh - with `W7T_EVT_PINNED_CHANGED` - is the
PinnedApps folder watcher's job, which fires only when the model actually
changed. With the switch off, the historical managed path (WScript.Shell)
stays byte-identical.

**Why.** Two writers of the same .lnk (the jump list and the right-click
menu) can drift; one write point makes "pinned" a single fact on disk. The
switch keeps the change reversible on the field, and the Start-menu verbs
stay out of scope by the project's clean-room rules.

**Consequences.** The Jump List pin row and the menu pin share one
implementation; the event semantics are unchanged (watcher-driven, only on
real change). The native ShellMenu has no named-verb dispatch (numeric ids
only), so there is nothing to hook there - stated in the PR, not faked.
**Not verified on real hardware**: the switch is off by default; the
off-switch path is the current code.
