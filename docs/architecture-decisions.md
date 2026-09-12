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
