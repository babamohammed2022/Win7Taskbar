# Win7Taskbar

<img width="1366" height="61" alt="image" src="https://github.com/user-attachments/assets/fcc86ac5-1ff2-418f-a7df-aca0039b1108" />

A Windows 7-inspired taskbar recreation and for Windows 10 and 11.

Win7Taskbar is a system utility that recreates the Windows 7-style taskbar and Superbar using a XAML frontend with a native C++/Win32 backend. It includes grouped task buttons, the notification area, system flyouts, overflow handling, Jump Lists, and a Windows 7-inspired Properties interface.

The software has been tested on Windows 8.1, Windows 10 21H2, Windows 10 22H2, Windows 11 24H2 and Windows 11 25H2. Windows 8.1 has been tested successfully and the software works reasonably well on this version, although some platform-specific differences may affect individual features. Some functionality on Windows 11, particularly the notification area, is recreated because newer versions of Windows no longer expose all of the same taskbar functionality available on previous versions.

On Windows 11, **ExplorerPatcher is recommended for the best experience**, but it is optional. It can provide a more compatible Windows 10-style taskbar environment and allow Win7Taskbar to use more native notification-area functionality.

**Open-Shell is also recommended alongside Win7Taskbar** for a more complete Windows 7-style desktop experience, particularly for restoring a Windows 7-style Start menu. Open-Shell is optional and complements Win7Taskbar rather than replacing it.

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
> Open-Shell can also be used alongside Win7Taskbar and is recommended when a Windows 7-style Start menu is desired.

## Installation Guide

To install this software, the subsequent steps need to be followed:
1. Download the latest release from [Releases](https://github.com/babamohammed2022/Win7Taskbar/releases).
2. Extract the complete package, keeping `Themes/`, `Resources/`, and `Languages/` next to `Win7Taskbar.exe`.
3. Run `Win7Taskbar.exe`.
4. To exit, right-click the clock → **Properties** → **Close Win7Taskbar**.

## Troubleshooting

**"Win7TaskbarCore.dll non è stata trovata" / "Win7TaskbarCore.dll was not found"**

The native core normally lives next to `Win7Taskbar.exe`, but the executable also carries an internal copy: on start it checks the file beside it and, when the file is missing, belongs to another version, or cannot be loaded, it restores the internal copy by itself (next to the executable, or in `%LOCALAPPDATA%\Win7Taskbar\core` when that folder is not writable) and keeps running.

If the message still appears, the automatic repair was blocked too. The usual causes are:

* an antivirus quarantining the native DLL — add `Win7Taskbar` to its exclusions and extract the package again;
* an incomplete extraction — extract the whole ZIP, keeping every file together;
* launching the executable from inside the ZIP viewer — extract first, then run.

## Current status

Win7Taskbar is still under development. Some features are incomplete or recreated, particularly parts of the notification area and system UI on Windows 11. Window thumbnail previews use a direct DWM surface with the existing image border and close button, without a coloured backing panel. Jump Lists are incomplete and temporarily disabled; their managed/native implementation remains in the source tree for completion. The right-click menu is unchanged.


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
