# Balloon notifications (`NIF_INFO`): Win7Taskbar vs ManagedShell — code review

Scope: how this project and ManagedShell (the library RetroBar is built on) take a
`Shell_NotifyIcon` balloon notification apart, what they keep, what they show, how
they behave when two notifications arrive together, and which edge cases each one
handles.

Every claim about **this repository** is quoted with file and line as they are at
commit `4c82492` of `main`. Every claim about **ManagedShell** is quoted from the
upstream sources, which are *not* vendored here (see "Sources and verification").

**Status.** The managed side of §5/§6.5 has since been implemented in
`src/Win7Taskbar/Controls/NotificationBalloon.cs` (`NotificationBalloon` with a
`Handled` flag, `BalloonQueue` with a bounded FIFO and the per-icon
`MissedNotifications`, `IBalloonSurface` implemented by `TaskbarWindow`, the
v1.21.41 behaviour kept only as `ShowBalloonLegacy`). The **native** half — §6.1
to §6.4, the balloon queue in `TrayService`, `hBalloonIcon`, the documented
removal path — is still open: it changes the ABI of `W7T_BalloonInfo` and this
sandbox has no Windows toolchain, so it cannot be compiled or shipped with a
matching `dist/Win7TaskbarCore.dll`. Until it lands, the core still holds one
balloon slot, so two `NIF_INFO` packets inside the same 100 ms pump window can
still overwrite each other before the managed queue sees them.

---

## 0. Sources and verification

| Side | What was read | Reference |
| --- | --- | --- |
| Win7Taskbar | working tree, 215 files | branch `arena/01a0bbc2-win7taskbar`, base commit `4c82492` |
| ManagedShell | `src/ManagedShell.WindowsTray/{TrayService,NotificationArea,NotifyIcon,NotificationBalloon}.cs` | `cairoshell/ManagedShell` @ `master`, tree SHA `855222035e927910ca53fd3215caef056337a048` |

ManagedShell is **not** in this repository. `grep -ril managedshell` matches only
prose and comments; `src/Win7Taskbar/Win7Taskbar.csproj` has exactly one
`ProjectReference` (`..\RetroBar.Shim\RetroBar.Shim.csproj`, line 406) and no
`PackageReference` at all. `docs/THIRD-PARTY-NOTICES.md` (lines 137–147) records
ManagedShell as the *behavioural* reference for the balloon work of v1.21.39, not
as a dependency. Line numbers below for ManagedShell are therefore quoted from
upstream at that commit and must be re-checked if upstream moves.

Checks actually run while writing this review:

```
python3 tools/verify-theme-xaml.py            -> exit 0, 6 lines OK
python3 tools/verify-preview-frame.py         -> exit 0, "RESULT: the preview frame is a single colour"
python3 native/tools/run-check.py check-exports.py dist/Win7TaskbarCore.dll -> exit 1
```

The third one fails **at HEAD, before any change**: the shipped
`dist/Win7TaskbarCore.dll` exports 104 `W7T_` functions while the sources declare
115 — `W7T_GetExtraFlyoutColor`, `W7T_JumpListMakeInteractive`, the five
`W7T_Net8Flyout*`, `W7T_RenderAeroThumbnailFrame`, `W7T_SetExtraSettings`,
`W7T_SetWin8NetworkFlyout`, `W7T_ShowPinMenu`, `W7T_ShowTaskManagerMode` are
missing, and `W7T_TrayCplShow` is exported but no longer declared. The tracked
DLL is stale with respect to `native/src/`. That is a pre-existing finding of this
review and it constrains section 7: any change to the native ABI has to be shipped
with a rebuilt DLL or it breaks at `EntryPointNotFoundException`.

---

## 1. Comparison table

