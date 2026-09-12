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
must never block the taskbar's UI thread) and the model is refreshed on tray
window events plus a 30 second safety pass.

## Volume, network and battery are recreated by us

Since Windows 11 22H2 the three system icons are **not three separate buttons**
any more: the shell draws a single "quick settings" button and the three states
live inside its flyout. A machine that reports the battery as its own icon
gives us the battery and nothing else, which is exactly what users see when the
reader is left alone.

Win7Taskbar recreates the missing ones (`TrayFallbackIcons`, the same artwork
used as fallback on Windows 10) and follows their state: volume level/mute,
network connectivity, battery level and AC/DC. Clicking one of them opens the
matching native flyout (volume, network, battery).

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

## Clock: one flyout only

The native clock/calendar flyout of Windows 10/11 is an immersive experience
that can take a while to appear on Windows 11. The probe that verifies "did it
really open?" now waits longer (1.4 s) when the shell accepted the request, and
before opening the Windows 7 calendar as a fallback it explicitly closes the
native one. The user sees a single flyout, never the native one followed by
ours.

## Search window

The search window is DPI aware: the interface (panel size, row height, fonts,
hit boxes) scales with the monitor DPI, while the icons keep their **nominal**
size (16/32/48 logical pixels). `ShellItemIcon` picks the shell image list from
the nominal size, not from the scaled one: for a DPI aware process the shell
already returns the icon at the size the monitor needs, and asking for a scaled
size made it return the next list up (16 -> 48 px), which is what made the
icons look gigantic.
