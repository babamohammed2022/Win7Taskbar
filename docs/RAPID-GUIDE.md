# Win7Taskbar — Rapid Guide (archived guide)

> **Archived guide.** Maintenance was discontinued due to lack of available time. The sole retained release is [Win7Taskbar v1.3.26-alpha](https://github.com/babamohammed2022/Win7Taskbar/releases/tag/v1.3.26-alpha); it remains incomplete relative to the project's initial objectives. This guide was originally written for v1.0.0-alpha and may not fully describe the retained release. No maintainer-led updates or support are planned.

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
| .NET installation | **No, not required**: the retained v1.3.26-alpha package includes the runtime |

---

## 3. Installation (end users)

1. Open the retained release page:
   **https://github.com/babamohammed2022/Win7Taskbar/releases/tag/v1.3.26-alpha**
2. Download `Win7Taskbar-1.3.26-alpha-win-x64.zip` (the .NET runtime is included).
3. **Extract the entire ZIP** to any folder (for example, `C:\Win7Taskbar`). Keep the `Themes`, `Resources`, and `Languages` subfolders next to `Win7Taskbar.exe`: the theme is loaded from disk at startup.
4. Run **`Win7Taskbar.exe`**.

### Verifying the downloaded file (optional)

If a package **SHA-256** hash is listed on the release page, verify it by opening PowerShell in the download folder and running:

```powershell
Get-FileHash .\Win7Taskbar-1.3.26-alpha-win-x64.zip -Algorithm SHA256
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

  The output shows whether the native component (`Win7TaskbarCore.dll`) was loaded. This archived project has no planned maintainer support or issue handling.

* **Icons or the theme are missing.** You may have extracted the ZIP into an extra nested folder without keeping `Themes\`, `Resources\`, and `Languages\` next to the executable. Extract the complete ZIP again using *Extract All*.

* **Antivirus / SmartScreen warning.** The program is not digitally signed. If Windows displays a warning, select *More info* → *Run anyway*. Source access is subject to the repository's access settings and license.

* **Feature limitations.** The retained alpha is incomplete. See [`FEATURE-STATUS.md`](./FEATURE-STATUS.md) for the historical feature record; it does not imply planned fixes or support.

---

## 5. Building from source (optional)

### Easiest method — double-click

1. Download the repository (**Code → Download ZIP**) and extract it, or run:
   `git clone https://github.com/babamohammed2022/Win7Taskbar.git`
2. Double-click **`build.bat`** inside the `compilation files/` folder.

The script:
* automatically installs the **.NET 8 SDK** if it is missing (without administrator rights);
* also builds the native C++ component if CMake is available; otherwise it uses the **prebuilt native DLLs** included in the repository (`dist\Win7TaskbarCore.dll`, `dist\W7TInject.dll`);
* creates the **self-contained** package in `dist-package\` and a ZIP archive whose versioned filename is determined by the archived source;
* opens the folder containing the finished ZIP.

### Packaging script retained in the archived source

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
No. The retained `v1.3.26-alpha` release is *self-contained*: the .NET runtime is included in the package.

**Can I use the Windows taskbar together with this one?**
No. Win7Taskbar hides the original taskbar because it is intended to replace it.

**Are there risks?**
The program does not modify Windows system files. It creates its own AppBar window and hides the Windows taskbar. Closing it with *Close Win7Taskbar* restores the original taskbar. As with any software that changes the desktop shell experience, keeping a restore point is recommended.

**How do I report a problem?**
This archived project does not provide maintainer issue handling or support. The source may be examined or developed independently by users with repository access, subject to its license.
