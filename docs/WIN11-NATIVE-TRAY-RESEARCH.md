# Windows 11 Native Tray Recon & Research

Recon for the mission: consume the native Windows 11 tray directly,
out-of-process, with no injection into `explorer.exe`, so ExplorerPatcher
becomes optional instead of recommended. Phase 0 feasibility gate and the
accumulated technical findings live here.

> **Rule of record (project):** this document distinguishes three things
> everywhere: (1) what was *verified by the probe on a specific build*,
> (2) what comes from *reference sources* (Windhawk mods, RetroBar,
> StackOverflow threads, community discoveries), and (3) what the mission
> *wants to verify*. Never present 2 or 3 as 1.

---

## 1. Constraints recap (binding)

* Out-of-process only. The product must not ship any code that injects,
  patches symbols, or detours functions inside `explorer.exe`.
  Reading explorer's address space (`VirtualAllocEx` + `ReadProcessMemory`
  on buffers allocated for `TB_*` messages) is out-of-process by the
  Windows definition and is the classic, documented practice for
  toolbar enumeration — it is allowed where the mission explicitly
  allows it (Phase 2.5 legacy control channel). Everything must degrade
  gracefully when that is not possible.
* The legacy Win32 layer, where it is alive on Windows 11, may only be
  used as a **data/control plane** (read state, anchor rects, button
  lists, callback metadata). It must never be made visible: no
  `ReBarWindow32`, `MSTaskSwWClass`, or `ToolbarWindow32` window may
  ever receive a style change or become visible.
* Windows 10 21H2/22H2 and Windows 8.1 behavior stays byte-identical;
  all of this is opt-in on Windows 11 only.
* No new services, drivers, watch processes, or registry keys beyond
  the README-documented ones.
* Balloon interception from out-of-process is architecturally impossible:
  balloons belong to `Shell_TrayWnd` (see section 6). Limit stands; do
  not attempt shell-takeover to defeat it.

## 2. Phase 0 probe (`tools/win11-native-tray-probe/`)

A standalone C++ translation unit (`win11_native_tray_probe.cpp`) with no
project files. It verifies on the machine it runs on:

1. The exact child-class list of every `Shell_TrayWnd` /
   `Shell_SecondaryTrayWnd` (legacy layer vs XAML overlay coexistence).
2. The classic chain `Shell_TrayWnd → TrayNotifyWnd → SysPager →
   ToolbarWindow32` ("User Promoted Notification Area").
