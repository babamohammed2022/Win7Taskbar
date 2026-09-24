# Win7Taskbar

<img width="1366" height="61" alt="image" src="https://github.com/user-attachments/assets/fcc86ac5-1ff2-418f-a7df-aca0039b1108" />

A Windows 7-inspired taskbar recreation and for Windows 10 and 11.

Win7Taskbar is a system utility that recreates the Windows 7-style taskbar and Superbar using a XAML frontend with a native C++/Win32 backend. It includes grouped task buttons, the notification area, system flyouts, overflow handling, Jump Lists, and a Windows 7-inspired Properties interface.

The software has been tested on Windows 8.1, Windows 10 21H2, Windows 10 22H2, Windows 11 23H2 Windows 11 24H2, Windows 11 25H2 and Windows Server 2025. Windows 8.1 has been tested successfully and the software works reasonably well on this version, although some platform-specific differences may affect individual features. Some functionality on Windows 11, particularly the notification area, is recreated because newer versions of Windows no longer expose all of the same taskbar functionality available on previous versions.

On Windows 11, **ExplorerPatcher is recommended for the best experience**, but it is optional. It can provide a more compatible Windows 10-style taskbar environment and allow Win7Taskbar to use more native notification-area functionality.

Win7Taskbar now ships its own Windows 7-style Start Menu (see below). **Open-Shell remains optional** if you prefer its menu instead: set **Windows key opens: Windows** in Properties → Extra.

This software has only been tested with ExplorerPatcher and OpenShell. Support for other third-party tools that serve a similar purpose will be analyzed individually where possible.

**Current state: `Alpha`**

## Screenshot (Windows 7 skin)

<img width="1366" height="61" alt="image" src="https://github.com/user-attachments/assets/fcc86ac5-1ff2-418f-a7df-aca0039b1108" />

## Screenshot (Windows 8.1 skin)

<img width="1366" height="51" alt="image" src="https://github.com/user-attachments/assets/470fc73f-0ab4-4601-8c92-94ff5f5d23c6" />

## Requirements

* Windows 8.1, Windows 10 or Windows 11 (64 bit)
* Official releases should be self-contained and should not require a separate .NET installation


> ⚠️ **Compatibility warning: RetroBar**
>
> It is recommended not to run Win7Taskbar together with RetroBar. Both applications replace the Windows taskbar by hiding it and may conflict with each other, causing duplicate or missing taskbar elements.
>
> **Use one or the other at a time.**
>
> ExplorerPatcher is different and can complement Win7Taskbar on Windows 11.
>
> Open-Shell can still be used alongside Win7Taskbar. To give it the Windows key, set **Windows key opens: Windows** in Properties → Extra.

## Installation Guide

