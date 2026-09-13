# Win7Taskbar on Windows 11

Windows 11 (24H2 included) no longer exposes the notification area the way
Windows 7, 8 and 10 did, so a few things cannot be done "the same way" and are
done in the way that actually works. This page lists them, with the reason, so
nobody has to rediscover them from the source.

## There is no tray toolbar any more

On Windows 10 the tray is a real `ToolbarWindow32` child of `Shell_TrayWnd`:
the core reads its buttons (position, tooltip, owner window, callback message,
`NIM_*` state) and talks to the owning applications directly.

Windows 11 replaced that toolbar with XAML islands drawn by `explorer.exe`.
There is no toolbar, no buttons, no owner window: nothing to enumerate and
nobody to send the tray callback message to. The core therefore reads the tray
through **UI Automation** (the same tree a screen reader sees) and sends clicks
with the accessibility patterns:

| action            | how it is done                                                        |
| ----------------- | --------------------------------------------------------------------- |
| read an icon      | walk the element whose automation class contains `SystemTray`          |
| left click        | `IUIAutomationInvokePattern::Invoke`                                   |
| right click       | `SetFocus()` + a synthetic `VK_APPS` (the legacy "show context menu" pattern does not exist) |
| icon identity     | automation id / name / executable, hashed into a stable uid            |

