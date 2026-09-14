# Win7Taskbar

<img src="docs/icon-256.png" alt="Win7Taskbar" width="96" align="left" hspace="12" vspace="4">

A Windows 7-inspired taskbar recreation for Windows 10 and 11.

Win7Taskbar is a software that recreates the Windows 7 taskbar on Windows 10 and Windows 11. It combines a XAML frontend with a native C++/Win32 backend to reproduce the Windows 7 Superbar, including grouped task buttons, the notification area, system flyouts, overflow handling, and a very similar Properties interface.

This software has been tested on Windows 10 21H2, Windows 10 22H2, Windows 11 24H2 and Windows 11 25H2. However, on Windows 11, some functionality, particularly the system tray, is implemented as a recreation because the newer versions of the operating system no longer expose the same taskbar elements that were available on previous versions of Windows, so the system tray behavior on Windows 11 is replicated rather than directly provided by the native taskbar.

On Windows 11, **ExplorerPatcher is recommended for the best experience**. Win7Taskbar can work without it, but ExplorerPatcher provides a more compatible Windows 10-style taskbar/shell environment and can expose the notification-area elements that Win7Taskbar can use directly. This can avoid falling back to recreated notification-area icons on Windows 11. ExplorerPatcher is optional and is not required for Win7Taskbar to run.

> ⚠️ **Compatibility warning: RetroBar**
>
> **Do not run Win7Taskbar together with RetroBar.**
>
> Both applications hide and replace the Windows taskbar. Running them simultaneously can cause taskbar conflicts, unexpected behavior, duplicate UI elements, or an apparently missing taskbar.
>
> **Use one or the other, not both at the same time.**
>
> **Note:** ExplorerPatcher is different. On Windows 11, it can complement Win7Taskbar by restoring or exposing native Explorer taskbar functionality that Win7Taskbar can use.

**Current state: `Alpha`**

## Screenshot

<img width="1366" height="61" alt="image" src="https://github.com/user-attachments/assets/fcc86ac5-1ff2-418f-a7df-aca0039b1108" />

## Requirements

- Windows 10/11
- x64
- No separate .NET installation required for official releases
> **Self-contained:** official releases include the .NET runtime and do not require a separate .NET installation.

## Run

1. Download the latest release from [Releases](https://github.com/babamohammed2022/Win7Taskbar/releases).
2. Extract the complete package, keeping `Themes/`, `Resources/`, and `Languages/` next to `Win7Taskbar.exe`.
3. Run `Win7Taskbar.exe`.
4. To exit, right-click the clock → **Properties** → **Close Win7Taskbar**.

## Current status

Win7Taskbar is still under development. Window thumbnail previews are temporarily disabled because the previous preview implementations were not reliable on real systems. Additionally, jump lists are not implemented yet. The application-name tooltip remains available.

Other known limitations include unsupported decorative taskbar rotation and system windows that are hooked and repositioned rather than fully recreated.

## Build

For the normal one-click build on Windows, double-click **`build.bat`** inside the `compilation files` folder.

For complete build, packaging, architecture, and development instructions, see [`docs/PROJECT-INSTRUCTIONS.md`](./docs/PROJECT-INSTRUCTIONS.md).

For a concise user and build guide, see [`docs/QUICK-START.md`](./docs/QUICK-START.md).

## Contributing

Bug reports, reproductions, and pull requests are welcome. New documentation and code comments should be written in English.

Please read [`AGENTS.md`](./docs/AGENTS.md) before making changes.

## Credits

- MAHMOGAMER - Arabic translation
- WinBoeing777 - Testing on Windows 10 22H2 and providing resouces
- AdministratoX - Testing on Windows 11 25H2

Win7Taskbar was created using work from projects including RetroBar, ExplorerPatcher and ManagedShell.
For additional information, please refer to the docs folder.

## License
The project is licensed under **GNU GPL v3.0 or later**. See [`LICENSE`](./docs/LICENSE), [`CREDITS.txt`](./docs/CREDITS.txt), and [`THIRD-PARTY-NOTICES.md`](./docs/THIRD-PARTY-NOTICES.md) for licensing and attribution details.
