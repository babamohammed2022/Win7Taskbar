# Win7Taskbar

<img src="docs/icon-256.png" alt="Win7Taskbar" width="96" align="left" hspace="12" vspace="4">

A Windows 7-inspired taskbar recreation and for Windows 10 and 11.

Win7Taskbar is a system utility that recreates the Windows 7-style taskbar and Superbar using a XAML frontend with a native C++/Win32 backend. It includes grouped task buttons, the notification area, system flyouts, overflow handling, Jump Lists, and a Windows 7-inspired Properties interface.

The software has been tested on Windows 10 21H2, Windows 10 22H2, Windows 11 24H2 and Windows 11 25H2. Some functionality on Windows 11, particularly the notification area, is recreated because newer versions of Windows no longer expose all of the same taskbar functionality available on previous versions.

On Windows 11, **ExplorerPatcher is recommended for the best experience**, but it is optional. It can provide a more compatible Windows 10-style taskbar environment and allow Win7Taskbar to use more native notification-area functionality.


**Current state: `Alpha`**

## Screenshot

<img width="1366" height="61" alt="image" src="https://github.com/user-attachments/assets/fcc86ac5-1ff2-418f-a7df-aca0039b1108" />



## Requirements

* Windows 10 or Windows 11
* x64
* Official releases are self-contained and do not require a separate .NET installation


> ⚠️ **Compatibility warning: RetroBar**
>
> It is recommended not to run Win7Taskbar together with RetroBar. Both applications replace the Windows taskbar by hiding it and may conflict with each other, causing duplicate or missing taskbar elements.
>
> **Use one or the other at a time.**
>
> ExplorerPatcher is different and can complement Win7Taskbar on Windows 11.

## Installation Guide

To install this software, the subsequent steps need to be followed:
1. Download the latest release from [Releases](https://github.com/babamohammed2022/Win7Taskbar/releases).
2. Extract the complete package, keeping `Themes/`, `Resources/`, and `Languages/` next to `Win7Taskbar.exe`.
3. Run `Win7Taskbar.exe`.
4. To exit, right-click the clock → **Properties** → **Close Win7Taskbar**.

## Current status

Win7Taskbar is still under development. Some features are incomplete or recreated, particularly parts of the notification area and system UI on Windows 11. Window thumbnail previews use direct live DWM thumbnails. Jump Lists use the real Shell jump-list APIs: press the left button on a taskbar button and drag upward to open one; releasing the drag leaves it open, and a separate item click selects an entry while an outside click dismisses it. The right-click menu is unchanged. These interactions still need verification on real hardware at all display scales.

*Customize notification area...* opens Windows' native Notification Area settings directly; Win7Taskbar does not recreate that Control Panel page.

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

Win7Taskbar was created using work from projects including RetroBar, ExplorerPatcher, and ManagedShell.

Additional information and attribution details are available in the `docs` folder.

## License

This software is licensed under **GNU GPL v3.0 or later**.

See [`LICENSE`](./docs/LICENSE), [`CREDITS.txt`](./docs/CREDITS.txt), and [`THIRD-PARTY-NOTICES.md`](./docs/THIRD-PARTY-NOTICES.md) for licensing and attribution details.