The reader runs on its own MTA worker (UI Automation needs a message pump and
must never block the taskbar's UI thread). The model is refreshed:

- right away when the Windows 11 path starts (first read is requested as soon
  as the worker exists, so the tray fills at startup, not on the first shell
  event);
- when the shell reports that an icon changed state (UI Automation
  property-changed events on the tray island: battery percentage, network
  coming and going, a tooltip being rewritten) - event driven, not polled;
- when a tray window appears or disappears, and once every 30 seconds as a
  safety pass;
- with a 400/800/1600/3200 ms ramp and then a 1 s -> 15 s backoff when the
  shell does not answer at all (island not ready yet). A read that fails is
  never interpreted as "the user has no icons": the model is left alone until
  a read succeeds.

The icons themselves are taken from the owning application, not invented: the
process is asked for the icon its windows carry (`WM_GETICON`, class icon -
many tray applications put their notification icon on a hidden window, which is
exactly the image Windows 10 shows), and only when the process exposes nothing
does the core fall back to the image of the executable and finally to a generic
application icon. The recreated volume/network/battery icons keep the Windows 7
artwork on purpose: on Windows 11 the shell does not draw them at all.

Icon identity does not include the tooltip. A tooltip is a moving target
("Battery 87%" becomes 86% a minute later, a mail client puts the message count
in it), and an identity built on it made the model create a new icon and drop
the old one on every read: the icon flickered, its position and its pin/hide
preference were lost and duplicates piled up. The key is the process (plus the
kind for the shell's own icons), so it lasts as long as the icon does; the name
still arrives in the model and updates the tooltip, which is where it belongs.

The tray/overflow split follows the user, not the shell: Windows 11 decides by
itself which icons live in the hidden-icons flyout, but moving an icon in
Win7Taskbar is a stored preference and wins over the shell's arrangement
instead of being overwritten at every read.

That preference is written **only when the user moves something**. The previous
round also wrote it while the model was being synchronised with the core - every
difference looked like a user choice - and since a stored preference wins over
the shell, those self-written values froze the tray: icons the shell kept in its
own flyout never came back to ours and the chevron was left with nothing to
show. Implicit writes are gone (the managed refresh marks the state it applies
and the core ignores it), and the stored values of that round are not read any
more: they live under `HKCU\SOFTWARE\Win7Taskbar\TrayIconPrefs`, while the
current preference key is `TrayIconPrefs2`. Nothing is deleted.

## Volume, network and battery are recreated by us

Since Windows 11 22H2 the three system icons are **not three separate buttons**
any more: the shell draws a single "quick settings" button and the three states
live inside its flyout. A machine that reports the battery as its own icon
gives us the battery and nothing else, which is exactly what users see when the
reader is left alone.

Win7Taskbar recreates them (`TrayFallbackIcons`, the same artwork used as
fallback on Windows 10) and follows their state: volume level/mute, network
connectivity, battery level and AC/DC.

Two rules make sure the user sees each of them **once**, and that it behaves
like Windows 7:

- An entry the shell exposes for one of those three types is **not imported**.
  The reader still reads it (it is what tells us the type exists), but the
  model takes only the icons of applications. Without this rule a machine whose
  shell draws its own battery shows two battery icons side by side: ours and
  Windows'.
- A click on one of ours opens the flyout **the settings ask for**, anchored to
  the rectangle the icon has at that moment. With *Windows 7* selected these are
  the Windows 7 ones: the classic volume flyout (`SndVol -f`), the Win32
  Windows 7 battery flyout requested from the shell, the recreated Windows 7
  network flyout, the Aero clock. With *Windows 10/11* selected the shell's
  immersive flyout of that type is requested (`ImmersiveFlyouts.cpp`), and the
  recreated one is the fallback when the shell cannot be used at all.

If a piece is missing - the recreated network flyout could not be initialised,
`SndVol` refused to start - the click falls back to the shell's flyout of that
type, and says so in `log-core.txt`. A silent no-op is never the answer.

The recreated network flyout is initialised at startup, not at the first click:
on Windows 11 the network icon is ours, so the branch that used to prepare the
module (a click on an icon imported from Explorer's tray) never ran, and the
module stayed dormant.

The type of these icons travels to the managed layer in the `guidKey` field
(`uia:volume`, `uia:network`, `uia:battery`).

- Consequence to be aware of: **on Windows 11 the tray shows the icons the
  applications register plus our three, not necessarily the original three of
  Explorer.** This is the documented limit of the Windows 11 path.

## The overflow chevron opens our own panel

On Windows 10 the chevron opens the native "hidden icons" window, which the
overflow panel mirrors icon by icon. On Windows 11 that flyout is a XAML island
that does not answer the accessibility invoke on 24H2: the click did nothing.

The chevron now always opens the panel built into Win7Taskbar. It is filled
from the tray model, so on Windows 11 it contains the icons read through UI
Automation (including the ones the shell keeps hidden) plus the recreated
system icons. Same limit as above: what the shell does not expose cannot be
shown.

Two details decide whether the panel is reachable at all, and both were wrong
after the previous round:

- The chevron is part of the tray: it is shown whenever the tray collapses
  icons, even if nothing is hidden at that instant (the panel always contains
  its "Customize..." link). Tying its visibility to "there is something in the
  panel right now" meant that a single late tray read could take the chevron
  away, and the click that followed landed on nothing.
- An icon the shell keeps in its hidden-icons flyout is **not** `NIS_HIDDEN`.
  `NIS_HIDDEN` means "the application asked not to show this icon", and the
  model removes such an icon from *both* lists. Icons hidden by the shell are
  exactly the ones the panel has to show, so they are stored as "not on the
  bar" and nothing else.

The link at the bottom of the panel is localized through the native string
table (eleven languages, English fallback) like every other string the program
draws itself.

## Settings: read at startup, applied in one place

The flyout options used to reach the system only when the user pressed **OK** or
**Apply** in the Properties window. Starting the bar with settings already saved
therefore left the registry and the native core untouched: the click opened the
flyout that the *defaults* implied, not the one that was selected. That is the
"the program does not read the settings at startup" report, and it also explains
why the choice looked inverted - for the volume the recreated tray icon always
launched the classic mixer (SndVol), for the battery it always opened the
recreated flyout, whatever the two combo boxes said.

Reading the configuration now produces a single call, at startup and on every
apply:

- `ApplyShellFlyoutPreferences()` publishes the four decisions to the core with
  `W7T_SetFlyoutPreferences` (1 = Windows 7, 0 = Windows 10/11);
- the three `HKCU\...\ImmersiveShell` values that Windows itself reads are kept
  in sync: `UseWin32TrayClockExperience`, `UseWin32BatteryFlyout` and
  `EnableMtcUvc` (the same switches ExplorerPatcher writes);
- the resulting state is written to `log-core.txt` as one `SETTINGS:` line, so
  "what did the program actually load" is a fact and not a guess.

The four decisions are then consumed from one place only: `ChooseFlyoutRoute()`
in `FlyoutLauncher.cpp`. No launcher carries its own copy of the rule any more -
the synthetic tray clicks, the taskbar menu and the frontend all ask the same
function, so the "Windows 7" entry opens the Windows 7 flyout and the
"Windows 10/11" entry opens the shell's one, by construction.

### The choice that was overwritten at every start

The settings file also carries one-shot migrations (they exist to give a value
to installations written before an option existed) and a pair of them forces the
clock choice. They ran at **every** load while their own flag was still missing
from the file, and they assigned the value without looking at whether the file
already contained it. If a save ever failed - or if the file was copied around
without those flags - the user's choice was rewritten at every start: "the
program does not read the settings at startup", with the flyout choices looking
swapped because one side of the pair always won.

A migration now assigns a value **only when the file does not contain that
property**: the loader reads the JSON once, collects the property names that are
actually present, and passes them to the migration, which uses them to tell
"the user chose this" from "the field was never written". What the user chose
stays theirs.

The same suspicion (a choice that does not get saved) is now answerable from the
log instead of being a guess. `Settings` writes one line per event:

| line | meaning |
| --- | --- |
| `SETTINGS-LETTURA: file=... salvati={...}` | the file, and for each option whether it was **stored** in it |
| `SETTINGS-LETTURA: nessun file (...)` | first run: the defaults are in use |
| `SETTINGS-CAMBIO: UseBatteryFlyout=True` | a choice changed in the Properties window |
| `SETTINGS-SALVA: <path> (n byte)` | the file was written |
| `SETTINGS-ERRORE salvataggio: ...` | the write failed, with the exception and the path |

The write used to swallow `IOException`/`UnauthorizedAccessException` without a
trace and to let anything else escape; now every failure is logged and nothing
can take the interface down. The lines produced before the native core exists
(the language is loaded before it) are buffered and delivered as soon as the core
is there, so the load is not lost.

## Clock: Windows 7 means Windows 7

The clock combo offers the recreated classic theme and **Windows 7**. The second
one used to end in the recreated calendar on Windows 11, because the whole
native route was refused there with the reasoning of the previous round (the
shell's clock flyout is a XAML island that appears whenever it likes).

The refusal was too broad: it also blocked the **classic Aero clock**
(`CLSID_AeroClock`, window class `ClockFlyoutWindow`), which is the Windows 7
calendar and still exists on Windows 11. The route is now:

1. preference *Windows 7* -> request the Aero clock, wait (bounded) for the
   `ClockFlyoutWindow` window and reposition it like Windows 7 does. Only if
   that window really appears is the answer "opened": a success code is not
   enough, because on Windows 11 the shell answers success while opening its own
   XAML island. If it does not appear, the recreated calendar is shown - the
   declared fallback, not a silent failure.
2. preference *Classic theme (recreated)* -> the recreated calendar, no shell
   involvement.
3. On Windows 8/10 the previous behaviour is untouched: the shell accepting the
   request still counts as success even when the window is slower than the wait.

The "the immersive host is missing" flag is no longer permanent either: a
transient failure after logon retried after thirty seconds instead of disabling
the native route for the whole session.

## Battery: the Windows 7 flyout, ExplorerPatcher style

With the battery set to **Windows 7** the click no longer opens the recreated
flyout. It is forwarded to the shell's real battery button (the UIA element the
Windows 11 tray exposes), with `UseWin32BatteryFlyout=1` in place - the same
mechanism ExplorerPatcher uses - so Windows itself shows the Win32 Windows 7
battery flyout, anchored to the tray.

Because that route depends on the shell answering, it is verified: 900 ms after
the click the number of visible foreign popups is compared with the number
before it. If nothing appeared, the recreated flyout is shown at the icon's
rectangle. A click therefore always has an effect; what changes is which of the
two flyouts you get, and the log says which (`[GATE] battery: ...`).

With **Windows 10/11** selected the shell's immersive battery flyout is tried
first, and the recreated one is the fallback.

## Network: the connection is whatever Windows says

The recreated network flyout could say *Not connected* to a machine that was
connected: the header trusted only our two reads (a physical Ethernet adapter
from `GetAdaptersAddresses`, the first Wi-Fi network flagged connected by
`WlanGetAvailableNetworkList`), and either of them can come back empty - a
WLAN service that starts late, an adapter the filter does not recognise, a
driver that does not set the `CONNECTED` flag.

`INetworkListManager::GetConnectivity()` is now queried **on every refresh**,
never cached from startup, and it is the last word of the header: if it says
connected, the flyout shows the connected layout with the network name Windows
reports, even when neither of our own reads saw anything.

### The list of the networks to connect to

The flyout showed the connection state but not the networks to connect to.
`WlanGetAvailableNetworkList` answers with what the system has **cached**: if
nobody scanned recently (or the WLAN service has just started) the list is
empty, and the flyout opened with a correct header and nothing under it.

Opening the flyout now asks for a scan (`WlanScan`) on every WLAN interface. The
scan is asynchronous and the WLAN notification is already registered, so when it
finishes (`wlan_notification_acm_scan_complete`) the flyout refreshes itself: the
list appears a moment after opening, with nothing blocked.

And the flyout is no longer the only mute component of the program. `Wh_Log` -
the logging function of the ported mod - wrote to `OutputDebugString` only,
which is invisible in normal use; it now also appends to `log-core.txt` with the
`[W7TNetFlyout]` prefix, and the refresh says what it found:

| line | meaning |
| --- | --- |
| `rete: nessun handle WLAN` / `WlanEnumInterfaces non riuscito (errore N)` | the WLAN client is not available |
| `rete: N interfaccia/e WLAN presenti` | the interfaces the system reports |
| `rete: elenco reti non disponibile (errore N)` | the per-interface query failed |
| `rete: scansione richiesta su N interfaccia/e` | the scan asked for when the flyout opens |
| `rete: N interfaccia/e WLAN, M rete/i visibili` | **the number that matters**: how many networks the flyout had to show |

## Opening the Network and Sharing Center cannot take the bar down

The link at the bottom of the flyout launches `control.exe /name
Microsoft.NetworkAndSharingCenter`, which used to close the program. Every shell
launch from the flyout now goes through a guarded helper: a SEH block around the
`ShellExecuteW` call and a C++ `try`/`catch` around that block (in separate
functions - MSVC does not allow the two in one body), with the outcome written
to the log. A failure is a log line, not a crash.

The launch also happens **after** the flyout is hidden and from a worker thread:
the click used to run `ShellExecute` with the window still alive and the mouse
message still in progress, so the flyout could receive activation messages
(another process taking the foreground) while it was halfway through its own
handler. Now the click returns immediately, and no part of the process is
mid-way when `control.exe` arrives.

## Clock flyout placement

## Search window

The search window is DPI aware: the interface (panel size, row height, fonts,
hit boxes) scales with the monitor DPI, while the icons keep their **nominal**
size (16/32/48 logical pixels). `ShellItemIcon` picks the shell image list from
the nominal size, not from the scaled one: for a DPI aware process the shell
already returns the icon at the size the monitor needs, and asking for a scaled
size made it return the next list up (16 -> 48 px), which is what made the
icons look gigantic.
