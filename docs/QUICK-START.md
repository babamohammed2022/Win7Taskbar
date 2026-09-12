# Win7Taskbar 1.0.0-alpha — Quick Start

This guide is for users who want to run Win7Taskbar and, optionally, build it themselves. No programming knowledge is required for normal use.

---

## 1. What it does

Win7Taskbar replaces the Windows 10/11 taskbar with a Windows 7-inspired taskbar, including Superbar window grouping, jump lists, the notification area, system flyouts (clock, network, volume, battery), the overflow panel, and the Properties window.

Only the new taskbar is displayed. The original Windows taskbar is hidden and restored when Win7Taskbar exits normally.

---

## 2. Requirements

| Item | Requirement |
| --- | --- |
| Operating system | Windows 10 (21H2 or later) or Windows 11 |
| Architecture | **x64 (64-bit)** |
| .NET installed | **No.** The official package includes the runtime. |

---

## 3. Installation

1. Open the [release page](https://github.com/babamohammed2022/Win7Taskbar/releases/tag/v1.0.0-alpha), or the [Releases](https://github.com/babamohammed2022/Win7Taskbar/releases) page for all versions.
2. Download `Win7Taskbar-1.0.0-alpha-win-x64.zip`.
3. Extract the entire ZIP to any folder, for example `C:\Win7Taskbar`. Keep `Themes`, `Resources`, and `Languages` next to `Win7Taskbar.exe`; the theme is loaded from disk at startup.
4. Run **`Win7Taskbar.exe`**.

### Optional file verification

The release page provides the SHA-256 hash of the package. To verify it, open PowerShell in the download folder and run:

```powershell
Get-FileHash .\Win7Taskbar-1.0.0-alpha-win-x64.zip -Algorithm SHA256
```

The displayed `Hash` must match the published value.

### Closing Win7Taskbar

Right-click the **clock** → **Properties** → **Close Win7Taskbar**.

Always use this command when possible so the original taskbar and AppBar registration are restored cleanly.

### Settings

Right-click the clock → **Properties**. Settings include the clock, flyout style, app search, language, notification area, toolbars, and exit options.

---

## 4. Troubleshooting

* **The taskbar does not appear.** Open a terminal in the program folder and run:

  ```powershell
  .\Win7Taskbar.exe
  ```

  The console output shows whether `Win7TaskbarCore.dll` was loaded. Include that output when opening a GitHub issue.

* **Icons or the theme are missing.** Make sure `Themes\`, `Resources\`, and `Languages\` are in the same folder as `Win7Taskbar.exe`.

* **Windows Defender / SmartScreen warning.** The program is currently unsigned. The source code is public in this repository.

* **Window previews do not appear.** This is intentional in the current alpha release. Window thumbnails are temporarily disabled; the application-name tooltip remains available.

---

## 5. Building from source

### One-click build

1. Download or clone the repository.
2. Double-click **`COMPILA.bat`**.

The script:

* installs the **.NET 8 SDK** if required;
* builds the native C++ component when the required tools are available, otherwise reuses the native DLLs included in `dist\`;
* creates a **self-contained** package in `dist-package\`;
* creates the release ZIP.

### Official PowerShell script

```powershell
pwsh -File build/publish.ps1 -Zip
pwsh -File build/publish.ps1 -Zip -SkipNative
```

For the complete build and architecture instructions, see [`PROJECT-INSTRUCTIONS.md`](./PROJECT-INSTRUCTIONS.md).

---

## 6. FAQ

**Does Win7Taskbar require .NET to be installed?**  
No. Official releases are self-contained and include the .NET runtime.

**Can I use the original Windows taskbar at the same time?**  
No. Win7Taskbar hides the original taskbar while it is running.

**Does it modify Windows system files?**  
No. It creates its own AppBar window and hides the original taskbar. Closing Win7Taskbar restores the original taskbar.

**How do I report a problem?**  
Open a GitHub issue with your Windows version, the steps that caused the problem, the expected behaviour, the actual behaviour, and any relevant console output.
