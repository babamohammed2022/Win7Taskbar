# Win7Taskbar 1.0.0-alpha — Rapid Guide

This guide is for anyone who **wants to use** Win7Taskbar and, if needed, for anyone who wants to **build it themselves**. No programming knowledge is required.

---

## 1. What it does

It replaces the Windows 10/11 taskbar with a **Windows 7-style taskbar**: Superbar with window grouping, jump lists, notification area, system flyouts (clock, network, volume, battery), overflow panel, and Properties window.

Only the new taskbar is shown: the Windows taskbar remains hidden and is properly restored when you close the program.

---

## 2. Requirements

| Item | Value |
| --- | --- |
| System | Windows 10 (21H2 or later) or Windows 11 |
| Architecture | **x64** (64-bit) |
| .NET installation | **No, not required**: the runtime is included in the package |

---

## 3. Installation (end users)

1. Go to the release page:
   **https://github.com/babamohammed2022/Win7Taskbar/releases/tag/v1.0.0-alpha**
   (or **https://github.com/babamohammed2022/Win7Taskbar/releases** to see all versions).
2. Download `Win7Taskbar-1.0.0-alpha-win-x64.zip` (about 64 MB: the .NET runtime is included, so you do not need to install anything else).
3. **Extract the entire ZIP** to any folder (for example, `C:\Win7Taskbar`). Keep the `Themes`, `Resources`, and `Languages` subfolders next to `Win7Taskbar.exe`: the theme is loaded from disk at startup.
4. Run **`Win7Taskbar.exe`**.

### Verifying the downloaded file (optional)

The release page provides the package **SHA-256** hash. To verify it, open PowerShell in the download folder and run:

```powershell
Get-FileHash .\Win7Taskbar-1.0.0-alpha-win-x64.zip -Algorithm SHA256
```

The `Hash` value must match the published value. If it matches, the file is intact and has not been modified.

> **Before anything else**, it is recommended to create a Windows restore point so you can quickly revert the system if needed.

### How to close Win7Taskbar

Right-click the **clock** (bottom-right) → **Properties** → **"Close Win7Taskbar"**. Always use this command so the Windows taskbar is restored cleanly.

### Settings

Right-click the clock → **Properties**. Available settings include language, clock, flyout style, app search, notification area, additional taskbars, and closing the program. Multiple languages are supported, including English, Italian, and Arabic.

---

## 4. If something does not work

* **The taskbar does not appear.** Open the program folder, hold `Shift`, right-click an empty area → *Open PowerShell here*, and run:

  ```powershell
  .\Win7Taskbar.exe
  ```

  The output shows whether the native component (`Win7TaskbarCore.dll`) was loaded. Include this output when opening an issue on GitHub.

* **Icons or the theme are missing.** You may have extracted the ZIP into an extra nested folder without keeping `Themes\`, `Resources\`, and `Languages\` next to the executable. Extract the complete ZIP again using *Extract All*.

* **Antivirus / SmartScreen warning.** The program is not digitally signed. If Windows displays a warning, select *More info* → *Run anyway*. The source code is publicly available in this repository.

* **Window previews do not appear.** This is intentional: previews are disabled in this alpha release (see the status in `README.md`); the application-name tooltip is available.

---

## 5. Building from source (optional)

### Easiest method — double-click

1. Download the repository (**Code → Download ZIP**) and extract it, or run:
   `git clone https://github.com/babamohammed2022/Win7Taskbar.git`
2. Double-click **`build.bat`** inside the `compilation files/` folder.

The script:
* automatically installs the **.NET 8 SDK** if it is missing (without administrator rights);
* also builds the native C++ component if CMake is available; otherwise it uses the **prebuilt native DLLs** included in the repository (`dist\Win7TaskbarCore.dll`, `dist\W7TInject.dll`);
* creates the **self-contained** package in `dist-package\` and the `Win7Taskbar-1.0.0-alpha-win-x64.zip` archive in the repository root;
* opens the folder containing the finished ZIP.

### Project method — official script

```powershell
pwsh -File "compilation files/publish.ps1" -Zip              # package + archive
pwsh -File "compilation files/publish.ps1" -Zip -SkipNative  # reuse the existing native DLLs
```

### Where the .NET part is located

All .NET/WPF code is under **`src/`**:

```
src/Win7Taskbar/Win7Taskbar.csproj      WPF application (the project to build)
src/RetroBar.Shim/RetroBar.Shim.csproj  theme compatibility shim
Win7Taskbar.sln                         Visual Studio solution containing both
```

With Visual Studio 2022 or Rider, you can open `Win7Taskbar.sln` and build it directly. No additional native DLL setup is required because the native DLLs are already included in the repository.

---

## 6. Frequently asked questions

**Do I need to install .NET?**
No. Releases are *self-contained*: the .NET runtime is included in the package. It can run on Windows without a separate .NET installation.

**Can I use the Windows taskbar together with this one?**
No. Win7Taskbar hides the original taskbar because it is intended to replace it.

**Are there risks?**
The program does not modify Windows system files. It creates its own AppBar window and hides the Windows taskbar. Closing it with *Close Win7Taskbar* restores the original taskbar. As with any software that changes the desktop shell experience, keeping a restore point is recommended.

**How do I report a problem?**
Open a GitHub issue with your Windows version, what you did, what you expected to happen, and the log output described in section 4.
