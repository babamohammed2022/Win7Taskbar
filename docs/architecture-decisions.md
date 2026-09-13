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

**Decision.** `Themes/Windows7.xaml` stays byte-identical to the upstream theme and is
loaded at runtime by `ThemeLoader.cs`, which merges a base dictionary *in memory* before
parsing.

**Why.** The theme is an upstream work and a compatibility target: keeping it untouched
makes it possible to drop in a newer version of the theme without touching the code. But it
has 16 `BasedOn` styles that expect a base dictionary with same-named keys, and WPF skips
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

## 5. Window previews: disabled until they can be verified

**Decision.** The preview popup is not opened at all (`TaskPreviewsEnabled` in
`TaskbarWindow.xaml.cs`); hovering a task button shows the app-name tooltip only. Both
previous implementations are kept, commented out, in `Controls/TaskThumbnail.cs`.

**Why.** *Unwanted rectangles* inside the popup were reported on real hardware in both
variants. The live DWM thumbnail (up to v2.54) can fail **silently**: `DwmRegisterThumbnail`
succeeds and the compositor then never paints, so the only thing visible is the popup
backdrop, identical for every window. The static `PrintWindow` capture (v2.55) fails
*loudly* (it returns `FALSE` and we fall back to the app icon), but a `TRUE` return still
does not prove that the captured surface is the window content - several applications
answer with an empty or stale surface - and the result also cannot move, which makes the
popup feel frozen.

**What a future implementation has to prove before this decision is reversed.**

1. A **positive** confirmation that real content was drawn (a pixel read-back, a
   non-uniformity check), not just a successful return code.
2. A documented fallback that is invisible to the user: if the check fails, the popup must
   look intentional (icon + title), not like an empty box.
3. A single switch to turn the feature back on, so it can be tested per machine without
   shipping it half-broken.

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

## 11. v1.3.0-alpha: the input indicator is a full port, hosted by a thin managed slot

**Decision (proposed fix, batch C).** The input language indicator stops
being a WPF control that draws its own text. The three Windhawk mods that
shaped the indicator on Windows (layout control, more space, fix rotated
text) are ported one-to-one into the native core (`LanguageBar.cpp`), which
creates the same window structure Windows uses: a `TrayInputIndicatorWClass`
frame containing an `InputIndicatorButton` text child (recursive child
search and cache, as in the reference port). The managed side keeps only the
layout slot and forwards the on-screen rectangle (`W7T_LangBarPlace`).

Ported behaviours: the four layout-control modes (keepLayoutOnly / hide /
show / windowsDefault) with the inverted `SPI_GETSYSTEMLANGUAGEBAR` reading
and the BOOL-cast-to-`PVOID` `SPI_SETSYSTEMLANGUAGEBAR` write; the temporary
hide (Remote Desktop in the foreground) is redirected to the text child so
the tray never jumps; frame height never goes below 32 px (the DeferWindowPos
rule of the more-space mod, enforced in `WM_WINDOWPOSCHANGING`); the code is
drawn straight, centred with `DrawTextW(DT_CENTER|DT_VCENTER|DT_SINGLELINE|
DT_NOPREFIX)`, only when it is a 2-4 letter alphabetic code, on a background
sampled from the taskbar corner pixel with a `COLOR_BTNFACE` fallback (the
fix-legacy mod's drawing rule). The ManagedShell mechanics stay: a 200 ms
poll of the foreground thread's `HKL` and layout switching via
`LoadKeyboardLayout(KLF_SUBSTITUTE_OK|KLF_ACTIVATE)` plus a
`WM_INPUTLANGCHANGEREQUEST` broadcast; the picker menu lists installed
layouts plus the four layout-control choices, translated from the core
tables (all 11 languages).

**Why.** A recreated indicator drawn by WPF could not reproduce the mods'
behaviours (they depend on the real window structure and on the system
setting). The user required a complete port, with only the on-taskbar text
drawn by our port.

**Revisit if.** Windows changes the indicator classes again or removes the
`SPI_SETSYSTEMLANGUAGEBAR` effect: the policy engine is isolated in
`LanguageBar.cpp` and can follow.