| Aspect | Win7Taskbar | ManagedShell |
| --- | --- | --- |
| Interception | Registers the `Shell_TrayWnd` class and creates its own tray window: `TrayService::CreateWindows()` (`native/src/TrayService.cpp:1616`–`1645`, class name at `:1623`, window at `:1640`). `TrayWndProcInner` routes `WM_COPYDATA` to `HandleCopyData` (`:1904`–`1906`). No hook into another process. | Same mechanism: `TrayService.cs` `TrayWndClass = "Shell_TrayWnd"`, `NotifyWndClass = "TrayNotifyWnd"`, `RegisterTrayWnd()` / `RegisterNotifyWnd()` from `Initialize()`, `WndProc` switches on `WM.COPYDATA`. No hook into another process. |
| Which tray receives the calls | Decided once, at `CreateWindowExW` with `WS_EX_TOOLWINDOW \| WS_EX_TOPMOST` (`:1641`). **No watchdog re-asserts it.** | Actively arbitrated: `Resume()` calls `SetWindowsTrayBottommost()` then `MakeTrayTopmost()` and starts the `trayMonitor` `DispatcherTimer`; `Suspend()` pushes the window to `HWND_BOTTOM` with the comment *"if we go beneath another tray, it will receive messages"*. |
| Re-registration prompt | `TaskbarCreated` is **listened to only** at start (`:1763`–`1771`, with the reason: the outgoing broadcast caused double registration). One fallback broadcast exists, once per session, only when an icon is confirmed gone while its owner is alive (`:1120`–`1126`). | `SendTaskbarCreated()` broadcasts to `HWND_BROADCAST` from `Run()` and again from `Dispose()`. |
| Wire decoding | Hand-written layouts `NidLayout64` (976 B, `:248`–`266`) and `NidLayout32` (956 B, `:271`–`287`); bitness chosen from `cbSize` in `NormalizeNid` (`:2354`–`2413`); a third Wine-only layout `WineTrayIconData` (960 B, `:2279`–`2296`) in `NormalizeNidWine` (`:2302`). | `Marshal.PtrToStructure<SHELLTRAYDATA>` then `new SafeNotifyIconData(trayData.nid)` in `WndProc`, `case 1`. |
| `dwData` handling | `0` AppBar (`:2140`), `1` tray icon (`:2152`), `3` `Shell_NotifyIconGetRect` answering through the copy-data buffer (`:2212`–`2239`), plus the Wine shape where `dwData` *is* the `NIM_*` code (`:2244`). | `0` AppBar (`APPBARMSGDATAV3`), `1` tray icon, `3` `WINNOTIFYICONIDENTIFIER`. Same three, no Wine shape. |
| Fields kept from `NIF_INFO` | `dwInfoFlags`, `uTimeout` (from the `uVersion` union member, gated on `NIF_INFO`), `szInfo`, `szInfoTitle`, plus owner `hWnd`/`uID` — into the single slot `m_lastBalloon` (`:2570`–`2577`). **`hBalloonIcon` is dropped.** | `szInfoTitle`, `szInfo`, `dwInfoFlags`, `uVersion` as timeout, **`hBalloonIcon`** (used directly as an `ImageSource`), plus a back-reference to the `NotifyIcon` — into a `NotificationBalloon` object. |
| Presentation | Already implemented as a WPF `Popup` with `AllowsTransparency`, `Focusable = false`, custom placement: `Controls/BalloonHost.cs:144`–`160`, content in `Controls/NotifyBalloon.xaml`. **Currently switched off** by `TaskbarWindow.BalloonNotificationsEnabled = false` (`TaskbarWindow.xaml.cs:1583`, gate at `:1617`). | No presentation in the library. `NotificationBalloon` is a data object; the consumer (RetroBar) draws it. |
| Queueing | None. One `W7T_BalloonInfo m_lastBalloon` slot + `bool m_hasBalloon` (`TrayService.h:524`–`525`); `GetLastBalloon` `memcpy`s it and never clears the flag (`:3764`–`3774`). Managed side replaces the visible balloon (`BalloonHost.Show` calls `Hide()` first, `:121`). | Per-icon `ObservableCollection<NotificationBalloon> MissedNotifications` (`NotifyIcon.cs`); `TriggerNotificationBalloon` adds to it when the raised event is not marked `Handled`. |
| `NIN_BALLOON*` feedback | `NotifyBalloon.SendFeedback` → `TaskbarWindow.BuildBalloonFeedback` (`:1750`–`1770`), `SendNotifyMessage`, version-4 `HIWORD(lParam)` layout. Dead code while the kill-switch is off. | `NotificationBalloon.SetVisibility` / `Click()` → `NotifyIcon.SendMessage` → `SendNotifyMessage` with the same version-4 split. |
| `uTimeout` policy | Request honoured when ≥ 1000 ms, else `SPI_GETMESSAGEDURATION`, then clamped 4–30 s (`NotifyBalloon.xaml.cs:176`–`201`). | Request honoured when ≥ 1000 ms, else `SPI_GETMESSAGEDURATION`. **No clamp.** |
| `NIF_REALTIME` / `NIIF_RESPECT` | Not referenced anywhere (`grep -rn NIF_REALTIME` and `NIIF_RESPECT`: 0 hits repo-wide). | Not referenced in `NotificationBalloon` / `NotificationArea` either. |
| Balloon removal (`szInfo` empty) | Native refuses to raise it (`:2562` requires `!nid.szInfo.empty()`); managed hides any visible balloon when both strings are blank (`BalloonHost.cs:115`–`119`). | `handleBalloonData` returns only when `szInfoTitle` is empty; a non-empty title with empty `szInfo` still produces a `NotificationBalloon`. |

---

## 2. Interception: does being a native tray host change the approach?

