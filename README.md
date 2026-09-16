# Win7Taskbar

<img src="docs/icon-256.png" alt="Win7Taskbar" width="96" align="left" hspace="12" vspace="4">

A Windows 7-inspired taskbar recreation for Windows 10 and 11.

Win7Taskbar is a software that recreates the Windows 7 taskbar on Windows 10 and Windows 11. It combines a XAML frontend with a native C++/Win32 backend to reproduce the Windows 7 Superbar, including grouped task buttons, the notification area, system flyouts, overflow handling, and a very similar Properties interface.

This software has been tested on Windows 10 21H2 and Windows 10 22H2.

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

### Icon assets

The tray icons the application draws by itself (volume, network, battery — see
`native/src/TrayFallbackIcons.cpp`) and the battery glyphs of the flyout are
compiled into the native DLL: `native/src/TrayIconAssets.inc` and
`native/src/BatteryAssets.inc` are generated files and the only icon files the
repository carries.

The PNG sources are kept outside the history in `assets/icon-sources/` (ignored
by git) and turned into the tables by
[`compilation files/icons_to_base64.py`](./compilation%20files/icons_to_base64.py):

```bash
# tray icons (the names are the order of the generated table)
python3 "compilation files/icons_to_base64.py" \
  --icon kVolume0=assets/icon-sources/volume-0.png ... \
  --icon kNetworkNotWorking=assets/icon-sources/network-not-working.png \
  --cpp-out native/src/TrayIconAssets.inc

# battery strip: the drawings are picked on the alpha bounding box, no
# coordinate is written by hand; --cells chooses which drawing goes where
python3 "compilation files/icons_to_base64.py" --style battery \
  --strip assets/icon-sources/Bitmap303.png \
  --cells 0,1,2,3,4,5,6,7,8,8,32,... --names kLevel1,... \
  --cpp-out native/src/BatteryAssets.inc
```

`--list-runs` prints the inventory of a strip (index, position, size) and the
header of the generated file records the mapping that was used, so replacing
the artwork does not require guessing which drawing was which.

## Contributing

Bug reports, reproductions, and pull requests are welcome. New documentation and code comments should be written in English.

Please read [`AGENTS.md`](./docs/AGENTS.md) before making changes.

## Credits

- MAHMOGAMER - Arabic translation
- WinBoeing777 - Testing on Windows 10 22H2 and providing resouces

Win7Taskbar was created using work from projects including RetroBar, ExplorerPatcher and ManagedShell.
For additional information, please refer to the docs folder.



## License 
The project is licensed under **GNU GPL v3.0 or later**. See [`LICENSE`](./docs/LICENSE), [`CREDITS.txt`](./docs/CREDITS.txt), and [`THIRD-PARTY-NOTICES.md`](./docs/THIRD-PARTY-NOTICES.md) for licensing and attribution details.
