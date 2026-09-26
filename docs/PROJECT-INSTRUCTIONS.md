# Win7Taskbar - Project Instructions

Everything needed to build, run and understand the project.
For the user-facing overview, see [`README.md`](../README.md).

---

## 1. Requirements

**Windows (recommended):**

* Visual Studio 2022 with the *Desktop development with C++* workload (brings MSVC + CMake),
  **or** CMake >= 3.20 plus MinGW-w64 x86_64;
* [.NET 8 SDK](https://dotnet.microsoft.com/download/dotnet/8.0) x64.

**Linux (cross-compiling, used for the reference builds):**

```bash
sudo apt-get install -y cmake g++-mingw-w64-x86-64
curl -sSL https://dot.net/v1/dotnet-install.sh | bash -s -- --channel 8.0 --install-dir ~/.dotnet
export DOTNET_ROOT=$HOME/.dotnet
export PATH=$DOTNET_ROOT:$PATH
```

---

## 2. Build

### Step 1 - Native C++ DLL (must come first)

The managed project copies `dist/Win7TaskbarCore.dll` into its output and **stops the
build with an explicit error** if the file is missing.

Windows / Visual Studio:

```bat
cd native
cmake -B build -A x64 -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Linux or Windows with MinGW (the toolchain file is in the repository):

```bash
cd native
cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
```

> If the compiler is killed with `cc1plus: Killed`, the machine is short on RAM:
> rerun with `-j1`.

A `POST_BUILD` step copies the result to `dist/` at the repository root. Quick check:

```bash
ls -la dist/Win7TaskbarCore.dll
x86_64-w64-mingw32-objdump -p dist/Win7TaskbarCore.dll | grep -oE 'W7T_[A-Za-z]+' | sort -u | wc -l
```

The build also prints a summary of the COM interfaces it found in the binary
(`ok ...` lines) - a missing entry means a system flyout would not be reachable.

Libraries linked: `user32 gdi32 shell32 ole32 oleaut32 dwmapi propsys shlwapi advapi32`,
with `-static-libgcc -static-libstdc++ -static`.

### Step 2 - Managed application

From the repository root:

```bash
dotnet build Win7Taskbar.sln -c Release
# or:
dotnet publish src/Win7Taskbar/Win7Taskbar.csproj -c Release -r win-x64
```

The solution contains two projects, in dependency order:

1. `src/RetroBar.Shim` -> produces **`RetroBar.dll`**. The theme references types in the
   `RetroBar` namespace, so the assembly **must** have that name. Original code: only the
   public surfaces needed to resolve the theme bindings are replicated.
2. `src/Win7Taskbar` -> the WPF application.

### Step 3 - Distributable package (self-contained)

The release is **self-contained**: it carries its own .NET runtime, so it runs on
a Windows 10/11 x64 machine with no .NET installed. The script does the native
step, the publish, the copy of the remaining native DLLs and the verification:

```powershell
pwsh -File "compilation files/publish.ps1"
# add -Zip to also create the archive
```

Or by hand:

```bash
cd src/Win7Taskbar
dotnet publish -c Release -r win-x64 --self-contained true -p:DebugType=none -o ../../dist-package
```

The check that the package is really self-contained is not cosmetic: look for
`System.Private.CoreLib.dll` in the output folder, and for an
`includedFrameworks` section in `Win7Taskbar.runtimeconfig.json` - a
framework-dependent publish has neither.

Single-file publishing (`-p:PublishSingleFile=true`) is deliberately **not** the
default: the theme, `Resources/` and `Languages/` are read from the folder next
to the executable and the native core is loaded with a plain `DllImport`, so the
folder-shaped package is the predictable, debuggable one.

The same steps run in CI ([`.github/workflows/release.yml`](../.github/workflows/release.yml)): package as an
artifact on every push, release asset on a `v*` tag, with the self-contained check
in between.

The MSBuild target `CopyNativeCoreOnPublish` copies `dist/Win7TaskbarCore.dll` next to the
published binary and fails the build if step 1 was skipped.

### Native core self-repair

Because the same `dist/Win7TaskbarCore.dll` is embedded in `Win7Taskbar.dll`,
`Interop/NativeCore.cs` can restore it at startup: when the file next to the executable
is missing (partial ZIP extraction, executable copied alone, launch from inside the ZIP
viewer), belongs to another build, or cannot be loaded, the embedded copy is written back
(next to the executable, or `%LOCALAPPDATA%\Win7Taskbar\core` when that folder is not
writable) and preloaded with its full path, so every later `DllImport` resolves to it.
The optional `W7TInject.dll` and `W7TTrayOverlayKill.dll` are healed the same way; the native code loads them by bare
name, which resolves to an already-loaded module. `W7TTrayOverlayKill.dll` is injected into explorer.exe (`WH_CALLWNDPROC` + SEH) to hide the Win11 XAML tray overlay and to apply the work area from inside Explorer — this breaks the former out-of-process-only rule for those two jobs.

The embedded copy must always be the DLL that ships next to the executable: both come
from the same `dist/` file in the same build, so the pairing holds by construction. The
packaging script verifies the embedded resource is really present in the published
assembly. The resource names appear in three places that must stay in sync:
`Win7Taskbar.csproj` (`LogicalName`), `NativeCore.cs` (`EmbeddedCoreName`) and
`compilation files/publish.ps1` (package check). Set `W7T_PREFER_LOCAL_CORE=1` to make
the file next to the executable win even when it differs from the embedded copy (native
core development and debugging).

---

## 3. How the theme is loaded

`Themes/Windows7.xaml` (the copy under `src/Win7Taskbar/Themes/`, also mirrored at the
repository root) is loaded from disk at runtime. Image keys no longer use relative
`UriSource` paths: they resolve through `x:Static` members of
`Win7Taskbar.Utilities.GraphicalResourceBundle`, which holds the original PNG bytes as
Base64. See `docs/GRAPHICAL-RESOURCES.md`.

The theme still contains 16 styles whose `BasedOn` points at same-named keys expected in a
base dictionary, plus a few keys and strings it does not define. While building a key, WPF
deliberately skips the entry with the same name and does **not** fall back to sibling
dictionaries, which is where the `Cannot find resource named 'TaskbarWindow'` error came
from. `ThemeLoader.cs` therefore:

1. reads `Themes/Windows7.xaml` from disk as an `XDocument`;
2. inserts **in memory**, as the first child of the root, a
   `<ResourceDictionary.MergedDictionaries>` containing `Base.xaml` (from the shim, via
   pack URI);
3. serializes to a `MemoryStream` and calls `XamlReader.Load(stream, new ParserContext { BaseUri = <file path> })`.

The theme ships as `Content` (copied next to the executable) and is **not compiled into
BAML** — it is excluded from `Page`/`Resource` in the `.csproj`. The `Resources/` folder
next to the executable still ships the eight native Aero 9-slice PNGs required by the
C++/WIC frame renderer; WPF theme images do not come from that folder.

---

## 4. Architecture in one page

* **`native/`** owns everything that needs Win32: the notification area and its overflow
  panel, the system flyout launchers (clock, network, volume, battery), shell context
  menus, jump lists, AppBar registration, the app search window, pinned apps, the native
  Properties dialog and the COM interface checks.
* **`src/Win7Taskbar/`** owns the visible taskbar: the Superbar with grouped buttons,
  thumbnails/tooltips, the clock, the notification area model, the toolbars, settings and
  localisation (ten languages in `Languages/`).
* **`Interop/`** is the only place where the managed side talks to the native DLL: the
  `extern "C"` surface is mirrored in `NativeMethods.cs` / `NativeBridge.cs`, and events
  travel back through a queue that the WPF dispatcher drains.

Design notes and the reasoning behind the main decisions (why the taskbar is a WPF window
over a hidden native one, why the theme is loaded rather than compiled, why previews are
currently disabled, and so on) are in [`docs/architecture-decisions.md`](./architecture-decisions.md).

---

## 5. Startup, diagnostics and safe mode

* Settings are stored per user; the Properties dialog is the only supported way to change
  them.
* If the taskbar does not appear, run `Win7Taskbar.exe` from a terminal: the log lines on
  the console (`Win7Taskbar.deps.json` must sit next to the executable) tell whether the
  native core loaded.
* The clock's right-click menu contains the exit command; `Properties -> Close Win7Taskbar`
  is the intended way to stop the bar, so the AppBar registration is released cleanly.

---

## 6. Repository conventions

* **Release notes:** each version is described on the Releases page, in the order the
  items were requested, together with the reasoning behind each change (a measurement, a
  rejected approach, a reported bug). Nothing of that goes into the repository as a
  separate file.
* **Distribution:** releases are self-contained win-x64 packages built by
  `compilation files/publish.ps1` (called by the release workflow; `build/publish.ps1`
  stays as a compatibility shim because the workflow copy already published on GitHub
  calls the old path). Any change that adds a file the
  application needs at runtime must keep that file inside the package (or next to the
  executable): the target machine has no .NET and downloads nothing else.
* **Code comments:** English for anything new. Historical Italian comments may be
  translated file by file; the technical notes they carry are worth preserving when
  still relevant.
* **One change at a time:** layout and behaviour changes are made so that each one can be
  tested on its own, because most of the visible behaviour can only be verified on a real
  Windows desktop.

## 7. Provenance

Win7Taskbar is a clean-room reconstruction. The bar's behaviour was
re-established by observing the behaviour of Windows 7 on hardware and by
using exclusively Microsoft's documented public APIs (Win32, Shell COM);
the analysis notes that supported those observations live in `docs/` and
are separated from the code by design. No line of Windows source code, no
binary offsets, addresses, symbol tables, vtables or decompiled artifacts
are included in this repository, and none are needed to build or run it:
every behaviour the bar performs is one a documented public API performs
for it. Historical notes that describe what was *observed* (and what was
inferred from it) keep that distinction explicit and are identified as
evidence, not as licensed material.
