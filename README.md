# Win7Taskbar

<img src="docs/icon-256.png" alt="Win7Taskbar" width="96" align="left" hspace="12" vspace="4">

A Windows 7-inspired taskbar for Windows 10 and 11.

Win7Taskbar combines a WPF/XAML frontend with a native C++/Win32 backend to reproduce the Windows 7 Superbar experience, including grouped task buttons, jump lists, the notification area, system flyouts, overflow handling, and the Properties interface.

**Current version: `1.0.0-alpha`** — an incomplete alpha release. See [Releases](https://github.com/babamohammed2022/Win7Taskbar/releases) for downloads and release notes.

> **Self-contained:** official releases include the .NET runtime and do not require a separate .NET installation.

## Requirements

- Windows 10 21H2 or later, or Windows 11
- x64
- No separate .NET installation required for official releases

## Run

1. Download the latest release from [Releases](https://github.com/babamohammed2022/Win7Taskbar/releases).
2. Extract the complete package, keeping `Themes/`, `Resources/`, and `Languages/` next to `Win7Taskbar.exe`.
3. Run `Win7Taskbar.exe`.
4. To exit, right-click the clock → **Properties** → **Close Win7Taskbar**.

## Current status

Win7Taskbar is still under development. Window thumbnail previews are temporarily disabled because the previous preview implementations were not reliable on real systems. The application-name tooltip remains available.

Other known limitations include unsupported decorative taskbar rotation and system windows that are hooked and repositioned rather than fully recreated.

## Build

For the normal one-click build on Windows, double-click **`COMPILA.bat`**.

For complete build, packaging, architecture, and development instructions, see [`docs/PROJECT-INSTRUCTIONS.md`](./docs/PROJECT-INSTRUCTIONS.md).

For a concise user and build guide, see [`docs/QUICK-START.md`](./docs/QUICK-START.md).

## Repository layout

```text
Win7Taskbar/
├── README.md
├── LICENSE
├── AGENTS.md
├── THIRD-PARTY-NOTICES.md
├── CREDITS.txt
├── COMPILA.bat
├── Win7Taskbar.sln
│
├── src/                 application and native source code
├── native/              native C++/Win32 backend
├── build/               build and publishing scripts
├── ci/                  CI configuration
├── docs/                project documentation
├── Themes/              runtime themes
├── Resources/           runtime resources
├── Languages/           localization files
└── dist/                native binaries used by the build/package
```

## Contributing

Bug reports, reproductions, and pull requests are welcome. New documentation and code comments should be written in English.

Please read [`AGENTS.md`](./AGENTS.md) before making changes.

## Credits and licensing

Win7Taskbar builds on work from projects including RetroBar, ExplorerPatcher, ManagedShell, and Windows7RetrobarTheme.

The project is licensed under **GNU GPL v3.0 or later**. See [`LICENSE`](./LICENSE), [`CREDITS.txt`](./CREDITS.txt), and [`THIRD-PARTY-NOTICES.md`](./THIRD-PARTY-NOTICES.md) for licensing and attribution details.