3. Out-of-process enumeration: `TB_BUTTONCOUNT`, `TB_GETBUTTON` on a
   remote buffer in explorer's address space, then `(hwnd, uID,
   uCallbackMessage, hIcon-presence)` from the legacy per-button struct,
   with layout auto-detect (two public variants float around).
4. Whether `NotifyIconOverflowWindow` exists at all (even hidden).
5. `--watch <seconds>`: whether the registered `TaskbarCreated`
   broadcast arrives (restart explorer manually to test).

Exit codes: 0 = legacy chain + at least one readable app-button;
1 = chain found but buttons unreadable;
2 = no legacy chain (UIA path only); 3 = probe could not run.

Build (all toolchains, no deps beyond system libs):

```text
MSVC: cl /EHsc /O2 win11_native_tray_probe.cpp /Fe:win11_native_tray_probe.exe user32.lib advapi32.lib
MinGW: g++ -O2 -municode -mconsole win11_native_tray_probe.cpp -o win11_native_tray_probe.exe -luser32 -ladvapi32
```

### 2.1 Per-build matrix — TO BE FILLED ON HALLOWED HARDWARE

| Build | Detected chain classes on Shell_TrayWnd | TB_* readable | Overflow window | Notes |
|---|---|---|---|---|
| 23H2 (22631) | pending | pending | pending | run probe |
| 24H2 (26100) | pending | pending | pending | run probe |
| 25H2 (26200) | pending | pending | pending | run probe |
| Server 2025 | pending | pending | pending | run probe |
| IoT Enterprise LTSC 2024 | pending | pending | pending | run probe |

Evidence gathered from reference sources before hardware is available
(section 5) says the legacy chain still exists — *hidden and partially
populated* — on 24H2 and 25H2 (the legacy taskbar revival discovery and
the notification-icon spacing Windhawk mods still enumerate it). The
probe turns "sources say" into "verified on build X".

## 3. What the project already does today (status quo)

The current tree already ships two out-of-process readers:

* `native/src/ExplorerTrayReader.cpp` — the classic Win32 compatibility
  path used when the legacy notification toolbar exists:
  `TrayNotifyWnd → SysPager → ToolbarWindow32`, `TB_*` remote-buffer
  enumeration, system icons, and the legacy overflow window. The window
  messages are documented, but the cross-process button payload and the
  `dwData` object are not a Microsoft contract; the implementation labels
  that boundary and degrades when it cannot validate it.
* `native/src/Win11TrayReader.cpp` (+ `Win11TrayReaderResilience.cpp`) —
  a best-effort UI Automation reader of the Win11 XAML tray: it searches
  the taskbar and its public XAML bridge windows, recognizes the observed
  `SystemTray.NotifyIconView`/`SystemTray.IconView` elements even when their
  UIA control type is `Custom`, reads tooltip text, invokes click patterns,
  subscribes to property and structure changes, and re-resolves after
  Explorer rebuilds. UIA is not a public notification-icon enumerator: it
  may omit an element, expose no owner HWND/HICON, or have no overflow
  window while the flyout is closed.
* `native/src/TrayService.cpp` — the take over existing icons,
  fallback system icons (network/volume/battery) recreated out of thin
  air, balloon queue, `TaskbarCreated` re-registration, retrobar
  coexistence warning.
* `native/src/TaskbarWindow.cpp` — the native bar itself; it calls
  `SetNativeTaskbarHidden(true)` through the appbar service
  (`ABM_SETSTATE` + the native bars moved off), with `TaskbarCreated`
  re-hide.

On Windows 11 the XAML path remains **best-effort, not complete support**.
The public UIA provider can expose visible `NotifyIconView` elements and an
overflow island when that island exists, but Microsoft does not document an
API that enumerates every third-party notification icon, its registration
identity, bitmap, or promotion state from another process. The real
`Shell_TrayWnd` shim therefore remains useful for new registrations and
updates; its `WM_COPYDATA` payload is a compatibility protocol, not a
supported Microsoft ABI. A missing UIA element is not removed from the
model after one read: removal uses hysteresis and keeps hidden entries when
the overflow island was not successfully traversed. This is still not a
promise that an omitted element can always be distinguished from a deletion,
and the product does not claim parity with Explorer's XAML tray.

## 4. Gap analysis vs the mission

| Mission gate | State today | What changes |
|---|---|---|
| Phase 0 probe + doc | **this document + probe (new)** | hardware matrix to fill |
| Source abstraction (`ITraySource`, Auto / EP mirror / Native Win11) | two readers exist, chosen statically per OS | one negotiator with hysteresis + settings flag (`ExtraSettings` / About) |
| Native Win11 third-party icons without EP | UIA snapshot plus the owned `Shell_TrayWnd` registration shim; **not complete** | use only the validated legacy fallback and registration traffic; never present private XAML hooks as support |
| Overflow parity without `NotifyIconOverflowWindow` | our own overflow exists; the XAML island may be absent while closed | read the public overflow island when present, preserve the last hidden snapshot otherwise, and document the gap |

| Native bar hiding without EP | `ABM_SETSTATE` + `ShowWindow(SW_HIDE)` + `TaskbarCreated` re-hide | restored-on-crash marker discipline (battery pattern analogy already in project precedent), per-monitor pass |
| Balloon parity | limitation stands | documented only, no workaround shipped |
| ExplorerPatcher wording | recommended | demote to "optional, only if you prefer the Win10 tray" once acceptance criteria pass |

## 5. Source review (what each reference taught us)

* **Microsoft docs** — TB_* messages, `TBBUTTON`, `OpenProcess`,
  `VirtualAllocEx`, `ReadProcessMemory`, `RegisterWindowMessage`,
  `shell_NotifyIcon*`, `ITaskbarList*`, `ABM_SETSTATE`. The TB_GETBUTTON
  remote-buffer dance is the classic technique; the doc stays the
  authority on ownership and on memory allocation requirements.
* **ManagedShell/RetroBar** (Apache-2.0)
  (`RetroBar ` / `ManagedShell.WindowsTray`): the reference
  implementation of consuming the legacy `TrayNotifyWnd` from outside
  explorer — `TrayService` (which we already mirror, following their
  `ExplorerTrayReader.cpp` shape), `NotifyIconList`, icon promotion for
  overflow balloons (`NotifyIcon.TrayIcon_NotificationBalloonShown`),
  `SPI_GETMESSAGEDURATION` balloon timing. Its important architectural
  lesson is that RetroBar replaces the taskbar and receives registrations in
  its own notification-area service; it does **not** provide a public
  enumerator for an already-built Explorer XAML tray. ManagedShell is a
  reference for lifecycle, identity reconciliation, and Explorer restart
  handling, not a Microsoft API.
* **Windhawk mods (GPL, referenced as research only):**
  * `taskbar-notification-icon-spacing` — identifies the modern XAML classes
    (`SystemTray.NotifyIconView`, `SystemTray.IconView`, and their named
    children). Its implementation runs inside Explorer and uses WinRT/XAML
    objects; only the class-name observations informed the UIA reader.
  * `taskbar-notification-icons-show-all` — changes
    `NotifyIconSettings\\<id>\\IsPromoted` through registry API hooks inside
    Explorer. It confirms that `IsPromoted` is a preference, not an icon
    enumerator; no hook or registry write was copied.
  * `taskbar-tray-system-icon-tweaks` — works with private `IconView` and
    taskbar-host objects inside Explorer. It is evidence about the visual
    tree, not a supported out-of-process channel.
  * `taskbar-multi-tray` and `taskbar-tray-system-icon-tweaks` — show that
    multiple XAML stacks/hosts and promoted state vary by build; their
    injection/vtable/offset techniques are explicitly excluded.
  * `windows-11-taskbar-styler` and related XAML mods — useful for names and
    lifecycle, but their VisualTree hooks are private and are not shipped.
  * Windhawk mods that patch `WM_COPYDATA` or Explorer's notification classes
    (for example SplitTray) depend on private symbols/ABIs. They motivated
    the real owned spy window, but no hook, injection, or ABI from them is
    distributed here.
  * `win10-taskbar-on-win11-24h2` + fix-mods (m417z) — evidence that on
    24H2 the whole legacy taskbar code path is still shipped and can be
    revived; supports the "legacy layers stay loadable" line of the
    mission. Clean-room rule: techniques only, no copied lines, credit
    retained in this doc and in THIRD-PARTY-NOTICES.
* **StackOverflow remote-TBBUTTON threads (classic, public)** — the
  canonical pattern for `TB_GETBUTTON` cross-process reads; explains the
  two floating per-button struct layouts the probe auto-detects.
* **m417z `win10-classic-tray-on-win11-*` discoveries** — evidence on how
  the Win11 XAML taskbar bars the classic deskbands from appearing by
  refusing `TrayBandSiteService`, NOT by removing the windows: exactly
  what the mission needs for the *data-plane only* use.
* **KRR1751, "Windows 11' SECRET Taskbar!" (2024)** — community video
  documenting the revival waypoints (registry key + subclass cleanup)
  and calling for persistence of the XAML overlay windows when the
  legacy deskbands are let out. **License status: discovery mod source
  attached to that work is unlicensed — treated as documentation/idea
  source ONLY; zero of its lines may enter our tree.** Credit it here
  and in THIRD-PARTY-NOTICES as an unlicensed discovery reference.
* **blendonl/mnotify** — independent confirmation that balloons are
  owned by `Shell_TrayWnd` and that `TaskbarCreated` re-registration is
  the shell-restart discipline. This is why the balloon limitation
  (section 1) is architectural for anything outside explorer.

## 6. Percorso legacy effettivamente usato dal servizio

Il progetto crea sul proprio thread UI una finestra reale di classe
`Shell_TrayWnd`, con figlia `TrayNotifyWnd`, prima di avviare l'importazione
della toolbar. Per ogni `WM_COPYDATA` con `dwData == 1` il servizio tenta prima
l'elaborazione locale e poi usa `SendMessageTimeout(WM_COPYDATA)` sulla vera
`Shell_TrayWnd` di `explorer.exe`, escludendo il proprio PID e preferendo la
finestra con figlia `TrayNotifyWnd` (con fallback alla finestra dello stesso
processo sulle build XAML che non espongono quella figlia). Un payload non
valido non viene trasformato in un successo fittizio; l'inoltro viene comunque
tentato solo per il protocollo `dwData == 1`.

Il tracciato `SHELLTRAYDATA` e il layout della voce puntata da `TBBUTTON::dwData`
sono **API non documentate da Microsoft**. Sono stati ricostruiti da
reverse engineering e confrontati con reimplementazioni open source
(ManagedShell/RetroBar e ReactOS); possono cambiare tra build e sono sempre
protetti da try/catch e guardie SEH/RAII.

### 6.1 Stato visibile/overflow di Windows 11

Quando legge la toolbar di Explorer, `ExplorerTrayReader` enumera anche
`HKCU\Control Panel\NotifyIconSettings`. Il nome della sottochiave osservato
è un identificatore decimale opaco: il codice ne verifica soltanto la forma,
non tenta di ricalcolare un hash. L'associazione è adottata solo quando
`UID` (`REG_DWORD`), `ExecutablePath` e il proprietario della finestra
corrispondono e `IsPromoted` è un `REG_DWORD` 0/1. `1` significa zona visibile,
`0` overflow; se la corrispondenza non è verificabile, il fallback è lo stato
reale `TBSTATE_HIDDEN`/toolbar. La chiave non viene mai scritta.

`ITrayNotify`/`ITrayNotifyImpl` non è un contratto COM documentato e il suo
layout non è nel Windows SDK; non viene chiamato dal prodotto. Su XAML il
lettore UIA registra inoltre eventi di proprietà e di struttura e riapre la
fotografia quando Explorer ricrea il bridge; una finestra di overflow non
presente non viene interpretata come rimozione. Il messaggio pubblico
`TaskbarCreated` viene ascoltato per il riavvio della shell. Il broadcast
artificiale all'avvio resta disabilitato nel percorso generico: senza una
riconciliazione verificabile fra UIA e registrazioni avrebbe prodotto voci
duplicate.

## 7. Architectural impossibility, stated once and for all

Balloon interception from out-of-process is impossible: Windows routes
`NIF_INFO` notifications into UI owned by `Shell_TrayWnd` inside
explorer.exe. An out-of-process app can only *observe* balloon toasts
through `ToastNotificationHistory` / DWD… no. There is no documented
observer for the legacy balloon queue. Therefore the project's balloons
stay the project's balloons (the recreated ones), exactly like RetroBar.
Any future "EP-less parity" will never include EP's balloon fidelity;
the README limitation remains.

## 8. Next-phase checklist (engineering, post-gate)

1. Fill the probe matrix on real/borrowed hardware for the five builds.
2. If the legacy channel is verified on at least 24H2/25H2: implement the
   delimited **legacy control channel** behind
   `NativeWin11TrayLegacyChannel` (settings uint32, default on for Win11
   22H2+; OFF path = today's behavior byte-identical).
   * Remote-buffer TB enumeration merged with the UIA strip by
     `(hwnd, uID)` (UIA stays the source of truth for geometry on
     screen; the legacy channel supplies icons/callbacks/rects).
   * `CopyIcon()` remote HICON into session-global icons; never store
     foreign points.
   * Cross-validate against the XAML UIA ground truth; per-build
     discrepancy log to `debug.log`.
   * Health-gated hysteresis (K consecutive failures → soft-disable,
     M successes → re-enable), instrumented channel-off diagnostic.
3. Source negotiation UI: Properties → Extra → Tray source
   (Auto / ExplorerPatcher mirror / Native Win11), README documented.
4. Harden the native-bar hiding: multi-monitor `Shell_SecondaryTrayWnd`
   sweep, restore marker-file discipline (precedent: battery key
   file), `TaskbarCreated` re-hide (already present).
5. Only after the acceptance criteria are exercised on hardware:
   demote the ExplorerPatcher recommendation in README (proposal PR).

## 9. Acceptance criteria echo (for the record)

Icons complete & correct only where the selected source actually exposes
that data (XAML completeness is not claimed); V4+legacy+system icon
behavior; overflow parity where a verified source offers one; native bar
hidden & self-healing incl. after crash; multi-monitor + DPI + theme;
fullscreen autocorrection; README demotion only post-verification;
registration reconciliation proven without duplicate model entries; no
Windhawk injection, private vtable, offset, or Explorer hook in the product.

---

*Last edited: 2026-09-25 — XAML UIA class filtering, public bridge/overflow
lifecycle, structure-change refresh, conservative overflow retention, and
legacy-protocol limits recorded here.*