To install this software, the subsequent steps need to be followed:
1. Download the latest release from [Releases](https://github.com/babamohammed2022/Win7Taskbar/releases).
2. Extract the complete package, keeping `Themes/`, `Resources/`, and `Languages/` next to `Win7Taskbar.exe`.
3. Run `Win7Taskbar.exe`.
4. To exit, right-click the clock → **Properties** → **Close Win7Taskbar**.

## Start Menu (v1.3.0-alpha)

Win7Taskbar hosts its own Windows 7-style Start Menu **in the same process as the taskbar** (`Win7Taskbar.exe`), on a dedicated STA thread with its own WPF Dispatcher. A second tiny process, `Win7StartHelper.exe`, owns the low-level keyboard hook and the Windows-key state machine. There is no `Win7StartMenu.exe`, no named pipe, and no menu-side mutex.

IPC is two session-local events:

| Event | Direction | Role |
| --- | --- | --- |
| `Local\Win7Taskbar_WindowsKey` | helper → menu | lone Windows key |
| `Local\Win7Taskbar_StartMenuHeartbeat` | menu DispatcherTimer → helper | menu still alive |

The setting **Windows key opens: Our Start Menu | Windows** (Properties → Extra, default our menu) is stored in `settings.json` and mirrored to `HKCU\Software\Win7Taskbar\WindowsKeyOpensOurMenu`. Open-Shell can be used alongside: choose **Windows** if you want Open-Shell / native Start to receive the key. The hook is **not** auto-disabled just because Open-Shell is installed.

### Failure modes

| Failure | What happens |
| --- | --- |
| Helper crash / exit | `WH_KEYBOARD_LL` unloads. Windows key passes through to native Start / Open-Shell. |
| Taskbar / menu process exits | Helper sees the process handle and stops consuming the Windows key, then quits. |
| Menu heartbeat timeout (~2 s) | Helper stops consuming the Windows key so native Start / Open-Shell works. Heartbeat resumes consume. |
| Helper missing from the package | Orb still toggles our menu. The Windows key is not consumed (native Start works). |
| Setting = Windows | Helper does not eat lone Win. Native Start / Open-Shell receives it. |
| Scan / Search COM failure | Program list or file hits are empty; the menu still opens and power actions still run. |

The menu window is created during `Win7Taskbar.exe` startup and only shown/focused on the orb or a lone Windows key. Typing goes straight to the search box.

## Troubleshooting

**"Win7TaskbarCore.dll non è stata trovata" / "Win7TaskbarCore.dll was not found"**

The native core normally lives next to `Win7Taskbar.exe`, but the executable also carries an internal copy: on start it checks the file beside it and, when the file is missing, belongs to another version, or cannot be loaded, it restores the internal copy by itself (next to the executable, or in `%LOCALAPPDATA%\Win7Taskbar\core` when that folder is not writable) and keeps running.

If the message still appears, the automatic repair was blocked too. The usual causes are:

* an antivirus quarantining the native DLL, it is recommended to add `Win7Taskbar` to its exclusions and extract the package again;
* an incomplete extraction, it is suggested to extract the whole ZIP, keeping every file together;
* launching the executable from inside the ZIP viewer - extract first, then run.

## Current status

Win7Taskbar is still under development. Some features are incomplete or recreated, particularly parts of the notification area and system UI on Windows 11. Window thumbnail previews use a direct DWM surface with the existing image border and close button, without a coloured backing panel. Jump Lists open the Windows 7 way: press a task button with the left button and drag away from the bar (up for a bottom bar) - the list appears directly above the button, left-aligned with it, exactly where the Windows 7 shell opens its jump view, and stays anchored there for the whole gesture while the row under the cursor is highlighted; releasing on a row activates it, releasing over the list or the button leaves the list open to click, releasing elsewhere cancels. A plain click still activates the window and a drag along the bar still reorders the icons (the two gestures arbitrate by the first threshold crossed, so they never conflict). The list shows the application's real Shell data - recent/frequent items and, for a running window group, the Windows 7 window tasks (the pinned (custom) items are not shown: Windows 7 and later expose no public API that reads or removes them) - never appears from the right-click, whose menu is unchanged, and honors the `Start_JumpListItems` policy (see `docs/JUMPLIST-RE-VERIFICATION.md` for the Windows 7 reverse-engineering notes).


Other known limitations include unsupported decorative taskbar rotation and system windows that are hooked and repositioned rather than fully recreated.

## Build

For a standard Windows build, double-click **`build.bat`** in the `compilation files` folder.

For development and build instructions, see [`docs/PROJECT-INSTRUCTIONS.md`](./docs/PROJECT-INSTRUCTIONS.md).

For a quick guide, see [`docs/QUICK-START.md`](./docs/QUICK-START.md).

## Contributing

Bug reports, reproductions, pull requests, documentation, and code improvements are welcome.

Please read [`AGENTS.md`](./docs/AGENTS.md) before making changes.

## Credits

* MAHMOGAMER - Arabic translation
* WinBoeing777 - Testing on Windows 10 22H2 and providing resources
* AdministratoX - Testing on Windows 11 25H2

Win7Taskbar was created using work from projects including RetroBar, ExplorerPatcher, and ManagedShell, but it is an independent project and is not affiliated with or endorsed by any of them. It acts as a community open-source project to help improve the user's experience.

Additional information and attribution details are available in the `docs` folder.

## Note

This software is not endorsed by, affiliated with, or sponsored by Microsoft Corporation.
Windows and related trademarks are the property of Microsoft Corporation.

## Registry keys

Everything the program touches in the registry, and nothing else:

* **Autostart (reversible, user-controlled).**
  `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`, value `Win7Taskbar`.
  Written when the "Start automatically with Windows" checkbox is confirmed
  with OK/Apply, deleted when it is unchecked. Never touched otherwise.
* **Windows-key routing (our key, user setting).**
  `HKCU\Software\Win7Taskbar\WindowsKeyOpensOurMenu` (`DWORD` 1 = our Start
  Menu, 0 = Windows / Open-Shell). Written whenever the Properties Extra
  setting is applied. Not a system Start-menu replacement.
* **Reversible shell choices (original state preserved).**
  `HKCU\...\CurrentVersion\ImmersiveShell`: `UseWin32TrayClockExperience`
  (1 = classic Aero clock), `EnableMtcUvc` (0 = classic volume mixer) and
  `UseWin32BatteryFlyout` (1 = real Windows 7 battery flyout).
  Before Win7Taskbar modifies any of them for the first time, it records the
  original state - whether the value existed and its original `DWORD` - under
  its own key `HKCU\Software\Win7Taskbar\RegistryBackup`. The backup is taken
  once and never overwritten: restarts, Apply/OK and switching between
  "Windows 7" and "Windows 10/11" do not touch it.
  On clean shutdown Win7Taskbar restores each value to its exact original
  state (the original `DWORD`, or the value removed if it did not exist) and
  then deletes the corresponding backup. As a safety guard, a value is
  restored only if it still holds what Win7Taskbar last applied: changes made
  by the user or another program while Win7Taskbar was active are never
  silently overwritten (the restore is skipped and the backup kept).
  If the program is killed or crashes, values and backup stay as they are;
  the next clean shutdown restores the true original.
* **Clock and volume (managed, restored on exit).**
  Written at startup and on every OK/Apply from the flyout choices, like
  ExplorerPatcher does. They stay in force while the program runs and are
  handed back to their original state on clean exit (see above).
* **Battery (native transient, managed backup copy).**
  Only the native core ever writes `UseWin32BatteryFlyout`, and only as `=1`
  around an attempt to open the real Windows 7 battery flyout; the previous
  value is restored (or the value deleted if it did not exist) when the
  attempt ends. That restore also runs on tray stop, on clean shutdown, on
  session ending, in the crash filter, and from the backup file at
  `%LOCALAPPDATA%\Win7Taskbar\battery-key-backup.dat` if a previous run died
  mid-attempt. The managed layer never writes this value; it only keeps a
  backup copy of the original in `RegistryBackup`, dropped at shutdown
  without touching the live value.
* **Legacy self-cleaning.**
  `HKCU\SOFTWARE\Win7Taskbar\TrayIconPrefs2` (tray icon preferences stored by
  older builds) is migrated into `trayicons.ini` and then removed.
* **Read-only.**
  `HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion` (OS name/build for the
  log header), `HKCR\Applications\...\SupportedTypes` (file extensions an
  executable accepts, for task-button drop targets) and the Run key above
  (to read the autostart state) are only ever read.

## License

This software is licensed under **GNU GPL v3.0 or later**.

See [`LICENSE`](./docs/LICENSE), [`CREDITS.txt`](./docs/CREDITS.txt), and [`THIRD-PARTY-NOTICES.md`](./docs/THIRD-PARTY-NOTICES.md) for licensing and attribution details.
