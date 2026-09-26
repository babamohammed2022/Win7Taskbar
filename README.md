# Win7Taskbar

<img width="1366" height="61" alt="image" src="https://github.com/user-attachments/assets/fcc86ac5-1ff2-418f-a7df-aca0039b1108" />

A Windows 7-inspired taskbar recreation for Windows 10 and 11.

> ## ⚠️ PROJECT ABANDONED — NO LONGER MAINTAINED
>
> **Win7Taskbar is abandoned and no longer maintained.** Development stopped permanently due to the lack of available time. No further updates, releases, bug fixes, or support will be provided by the original author.
>
> The sole retained release, [Win7Taskbar v1.3.26-alpha](https://github.com/babamohammed2022/Win7Taskbar/releases/tag/v1.3.26-alpha), is marked **Latest**. It is an **incomplete alpha** and does not satisfy all of the project's initial objectives.
>
> The source code remains available **for consultation, forking, and independent development only**, subject to repository access and the GNU GPL v3.0-or-later license.
>
> **Do not expect any response to issues, pull requests, or support requests.** The repository is effectively archived.

## Project status — abandoned

Maintenance of Win7Taskbar has been **permanently discontinued**. The project is **abandoned**. Development stopped at the state represented by this repository, and no further maintainer-led work of any kind is planned.

- **No further releases.** `v1.3.26-alpha` is the only remaining release and is final.
- **No further tags.** All other tags and releases have been removed.
- **No automated release publishing.** The automation has been removed.
- **No issue handling.** Issues will not be triaged, answered, or fixed.
- **No pull-request review.** Pull requests will not be reviewed or merged.
- **No support.** No help will be provided for installation, configuration, or troubleshooting.
- **No compatibility analysis.** Compatibility with other tools will not be assessed further.

The source is retained for **consultation, forking, and independent development**, subject to repository access and the GNU GPL v3.0-or-later license. Acknowledgement is extended to all contributors, testers, translators, and other participants.

The software was tested on Windows 8.1, Windows 10 21H2, Windows 10 22H2, Windows 11 23H2, Windows 11 24H2, Windows 11 25H2, and Windows Server 2025. Windows 8.1 was tested successfully and works reasonably well, although some platform-specific differences may affect individual features. Some functionality on Windows 11, particularly the notification area, is recreated because newer versions of Windows no longer expose all of the same taskbar functionality available on previous versions.

In the archived source, Windows 11 notification-area access uses the accessibility surface of the XAML tray. Use of the legacy Win32 layer shipped inside the modern taskbar as a *data/control plane only, never shown* had been investigated, but verified parity was not reached before development stopped (see `docs/WIN11-NATIVE-TRAY-RESEARCH.md` and the Phase 0 probe `tools/win11-native-tray-probe/`). **ExplorerPatcher remained optional** for users who prefer a Windows 10-style taskbar environment; no further development of native Windows 11 tray support is planned.

The archived source includes its own Windows 7-style Start Menu (see below). **Open-Shell is optional** for users who prefer its menu instead: set **Windows key opens: Windows** in Properties → Extra.

Testing was limited to ExplorerPatcher and Open-Shell. Compatibility with other third-party tools was not comprehensively assessed; no further compatibility analysis is planned.

**Project status: abandoned. Retained release: `v1.3.26-alpha` (final, incomplete alpha).**

## Screenshot (Windows 7 skin)

<img width="1366" height="61" alt="image" src="https://github.com/user-attachments/assets/fcc86ac5-1ff2-418f-a7df-aca0039b1108" />

## Screenshot (Windows 8.1 skin)

<img width="1366" height="51" alt="image" src="https://github.com/user-attachments/assets/470fc73f-0ab4-4601-8c92-94ff5f5d23c6" />

## Requirements

* Windows 8.1, Windows 10 or Windows 11 (64 bit)
* The retained `v1.3.26-alpha` package is self-contained and does not require a separate .NET installation.

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

> **Note:** These instructions are provided **as-is**, for the final `v1.3.26-alpha` release only. No support is available.

1. Download the retained [Win7Taskbar v1.3.26-alpha release](https://github.com/babamohammed2022/Win7Taskbar/releases/tag/v1.3.26-alpha). This is an incomplete alpha; see the project status notice above.
2. Extract the complete package, keeping `Themes/`, `Resources/`, and `Languages/` next to `Win7Taskbar.exe`.
3. Run `Win7Taskbar.exe`.
4. To exit, right-click the clock → **Properties** → **Close Win7Taskbar**.

## Start Menu (v1.3.0-alpha)

The archived source hosts its own Windows 7-style Start Menu **in the same process as the taskbar** (`Win7Taskbar.exe`), on a dedicated STA thread with its own WPF Dispatcher. A second tiny process, `Win7StartHelper.exe`, owns the low-level keyboard hook and the Windows-key state machine. There is no `Win7StartMenu.exe`, no named pipe, and no menu-side mutex.

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

> **No support is provided.** These notes are retained for historical reference only. The project is abandoned, and no one will respond to questions about them.

**"Win7TaskbarCore.dll non è stata trovata" / "Win7TaskbarCore.dll was not found"**

The native core normally lives next to `Win7Taskbar.exe`, but the executable also carries an internal copy: on start it checks the file beside it and, when the file is missing, belongs to another version, or cannot be loaded, it restores the internal copy by itself (next to the executable, or in `%LOCALAPPDATA%\Win7Taskbar\core` when that folder is not writable) and keeps running.

If the message still appears, the automatic repair was blocked too. The usual causes are:

* an antivirus quarantining the native DLL, it is recommended to add `Win7Taskbar` to its exclusions and extract the package again;
* an incomplete extraction, it is suggested to extract the whole ZIP, keeping every file together;
* launching the executable from inside the ZIP viewer - extract first, then run.

## Status at abandonment

At the time maintenance ended, the implementation remained incomplete relative to the project's initial objectives. Some features were incomplete or recreated, particularly parts of the notification area and system UI on Windows 11. No further maintainer-led development, verification, or support is planned.

The source snapshot includes window thumbnail previews using a direct DWM surface with the existing image border and close button, without a coloured backing panel. Jump Lists use the Windows 7-style drag-away gesture: press a task button and drag away from the bar (up for a bottom bar); the list appears directly above the button, left-aligned with it, and remains anchored while the row under the cursor is highlighted. Releasing on a row activates it, releasing over the list or button leaves the list open, and releasing elsewhere cancels. A plain click activates the window and a drag along the bar reorders icons. The list shows Shell data—recent/frequent items and, for a running window group, Windows 7 window tasks; pinned custom items are not shown because Windows 7 and later expose no public API to read or remove them—and honors the `Start_JumpListItems` policy (see `docs/JUMPLIST-RE-VERIFICATION.md` for historical reverse-engineering notes).

Other known limitations include unsupported decorative taskbar rotation and system windows that are hooked and repositioned rather than fully recreated.

## Build

> **No support is provided for building.** These instructions are retained for historical reference and for independent forks. The project is abandoned.

For a standard Windows build, double-click **`build.bat`** in the `compilation files` folder.

For development and build instructions, see [`docs/PROJECT-INSTRUCTIONS.md`](./docs/PROJECT-INSTRUCTIONS.md).

For a quick guide, see [`docs/QUICK-START.md`](./docs/QUICK-START.md).

## Source access and independent development

**No maintainer-led issue handling, pull-request review, or further development is planned.** The project is abandoned.

The source may be consulted, forked, and developed independently, subject to repository access and the applicable license. If you wish to continue the project, you are free to fork it under the terms of the GNU GPL v3.0-or-later.

## Credits

* MAHMOGAMER - Arabic translation
* WinBoeing777 - Testing on Windows 10 22H2 and providing resources
* AdministratoX - Testing on Windows 11 25H2

Win7Taskbar was created using work from projects including RetroBar, ExplorerPatcher, and ManagedShell, but it is an independent project and is not affiliated with or endorsed by any of them.

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

---