**No — the interception mechanism is the same, and that is the interesting part of
the comparison.** Win7Taskbar is not hooking anybody: it *is* the tray host that
`Shell_NotifyIcon` looks for.

* `TrayService.cpp:1623` registers `L"Shell_TrayWnd"`, `:1640` creates the window,
  `:1642` names it, `:1653` adds the `TrayNotifyWnd` child "expected by some
  applications".
* `TrayService.cpp:2130`–`2139` documents the protocol it answers, and names the
  evidence: `shell32!Shell_NotifyIconA` does `FindWindow("Shell_TrayWnd")` +
  `SendMessage(WM_COPYDATA, ...)`.
* `docs/architecture-decisions.md:422`–`425` states the same for the taskbar-list
  protocol: "The shell implementation reaches the taskbar through the class-name
  lookup that also serves `Shell_NotifyIcon`".

ManagedShell does exactly this in C#: `TrayService.cs` holds
`TrayWndClass = "Shell_TrayWnd"` / `NotifyWndClass = "TrayNotifyWnd"`, registers
both, and its `WndProc` decodes `dwData` 0 / 1 / 3 the same way — including
marshalling `SHELLTRAYDATA` and wrapping `nid` in `SafeNotifyIconData`.

Two consequences follow, and they are where the projects actually differ:

