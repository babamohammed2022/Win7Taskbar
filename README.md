# Win7Taskbar

<img src="docs/icon-256.png" alt="Win7Taskbar" width="96" align="left" hspace="12" vspace="4">

A faithful re-creation of the **Windows 7 taskbar** running on Windows 10 / 11.

The look (glass, gradients, hover tiles, colors, metrics) comes from the original
Windows 7 theme; the behaviour (Superbar grouping, jump lists, notification area,
system flyouts, overflow panel, Properties dialog) is implemented by this project.

**Current version: `1.0.0-alpha`** — first public release of the v2.58 codebase
turned into a packaged, self-contained product. See
[Releases](https://github.com/babamohammed2022/Win7Taskbar/releases) for the
download and the notes of each version.

* **Logic backend:** native C++ / Win32, built as `Win7TaskbarCore.dll` and exposed
  through `extern "C"` entry points.
* **Frontend:** WPF / XAML (C#), which loads the existing `Themes/Windows7.xaml`
  theme without modifying it.
* **Glue:** P/Invoke between the two.

> **Win7Taskbar is built with .NET/WPF. The official releases are distributed as
> self-contained packages and do not require a separate .NET installation.**

* Italiano, guida rapida per l'uso e la compilazione: [`docs/GUIDA-RAPIDA-IT.md`](./docs/GUIDA-RAPIDA-IT.md)

**License: GNU GPL v3.0 or later** — see [`LICENSE`](./LICENSE) and
[`THIRD-PARTY-NOTICES.md`](./THIRD-PARTY-NOTICES.md) for attributions
(RetroBar, ExplorerPatcher, ManagedShell, Windows7RetrobarTheme).

---

## Download and run (no .NET needed)

1. Open the [Releases](https://github.com/babamohammed2022/Win7Taskbar/releases) page
   and download `Win7Taskbar-1.0.0-alpha-win-x64.zip`.
2. **Extract the whole folder** — keep `Themes\`, `Resources\` and `Languages\`
   next to `Win7Taskbar.exe`: the theme is read from disk at runtime.
3. Run `Win7Taskbar.exe`.
4. To close it: **right-click the clock → Properties → "Close Win7Taskbar"**.

Requirements: Windows 10 (21H2 or later) or Windows 11, **x64**. Nothing else:
the package is **self-contained**, so the .NET runtime travels inside it and the
.NET version installed on the machine is irrelevant (a machine with no .NET at
all works just the same).

Every setting lives in **Properties** (right-click the clock), in ten languages:
clock, flyout style per component (clock, network, volume, battery), app search,
language, notification area, toolbars and exit.

---

## Current status

> **Window thumbnails:** Temporarily disabled.
> **Tooltips:** Supported.
> Thumbnail previews will be reintroduced in a future version after the preview
> rendering system has been properly implemented.

Why: both preview implementations we shipped drew *unwanted rectangles* inside the
preview popup (an empty box, identical for every window). The live DWM thumbnail
(used up to v2.54) can fail silently — the registration succeeds and the compositor
then never paints anything, with no return value to check. The static `PrintWindow`
capture (v2.55) fails loudly, but on real hardware it still showed up as an empty
rectangle and made the popup feel frozen. Instead of shipping a half-broken feature,
the popup is not opened at all and hovering a task button shows only the app-name
tooltip, which is the part we can guarantee works.

The previous implementations are kept as a reference inside
`src/Win7Taskbar/Controls/TaskThumbnail.cs` (commented out, with notes on what a
future implementation has to prove first).

Win7Taskbar is a **project in constant development**: not every part is finished and
100% precision is not guaranteed yet. Some features are rebuilt from scratch, others
are hooked and repositioned. If you find a bug or have a suggestion, **reporting it is
useful** — it is what makes the next versions more faithful and more stable.

Known limitations: taskbar rotation is not supported (decorative option); system
windows (flyouts, Start menu) are hooked and repositioned, not recreated.

---

## Building from source

Full instructions: [`ISTRUZIONI_PROGETTO.md`](./ISTRUZIONI_PROGETTO.md)
(English) and [`docs/GUIDA-RAPIDA-IT.md`](./docs/GUIDA-RAPIDA-IT.md) (Italian).

### One click (Windows)

Double-click **`COMPILA.bat`** in the repository root. It takes care of everything:

* installs the .NET 8 SDK by itself if it is missing (per-user, no admin rights);
* builds the native C++ core if CMake is available, otherwise it uses the
  **prebuilt `dist/Win7TaskbarCore.dll` and `dist/W7TInject.dll` that ship with the
  repository** — so no Visual Studio / C++ toolchain is strictly required;
* publishes the **self-contained** package (the .NET runtime is inside the package);
* creates `Win7Taskbar-1.0.0-alpha-win-x64.zip` and opens the folder for you.

### The project's own script

```powershell
pwsh -File build/publish.ps1            # -> dist-package/  (add -Zip for the archive)
pwsh -File build/publish.ps1 -SkipNative    # reuse an existing dist/
```

### Manually, step by step

**The native DLL must be built first**, because the managed project copies it into
its output and fails with an explicit error if it is missing.

```bash
# 1) native backend  ->  dist/Win7TaskbarCore.dll
cd native
cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2

# 2) managed frontend, self-contained (no .NET needed on the target machine)
cd ..
dotnet publish src/Win7Taskbar/Win7Taskbar.csproj -c Release -r win-x64 \
      --self-contained true -o dist-package
```

`dotnet build Win7Taskbar.sln -c Release` stays the normal development loop: it
builds against the installed .NET and needs no publish step.

### Continuous integration

The release pipeline is [`ci/github-actions/release.yml`](./ci/github-actions/release.yml).
GitHub only reads workflows from `.github/workflows/`, so **copy that file to
`.github/workflows/release.yml`** once (instructions in
[`ci/github-actions/README.md`](./ci/github-actions/README.md)). It then:

* builds the native core with MSVC and runs the publish script;
* verifies the package (runtime files present, `includedFrameworks` in the runtime
  config), i.e. it fails if the package would ever need .NET installed;
* uploads the zip as a workflow artifact on every push;
* on a `v*` tag (for example `v1.0.0-alpha`) attaches the zip to a GitHub Release.

The archived, MediaFire-era build of the same code (`v2.58`) is intentionally **not**
part of this repository: the source here is the reference, and the packages are
built from it.

---

## Repository layout

```
Win7Taskbar/
├── README.md                       this file
├── LICENSE                         GNU GPL v3.0
├── CREDITS.txt                     detailed attributions + Apache 2.0 text
├── THIRD-PARTY-NOTICES.md          licences and attributions, file by file
├── ISTRUZIONI_PROGETTO.md          build / run / architecture notes (EN)
├── COMPILA.bat                     one-click build for Windows
├── Win7Taskbar.sln
│
├── build/
│   ├── publish.ps1                 self-contained release package (official script)
│   └── Compila-Release.ps1         friendly wrapper used by COMPILA.bat
│
├── ci/github-actions/release.yml   CI: package on push, release on a v* tag
├── .github/workflows/              (copy release.yml here once: see ci/.../README.md)
│
├── src/
│   ├── Win7Taskbar/                WPF application  (.NET part)
│   │   ├── Controls/               TaskThumbnail, Win7Calendar
│   │   ├── Models/                 task groups, notification area, clock, pins
│   │   ├── Utilities/              settings, theme loader, hooks, assets
│   │   ├── Converters/  Interop/   P/Invoke and native bridge
│   │   └── Themes/                 Windows7.xaml + Overrides.xaml
│   └── RetroBar.Shim/              compatibility assembly named "RetroBar"
│
├── native/                         native C++ core (Win32)
│   ├── CMakeLists.txt  cmake/  include/
│   ├── src/                        tray, flyouts, shell menus, properties, hooks
│   └── tools/                      COM interface check (runs after the build)
│
├── dist/                           prebuilt Win7TaskbarCore.dll + W7TInject.dll (x64)
├── Themes/ Resources/ Languages/   runtime assets of the package
├── docs/                           architecture decisions, icon, Italian quick guide
└── tools/                          optional helpers (see below)
```

The release archive contains the ready-to-run binaries at its root
(`Win7Taskbar.exe`, `Themes/`, `Resources/`, `Languages/`, the native DLLs), so the
same folder works both as the repository and as the runnable package.

### Optional helper

`tools/fetch_sources.py` downloads the archived MediaFire bundles (source zip and the
old `v2.58` binary build) if you ever need them for reference. The sources are
already in this repository, so it is **not** needed to build or run the project.

---

## Contributing

Bug reports, reproductions and pull requests are welcome — please open an issue.
Comments in the code are being translated to English to make the project easier to
work on; new code and documentation should be written in English.

What changed in each version is described in the release notes on the
[Releases](https://github.com/babamohammed2022/Win7Taskbar/releases) page (and, in
detail, in the commit history): there is no separate changelog file in the repository.

---

## Credits

Win7Taskbar builds on the work of others, in particular **RetroBar** (dremin),
**ExplorerPatcher** (valinet), **ManagedShell** (cairo shell) and
**Windows7RetrobarTheme** (babamohammed2022). See
[`CREDITS.txt`](./CREDITS.txt) and [`THIRD-PARTY-NOTICES.md`](./THIRD-PARTY-NOTICES.md)
for the full, file-by-file attribution.
