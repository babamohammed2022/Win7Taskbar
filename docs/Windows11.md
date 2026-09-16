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
itself which icons live in the hidden-icons flyout, but pin/unpin in Win7Taskbar
is a stored preference and now wins over the shell's arrangement instead of
being overwritten at every read.

## Volume, network and battery are recreated by us

Since Windows 11 22H2 the three system icons are **not three separate buttons**
any more: the shell draws a single "quick settings" button and the three states
live inside its flyout. A machine that reports the battery as its own icon
gives us the battery and nothing else, which is exactly what users see when the
reader is left alone.

Win7Taskbar recreates the missing ones (`TrayFallbackIcons`, the same artwork
used as fallback on Windows 10) and follows their state: volume level/mute,
network connectivity, battery level and AC/DC.

Clicking one of them opens the matching **system** flyout (volume, network,
battery) through the shell's own experience managers, anchored to the current
rectangle of the clicked icon: the same mechanism the shell itself uses, not a
look-alike. A right click is forwarded to the shell's element of that kind when
it has one, so the context menu is the real one; when the shell exposes no such
element (the usual case on 22H2 and later, where a single quick-settings button
represents all three) the flyout is opened instead, because a context menu
without a source cannot be invented.

- The shell always wins: as soon as it exposes a type itself, our copy of that
  type disappears within two reads.
- Consequence to be aware of: **on Windows 11 the tray shows the icons the
  shell exposes plus our three, not necessarily the original three of
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

## Clock: one flyout only, and it is ours

The clock flyout of Windows 10/11 is an immersive experience, and on Windows 11
it is a XAML island that the shell materialises when it feels like it. Asking
for it and then waiting for it to show up was a lost cause: the call succeeds,
the window can appear well after any timeout we are willing to wait, we
conclude "it did not open", we show the Windows 7 calendar - and *then* the
system one appears on top. That is why the user saw the native flyout and ours.

On Windows 11 the shell flyout is therefore **never requested**: not by the
frontend (the native path is only taken when the core positively says the system
is not Windows 11, and an unreadable answer counts as Windows 11) and not by the
core (`ShowClockFlyout` returns "not available" on build 22000+ even if it is
called). The Windows 7 calendar is the only flyout, and to make sure nothing can
leave a second one on screen, opening it first asks the core to close the
shell's clock flyout if it happens to be open - a close-only call that opens
nothing and is a no-op when there is nothing to close.

On Windows 10 and earlier nothing changes: the "native flyout" option still
works and the shell flyout is the one shown.

The flyout is anchored to the clock of the moment. Its position is computed at
every open from the clock's real screen rectangle and the work area of the
monitor the clock is on: right edge aligned with the clock and just above the
taskbar (Windows 7 style), below the clock when there is no room above, and
always inside the work area. After opening, the real position is measured and
corrected if it is not the requested one, which covers per-monitor DPI and a
taskbar that moved. A flyout placed from import-time coordinates - or one left
to the default WPF placement - ended up in the wrong place as soon as the DPI or
the monitor changed.

## Search window

The search window is DPI aware: the interface (panel size, row height, fonts,
hit boxes) scales with the monitor DPI, while the icons keep their **nominal**
size (16/32/48 logical pixels). `ShellItemIcon` picks the shell image list from
the nominal size, not from the scaled one: for a DPI aware process the shell
already returns the icon at the size the monitor needs, and asking for a scaled
size made it return the next list up (16 -> 48 px), which is what made the
icons look gigantic.