1. **Z-order arbitration.** Because two `Shell_TrayWnd` windows can coexist (ours
   and Explorer's), who answers is decided by window order, not by code.
   ManagedShell treats this as a live problem: `Resume()` re-asserts the topmost
   position and a `DispatcherTimer` (`trayMonitor`) keeps doing it, with the
   comment *"if we are above another tray, we will receive messages"*. Win7Taskbar
   sets `WS_EX_TOPMOST` once at `:1641` and has no equivalent watchdog — the
   `HWND_TOPMOST` calls in `AppBarService.cpp` (`:571`, `:939`, `:1012`) act on the
   *bar*, not on `m_trayWnd`. If our tray window ever ends up below Explorer's,
   `NIF_INFO` goes to Explorer and this project sees nothing at all, silently.
   This is not observable from the sources; it is a Windows-behaviour question
   (see section 8) but it is the cheapest hypothesis to test for "notifications
   that never arrive".

2. **Re-registration prompt.** ManagedShell broadcasts `TaskbarCreated` when it
   takes the tray over (`Run()`) and when it gives it back (`Dispose()`), so
   applications re-send `NIM_ADD` — and with it their balloons. Win7Taskbar
   deliberately does not (`:1763`–`1771`) because the outgoing broadcast produced
   duplicate icons; it imports the pre-existing icons from Explorer's toolbar
   instead (`ExplorerTrayReader`). Those imported entries carry **no balloon
   state**: `ExplorerTrayReader.cpp` and `Win11TrayReader.cpp` contain no
   `NIF_INFO`, `szInfo` or balloon code at all (0 grep hits in both). So for an
   icon that was already registered before we started, the first balloon we can
   ever see is the next `NIM_MODIFY` with `NIF_INFO` — which is correct, but worth
   stating because it is a real difference in coverage versus a shell that forces
   re-registration.

---

## 3. Fields kept, fields lost

`NormalizedNid` (`TrayService.h:323`–`344`) carries `dwInfoFlags`, `uTimeout`,
`szInfo`, `szInfoTitle`. The normalisers fill them:

* 64-bit: `TrayService.cpp:2377`–`2384`
* 32-bit: `:2400`–`2407`
* Wine: `:2344`–`2349`

Both Windows paths contain the same union note, and it is correct:
`out.uTimeout = (nid.uFlags & NIF_INFO) ? nid.uVersion : 0;` (`:2380`, `:2403`).
`uTimeout` and `uVersion` share storage in `NOTIFYICONDATAW`, so reading the
timeout only when `NIF_INFO` is set is the right reading (Microsoft documents
`NIF_INFO` as "The `szInfo`, `szInfoTitle`, `dwInfoFlags`, and `uTimeout` members
are valid").

**What Win7Taskbar loses relative to ManagedShell:**

1. **`hBalloonIcon` — the only genuine loss.** Both wire layouts declare the field
   (`:265`, `:286`) and neither normaliser copies it; `NormalizedNid` has no member
   for it, so it cannot be recovered downstream. ManagedShell uses it as the
   primary source for `NIIF_USER`:

   ```csharp
   else if (Flags.HasFlag(NIIF.USER)) {
       if (nicData.hBalloonIcon != 0)      SetIconFromHIcon((IntPtr)nicData.hBalloonIcon);
       else if (nicData.hIcon != IntPtr.Zero) SetIconFromHIcon(nicData.hIcon);
   }
   ```

   The comments in this repository claim the handle "cannot cross the process
   boundary" (`NotifyBalloon.xaml.cs:222`–`224`, `TaskbarWindow.xaml.cs:1718`).
   That reasoning does not hold for this code path, and the same file proves it:
   `NIF_ICON` is handled at `:2518`–`2545` by `CopyIcon(source)` on the sender's
   `hIcon`, with the comment "the handle belongs to the sending process, which may
   destroy it: we take our own copy". `WM_COPYDATA` duplicates USER handles into
   the receiver; `hBalloonIcon` is a USER handle of exactly the same kind. The
   current `NIIF_USER` fallback to the tray icon (`TaskbarWindow.xaml.cs:1723`–`1727`)
   is ManagedShell's *second* choice, not its first.

2. **`NIF_SHOWTIP` / `NIF_REALTIME` — not handled, but not a regression.** 0 hits
   repo-wide. ManagedShell does not handle them either. Parity, not a gap.

3. **One thing Win7Taskbar does better.** ManagedShell reads the icon *version*
   from the same union on every ADD/MODIFY:

   ```csharp
   if (nicData.uVersion > 0 && nicData.uVersion <= 4)
       trayIcon.Version = nicData.uVersion;
   ```

   Because the field holds `uTimeout` whenever `NIF_INFO` is set, a balloon that
   requests 3 or 4 **milliseconds** overwrites the icon's version, which then
   changes the `wParam`/`lParam` layout of every subsequent callback
   (`GetMessageHiWord` / `GetMessageWParam`). Win7Taskbar only assigns the version
   in `NIM_SETVERSION` (`:2604`–`2611`) and never in the ADD/MODIFY path, so it is
   not exposed. Do not port that line.

`W7T_BalloonInfo` (`native/include/Win7TaskbarCore.h:157`–`165`, `#pragma pack(8)`
at `:131`) already has a `uint32_t reserved` at `:162`, and the managed mirror
`W7TBalloonInfo` (`Interop/NativeMethods.cs:157`–`171`, `Pack = 8`) has the
matching `Reserved`. Section 6 spends that field.

---

## 4. Presentation: it already exists, and it is switched off

The premise "if Win7Taskbar does not have this part yet" does not match the tree.
The balloon renderer is written, themed and documented:

* `Controls/BalloonHost.cs` — 576 lines: `Popup` with `AllowsTransparency = true`,
  `StaysOpen = true`, `Focusable = false` (`:144`–`160`); two placement callbacks
  (`PlaceTipOnIcon` `:488`, `PlaceAboveAnchor` `:545`); a 400 ms watchdog
  (`AnchorWatchInterval`, `:65`) that re-checks the anchor and forces the real
  position with `SetWindowPos` (`EnforceScreenPosition`, `:329`); work-area
  clamping in `TryComputeDesiredPosition` (`:384`–`460`).
* `Controls/NotifyBalloon.xaml` + `.xaml.cs` — the Aero bubble: title, body,
  vector close button (no Marlett dependency, `NotifyBalloon.xaml:12`–`16`),
  `NIIF_*` icon selection (`NotifyBalloon.xaml.cs:203`–`245`), `SystemNotification`
  sound unless `NIIF_NOSOUND` (`:159`–`162`), `NIN_BALLOON*` feedback (`:306`–`331`).
* Icon promotion while a balloon is up (the RetroBar behaviour) is in
  `Models/NotificationArea.cs:145`–`174` and `TaskbarWindow.xaml.cs:1772`–`1816`.

What is missing is not the interface — it is that none of it runs:

```csharp
// TaskbarWindow.xaml.cs:1583
private static readonly bool BalloonNotificationsEnabled = false;
```

with the early return at `:1617`–`1620` and the reason recorded at `:1564`–`1579`
and in `docs/FEATURE-STATUS.md` line 28: on real hardware 1.21.39/1.21.40 still
parked the balloon at the top of the screen, so v1.21.41 disabled the display and
left the interception on. Note what that combination costs: we consume the
`WM_COPYDATA`, so the application gets neither our balloon nor Windows', and —
because the feedback code sits behind the same gate — no `NIN_BALLOON*` at all.

On the asset question: **no new 9-slice asset work is needed for balloons.** The
9-slice pipeline in this repository is the *preview* frame, not the balloon:
`native/src/AeroThumbnailFrame.h:1`–`12` and `AeroThumbnailFrame.cpp:28`–`50`
(eight slices, `kSliceFiles`), fed by the PNGs in `Resources/` and validated by
`tools/verify-preview-frame.py`. The balloon is vector + theme styles
(`NotifyBalloon` style, resolved with `DynamicResource`), so it scales without a
slice set. Two clarifications on the tooling, since both were assumed in the
request: the Python scripts that *check* assets (`tools/verify-preview-frame.py`,
`tools/verify-graphical-resources.py`, `native/tools/check-preview-geometry.py`)
use only the standard library (`zlib`, `struct`, `base64`) — **no Pillow**; Pillow
appears in exactly one place, the optional converter
`compilation files/icons_to_base64.py:49`. And the artwork sources in
`assets/icon-sources/` are gitignored by design (`.gitignore:2`–`5`), so that
directory legitimately does not exist in a fresh clone.

If a themed Aero *balloon* frame is wanted later, the coherent shape is the one
already used for the preview frame — a slice set plus a theme style — not a new
mechanism; but that is a design choice, not a parity requirement, and nothing in
ManagedShell pushes towards it (ManagedShell ships no balloon visuals at all).

---

## 5. Queueing and concurrency

This is the weakest point of the current implementation, and it is native-side.

The chain today:

1. `ApplyMessage` writes the single slot `m_lastBalloon` and sets `m_hasBalloon`
   (`:2570`–`2577`), then queues `W7T_EVT_TRAY_BALLOON` (`:2578`).
2. `CoreState::QueueEvent` is a real FIFO with a 4096 cap and front-drop
   backpressure (`Common.cpp:66`–`77`, `Common.h:91`).
3. The managed pump runs every 100 ms and drains up to 64 events
   (`NativeBridge.cs:76`–`78`, `:177`).
4. `TaskbarViewModel.OnCoreEvent` turns each balloon event into `RaiseBalloon()`,
   which calls `TryGetLastBalloon` (`:191`–`196`, `:216`–`222`).

Two defects fall out of that shape:

* **Content loss on bursts.** The event queue is FIFO but the payload is one slot.
  Two balloons inside one 100 ms pump window produce two events and one payload —
  the second `NIF_INFO` overwrote the first at `:2570` before anything read it. The
  managed side then shows the *newer* text twice.
* **A stale balloon that never expires.** `GetLastBalloon` (`:3764`–`3774`) copies
  `m_lastBalloon` and leaves `m_hasBalloon = true`; the flag is written in exactly
  one place in the whole tree (`:2577`, verified by grep) and never cleared. So the
  slot stays readable forever, and any future caller — or a duplicated event —
  resurrects an old notification.

ManagedShell has neither problem because the payload travels *with* the event:
`NotificationBalloon` is an object, `TriggerNotificationBalloon` raises it, and an
unhandled one is appended to that icon's `MissedNotifications` collection, which
the consumer drains when the icon next becomes visible.

**Is there a reusable mechanism already here?** Yes, and it is the right one — but
not in the flyouts. `FlyoutLauncher` has no queue at all (the only match in
`FlyoutLauncher.cpp` / `TrayOverflowWindow.cpp` is an unrelated
`QueueEvent(W7T_EVT_OVERFLOW_HIDDEN, ...)`, `TrayOverflowWindow.cpp:272`); its
design is a single point of decision, not a queue. The mechanism to reuse is
`CoreState` itself (`Common.h:66`–`92`, `Common.cpp:61`–`93`): a mutex-protected
`std::deque` with a bounded size and drop-oldest backpressure — exactly the
semantics wanted for notifications, and already proven in this codebase. The
balloon path should get the same treatment instead of a hand-rolled slot.
On the managed side there is no balloon queue either: the only `Queue<` in `src/`
is `DiagnosticLogger`'s log queue (`Utilities/DiagnosticLogger.cs:20`), and the
single mention of `MissedNotifications` is a comment (`TaskbarWindow.xaml.cs:1601`).

Concrete changes: section 6.

---

## 6. Proposed changes to `TrayService.cpp` and the ABI

All of it is additive except the deprecation note; nothing changes the meaning of
an existing field.

### 6.1 `native/src/TrayService.h` — carry `hBalloonIcon`, replace the slot with a queue

```cpp
// NormalizedNid (:323-344)
     uint32_t uTimeout        = 0;
+    uint64_t hBalloonIcon    = 0;   /* NIIF_USER: sender's HICON, already
+                                     * duplicated into us by WM_COPYDATA */
     GUID     guidItem        = {};

// methods (:232)
-    bool    GetLastBalloon(W7T_BalloonInfo* out);
+    bool    GetLastBalloon(W7T_BalloonInfo* out);          /* deprecated: peek */
+    bool    PopBalloon(W7T_BalloonInfo* out);              /* FIFO, removes */

// members (:524-525)
-    W7T_BalloonInfo    m_lastBalloon = {};
-    bool               m_hasBalloon  = false;
+    /* Balloons waiting for the managed pump. Same shape as CoreState's event
+     * queue (Common.h:66-92): bounded, drop-oldest, one mutex. A single slot
+     * lost the older of two notifications arriving inside one 100 ms pump. */
+    std::deque<W7T_BalloonInfo> m_balloonQueue;
+    static constexpr size_t     kMaxBalloonQueue = 16;
```

`<deque>` is already available: `TrayService.h:48` includes `Common.h`, which
includes `<deque>` (`Common.h:35`). `ArgbBitmap` (`TrayService.h:145`, `:341`) is
the existing precedent for carrying pixels across the boundary, so the balloon
icon needs no new type.

### 6.2 `native/src/TrayService.cpp` — read the field

Add to `NormalizeNid`, in **both** branches, next to the existing `hIcon` lines
(`:2373` for 64-bit, `:2396` for 32-bit, using the existing `HandleFrom32` helper
at `:301`):

```cpp
 out.hIcon            = nid.hIcon;                                   // :2373
+out.hBalloonIcon     = nid.hBalloonIcon;                            // NidLayout64:265
```
```cpp
 out.hIcon            = HandleFrom32(nid.hIcon);                     // :2396
+out.hBalloonIcon     = HandleFrom32(nid.hBalloonIcon);              // NidLayout32:286
```

`NormalizeNidWine` (`:2302`–`2352`) leaves it at 0 — Wine's `WineTrayIconData`
(`:2279`–`2296`) has no such field, and the managed `NIIF_USER` fallback to the
tray icon already covers that case.

### 6.3 `native/src/TrayService.cpp` — the `NIF_INFO` block (`:2554`–`2580`)

Rewrite the block to (a) take the documented removal path, (b) keep the icon,
(c) enqueue instead of overwriting:

```cpp
 if (nid.uFlags & NIF_INFO) {
-    if (!nid.szInfo.empty()) {
-        /* ...existing visibility check and slot write, :2562-2579... */
+    if (nid.szInfo.empty()) {
+        /* NOTIFYICONDATAW: "To remove the balloon notification from the UI,
+         * either delete the icon (with NIM_DELETE) or set the NIF_INFO flag
+         * in uFlags and set szInfo to an empty string." Drop anything queued
+         * for this icon and tell the managed layer to close the visible one.
+         * Until now this shape was ignored outright (the old condition
+         * required a non-empty szInfo), so a withdrawn notification stayed up. */
+        DropQueuedBalloonsLocked(key);
+        W7T_BalloonInfo removal = {};
+        removal.ownerHwnd = nid.hWnd;
+        removal.uid       = nid.uID;
+        EnqueueBalloonLocked(removal);
+    } else {
+        bool barVisibleBalloon = false, presentBalloon = false;
+        ResolveVisibilityLocked(entry, barVisibleBalloon, presentBalloon);
+        if (!presentBalloon) {
+            LogTagged(L"TRAY",
+                      L"balloon suppressed by saved behavior (uid %u)", key.uid);
+        } else {
+            W7T_BalloonInfo info = {};
+            info.ownerHwnd = nid.hWnd;
+            info.uid       = nid.uID;
+            info.infoFlags = nid.dwInfoFlags;
+            info.timeout   = nid.uTimeout;
+            /* reserved (:162 in Win7TaskbarCore.h) becomes hasUserIcon. */
+            info.reserved  = CaptureBalloonIcon(nid) ? 1u : 0u;
+            CopyToFixed(info.title, 64,  nid.szInfoTitle);
+            CopyToFixed(info.text,  256, nid.szInfo);
+            EnqueueBalloonLocked(info);
+        }
     }
+    CoreState::Instance().QueueEvent(W7T_EVT_TRAY_BALLOON, key.ownerHwnd, key.uid);
 }
```

New private members (declaration in `TrayService.h`, definition beside
`GetLastBalloon` at `:3764`). Every one runs under `m_mutex`, which
`ApplyMessage` already holds (`:2424`, `std::recursive_mutex`):

```cpp
/* Copies the sender's balloon icon into our own bitmap, mirroring the NIF_ICON
 * path at :2518-2545 (CopyIcon -> IconToArgb -> BitmapSane -> DestroyIcon).
 * WM_COPYDATA duplicates the handle into this process, so it is valid here and
 * only until the sender frees it: copy now, use later. */
bool TrayService::CaptureBalloonIcon(const NormalizedNid& nid);

void TrayService::EnqueueBalloonLocked(const W7T_BalloonInfo& info) {
    if (m_balloonQueue.size() >= kMaxBalloonQueue) {
        m_balloonQueue.pop_front();          /* drop-oldest, as CoreState does */
    }
    m_balloonQueue.push_back(info);
}

void TrayService::DropQueuedBalloonsLocked(const TrayIconKey& key) {
    for (auto it = m_balloonQueue.begin(); it != m_balloonQueue.end(); ) {
        it = (it->ownerHwnd == key.ownerHwnd && it->uid == key.uid)
                 ? m_balloonQueue.erase(it) : std::next(it);
    }
}

bool TrayService::PopBalloon(W7T_BalloonInfo* out) {
    if (out == nullptr) return false;
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_balloonQueue.empty()) return false;
    memcpy(out, &m_balloonQueue.front(), sizeof(W7T_BalloonInfo));
    m_balloonQueue.pop_front();
    return true;
}
```

Keep `GetLastBalloon` as a non-destructive peek over `m_balloonQueue.front()` for
one release so an older managed layer does not break, then remove it (and its
export) once the frontend is updated.

### 6.4 `native/include/Win7TaskbarCore.h`

```c
 typedef struct W7T_BalloonInfo {
     uint64_t ownerHwnd;
     uint32_t uid;
     uint32_t infoFlags;
     uint32_t timeout;
-    uint32_t reserved;
+    uint32_t hasUserIcon;   /* 1 = balloonIcon below is valid (NIIF_USER) */
     wchar_t  title[64];
     wchar_t  text[256];
+    /* The sender's hBalloonIcon as BGRA pixels, 0x0 when absent. The handle
+     * itself cannot be handed to the managed layer: it dies with the sender. */
+    int32_t  balloonIconWidth;
+    int32_t  balloonIconHeight;
+    uint32_t balloonIconSize;    /* bytes, == width*height*4 */
+    uint64_t balloonIconData;    /* caller-owned buffer, may be NULL */
 } W7T_BalloonInfo;
+
+W7T_API int32_t W7T_CALL W7T_PopBalloon(W7T_BalloonInfo* out,
+                                        uint8_t* iconBuffer, uint32_t iconBufferSize);
```

Note the ABI consequence: this **grows** `W7T_BalloonInfo`, so
`Interop/NativeMethods.cs:157`–`171` must be extended in the same commit
(`Pack = 8`, `CharSet.Unicode`, `ByValTStr` 64/256 stay as they are). The
alternative — keeping the struct size and adding a separate accessor — is the
safer choice if a staged rollout is preferred, and the precedent to copy already
exists:

```c
/* Win7TaskbarCore.h:250-252 */
W7T_API int32_t W7T_CALL W7T_GetTrayIconBitmap(uint64_t ownerHwnd, uint32_t uid,
                                               int32_t* width, int32_t* height,
                                               uint8_t* pixels, int32_t pixelsBytes);
```

called twice from `NativeBridge.GetTrayIcon` (`Interop/NativeBridge.cs:327`–`342`):
first with `pixels == NULL` to learn the byte count, then with a buffer of that
size, then `CreateBitmap(pixels, width, height)`. A
`W7T_GetBalloonIconBitmap(uint64_t ownerHwnd, uint32_t uid, ...)` with the same
signature and the same two-call pattern would keep `W7T_BalloonInfo` byte-identical
and leave the existing `W7T_GetLastBalloon` marshalling untouched.

### 6.5 Managed follow-through

* `Interop/NativeBridge.cs:30`–`53` — add `HasUserIcon` and a `BitmapSource?
  UserIcon` to `BalloonNotification`; `:351`–`369` becomes `TryPopBalloon` over
  `W7T_PopBalloon`, and the icon arrives through a `GetBalloonIcon` method shaped
  exactly like `GetTrayIcon` (`:327`–`342`), reusing its `CreateBitmap`.
* `Models/TaskbarViewModel.cs:216`–`222` — `RaiseBalloon` becomes a drain loop
  (`while (_bridge.TryPopBalloon(out var b)) BalloonReceived?.Invoke(this, b);`),
  so a burst delivers every notification instead of the newest one twice.
* `TaskbarWindow.xaml.cs:1612`–`1669` — `OnBalloonReceived` must handle the
  removal record (empty `Title` *and* empty `Text`) by calling `_balloonHost.Hide()`
  and `UnpromoteBalloonIcon()` before anything else; today that shape cannot reach
  it. A short pending list here (the `MissedNotifications` idea) is only worth
  adding once the display is back on.
* `Controls/NotifyBalloon.xaml.cs:216`–`227` — `NIIF_USER` should prefer the
  bitmap coming from the core and keep the tray icon as the second fallback,
  which is ManagedShell's order.

### 6.6 Deliberately *not* proposed

* Porting ManagedShell's `trayIcon.Version = nicData.uVersion` on ADD/MODIFY
  (section 3, item 3): it corrupts the version when `NIF_INFO` is present.
* Dropping the 4–30 s clamp in `NotifyBalloon.xaml.cs:191`–`198`. ManagedShell has
  no clamp, but the clamp is documented here as protection against an application
  pinning a balloon on screen. Keep it; at most raise the ceiling.
* `NIF_REALTIME` / `NIIF_RESPECT` (quiet hours): neither project implements them.
  If wanted, the honest place is a policy check next to
  `ResolveVisibilityLocked` in `ApplyMessage`, not in the renderer.

---

## 7. Edge cases

| Case | Current Win7Taskbar behaviour (verified) | ManagedShell | Suggested |
| --- | --- | --- | --- |
| Balloon removal, `NIF_INFO` + empty `szInfo` | Ignored natively: `:2562` requires a non-empty `szInfo`, so no event, no hide, no `NIN_BALLOONHIDE`. A visible balloon would stay up (nothing can reach `BalloonHost.Hide` from that path). Managed hides both strings blank at `BalloonHost.cs:115`–`119`, but it is unreachable for this shape. | `handleBalloonData` returns early only on an empty **title**; empty `szInfo` with a title still creates a balloon. Neither matches the documented removal contract. | §6.3: emit an explicit removal record. This makes Win7Taskbar more correct than the reference. |
| `NIF_REALTIME` | Absent (0 hits). Treated as a normal balloon. | Absent. | Leave as is; revisit only if a real application is observed relying on it. |
| `NIIF_RESPECT` (quiet hours) | Absent (0 hits); the flag would be masked off by `NIIF_ICON_MASK = 0xF` (`NotifyBalloon.xaml.cs:41`) and ignored. | Absent. | If implemented, gate in `ApplyMessage` so the application still gets `NIN_BALLOONHIDE`. |
| `NIIF_USER` without `hBalloonIcon` | Falls back to the tray icon (`TaskbarWindow.xaml.cs:1723`–`1727`). | Falls back to `hIcon`, then to a default icon. | Equivalent; add the `hBalloonIcon` first choice (§6.2). |
| Empty `szInfoTitle` | Icon suppressed, text still shown (`NotifyBalloon.xaml.cs:208`–`212`), per the documented rule. | Whole balloon dropped (`handleBalloonData`). | Keep ours: the documentation only says the *icon* is not shown. |
| Toasts that bypass `NIF_INFO` | Out of scope by construction. Win10/11 toasts never traverse `Shell_NotifyIcon`; and on those systems the icons this project shows come from `Win11TrayReader` / `ExplorerTrayReader`, which produce no balloon data at all (0 grep hits for `NIF_INFO`/`szInfo`/`balloon` in both). Nothing in either project surfaces a WinRT toast. | Same: the library only decodes `SHELLTRAYDATA`. | Document as unsupported. Surfacing real toasts would need the WinRT notification APIs, which is a separate feature, not a parity item. |
| Two notifications within one pump window | Second overwrites the first (§5). | Both survive (`MissedNotifications`). | §6.1/§6.3/§6.5. |
| Application never told anything while the display is disabled | True today: the kill-switch at `TaskbarWindow.xaml.cs:1583` returns before `BuildBalloonFeedback` is ever built, and the native side already consumed the message. | Not applicable (ManagedShell always raises). | While the switch is off, consider answering `NIN_BALLOONHIDE` from `OnBalloonReceived` instead of a bare `return`, so applications do not wait on a balloon that will never appear. |

---

## 8. What cannot be verified from the files

Stated plainly, because each of these would otherwise read as a finding:

1. **ManagedShell and RetroBar are not in this repository.** Everything attributed
   to them here comes from `cairoshell/ManagedShell` @ `8552220`, fetched while
   writing this review. RetroBar's own consumer code (`NotifyIconList`,
   `NotificationBalloon` XAML, `SoundHelper`) was **not** read; claims about how
   RetroBar *displays* a balloon are not made.
2. **The proposed C++ was not compiled.** This sandbox has `g++` and `python3`
   only — no `cmake`, no `x86_64-w64-mingw32-g++`, no `dotnet`, no MSVC. The
   project builds on `windows-latest` with CMake + the Windows SDK
   (`.github/workflows/build-validation.yml`), so §6 is a reviewed patch, not a
   built one.
3. **`dist/Win7TaskbarCore.dll` cannot be used to validate §6** and already fails
   the project's own gate at HEAD (12 missing exports, 1 orphan — output in §0).
   Any ABI change must land together with a rebuilt DLL.
4. **Whether our `Shell_TrayWnd` actually wins the `FindWindow` lookup** against
   Explorer's is a runtime property of Windows, not a property of these sources.
   `docs/FEATURE-STATUS.md:28` already records that the 1.21.39/1.21.40 balloon
   placement defect reproduced **only on real hardware**, so none of the
   placement or Z-order questions in §2/§4 can be settled here.
5. **The `uTimeout`/`uVersion` union** is taken from `NOTIFYICONDATAW`
   documentation and from the comments at `TrayService.cpp:2378`–`2380`; it was
   not observed on a running system.
6. **ManagedShell's `NIIF` enum values were not read.** The claim in §7 about
   `HasFlag` ordering in `NotificationBalloon` is therefore not made; only the
   code as written is quoted.
7. **Line numbers for ManagedShell** are from the fetched files at that commit and
   will drift upstream; the Win7Taskbar ones are exact for `4c82492`.
