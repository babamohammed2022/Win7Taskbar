<#
.SYNOPSIS
    Builds the Win7Taskbar release package: self-contained, Windows x64.

.DESCRIPTION
    Produces a folder that runs on a Windows 10/11 x64 machine with no .NET
    installed at all:

        1. builds the native core (CMake)          -> dist/*.dll
        2. publishes the WPF app, self-contained    -> dist-package/
        3. copies the remaining native DLLs and the launcher note
        4. verifies that the package really is self-contained
        5. optionally zips it

    The Windows forms of the same steps are also in the GitHub workflow
    (.github/workflows/release.yml), which calls this script.

.EXAMPLE
    pwsh -File "compilation files/publish.ps1"
    pwsh -File "compilation files/publish.ps1" -Zip
    pwsh -File "compilation files/publish.ps1" -SkipNative          # reuse an existing dist/

.NOTES
    Requires: .NET 8 SDK, CMake + a C++ toolchain (MSVC, or MinGW-w64 when
    cross-compiling from Linux/macOS).
#>
[CmdletBinding()]
param(
    [ValidateSet('Release', 'Debug')]
    [string]$Configuration = 'Release',

    # Output folder, relative to the repository root.
    [string]$OutputDir = 'dist-package',

    # Also produce Win7Taskbar-<version>-win-x64.zip next to the output folder.
    [switch]$Zip,

    # Skip the native build and use whatever is already in dist/.
    [switch]$SkipNative,

    # Experimental: single-file executable. Not the default (see the csproj).
    [switch]$SingleFile
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# The script lives in 'compilation files/', one level below the repository
# root (it used to live in build/, two levels below).
$root = Split-Path -Parent $PSScriptRoot
$native = Join-Path $root 'native'
$buildDir = Join-Path $native 'build'
$dist = Join-Path $root 'dist'
$out = Join-Path $root $OutputDir
# 1.0.0-alpha: $IsWindows esiste solo in PowerShell 6+; su Windows
# PowerShell 5.1 (quello di serie su Windows 10/11) la sua lettura con
# Set-StrictMode fa fallire lo script. Il test sull'ambiente e' equivalente.
$isWindowsHost = [bool]$env:OS -and ($env:OS -eq 'Windows_NT')

function Step($text) { Write-Host "==> $text" -ForegroundColor Cyan }
# v1.21.18: "::error::" is a GitHub Actions workflow command: the runner turns
# the LINE into a check annotation, so a failed package says why on the run
# page instead of only "Process completed with exit code 1" (the job log itself
# is not always reachable, e.g. behind a network that blocks the log host).
function Fail($text) {
    Write-Host "!!  $text" -ForegroundColor Red
    Write-Output "::error::$text"
    exit 1
}
function Warn($text) {
    Write-Host "!!  $text" -ForegroundColor Yellow
    Write-Output "::warning::$text"
}

# v1.21.18: an unhandled error (a failed Copy-Item, a JSON that does not parse)
# leaves the run page with a bare "exit code 1" and nothing else. Report it as
# an annotation too, with the message PowerShell produced.
trap {
    $message = ($_ | Out-String).Trim()
    Write-Output "::error::publish.ps1 stopped: $message"
    exit 1
}

# ---------------------------------------------------------------------------
# 1. native core
# ---------------------------------------------------------------------------
if (-not $SkipNative) {
    Step 'Native core: CMake configure'
    # One argument per variable: building the string inline inside the array
    # literal (`'-D...' + $x`) makes PowerShell emit TWO elements and CMake then
    # configures with an empty build type - which silently ships an
    # unoptimised DLL. The cache is checked right after, exactly for that reason.
    $buildTypeArg = "-DCMAKE_BUILD_TYPE=$Configuration"
    $cmakeArgs = @('-S', $native, '-B', $buildDir, $buildTypeArg)
    if ($isWindowsHost) {
        $cmakeArgs += @('-A', 'x64')
    } else {
        # Cross-compiling from Linux/macOS with the toolchain shipped in the repo.
        $cmakeArgs += "-DCMAKE_TOOLCHAIN_FILE=$(Join-Path $native 'cmake/mingw-w64-x86_64.cmake')"
    }
    & cmake @cmakeArgs
    if ($LASTEXITCODE -ne 0) { Fail 'cmake configure failed' }

    if (-not $isWindowsHost) {
        $cache = Join-Path $buildDir 'CMakeCache.txt'
        $cached = (Select-String -Path $cache -Pattern '^CMAKE_BUILD_TYPE:STRING=(.*)$').Matches[0].Groups[1].Value
        if ($cached -ne $Configuration) {
            Fail "CMake configured with build type '$cached' instead of '$Configuration'"
        }
    }

    Step 'Native core: build'
    # -j2 keeps the peak memory of the C++ compiler under control on small CI
    # runners (parallel jobs there were killed by the OOM killer).
    & cmake --build $buildDir --config $Configuration -j 2
    if ($LASTEXITCODE -ne 0) { Fail 'native build failed' }
} else {
    Step 'Native core: skipped (using the existing dist/)'
}

# v1.21.18: the release workflow builds the native core with CMake and only
# then calls this script with -SkipNative, which used to mean "package whatever
# dist/ happens to contain". dist/ is a tracked folder holding a prebuilt DLL,
# so a package could carry a native core compiled from OLD sources while every
# managed-side fix in the package was current: exactly the "the fix is in the
# repo but not on my machine" report this project received three times. The
# CMake build tree is checked first now - that DLL cannot be older than the
# sources it was just compiled from - and dist/ stays the fallback for a local
# run without any build output.
if ($SkipNative) {
    # The outer @() keeps $builtCores an array even when nothing matches:
    # reading .Count on a $null under Set-StrictMode is not a safe bet.
    $builtCores = @(@(
        (Join-Path $buildDir "$Configuration/Win7TaskbarCore.dll"),
        (Join-Path $buildDir 'Win7TaskbarCore.dll')
    ) | Where-Object { Test-Path $_ } |
        Sort-Object { (Get-Item $_).LastWriteTimeUtc } -Descending)
    if ($builtCores.Count -gt 0) {
        $built = $builtCores[0]
        Step "Native core: using the CMake build output ($built)"
        Copy-Item $built (Join-Path $dist 'Win7TaskbarCore.dll') -Force
    }
    $builtInject = @(@(
        (Join-Path $buildDir "$Configuration/W7TInject.dll"),
        (Join-Path $buildDir 'W7TInject.dll')
    ) | Where-Object { Test-Path $_ } |
        Sort-Object { (Get-Item $_).LastWriteTimeUtc } -Descending)
    if ($builtInject.Count -gt 0) {
        Copy-Item $builtInject[0] (Join-Path $dist 'W7TInject.dll') -Force
    }
    $builtOverlayKill = @(@(
        (Join-Path $buildDir "$Configuration/W7TTrayOverlayKill.dll"),
        (Join-Path $buildDir 'W7TTrayOverlayKill.dll')
    ) | Where-Object { Test-Path $_ } |
        Sort-Object { (Get-Item $_).LastWriteTimeUtc } -Descending)
    if ($builtOverlayKill.Count -gt 0) {
        Copy-Item $builtOverlayKill[0] (Join-Path $dist 'W7TTrayOverlayKill.dll') -Force
    }
    $builtHelper = @(@(
        (Join-Path $buildDir "$Configuration/Win7StartHelper.exe"),
        (Join-Path $buildDir 'Win7StartHelper.exe')
    ) | Where-Object { Test-Path $_ } |
        Sort-Object { (Get-Item $_).LastWriteTimeUtc } -Descending)
    if ($builtHelper.Count -gt 0) {
        Copy-Item $builtHelper[0] (Join-Path $dist 'Win7StartHelper.exe') -Force
    }
}

$coreDll = Join-Path $dist 'Win7TaskbarCore.dll'
if (-not (Test-Path $coreDll)) {
    Fail "dist/Win7TaskbarCore.dll not found: build the native core first (native/), or drop -SkipNative."
}

# v1.21.18: -SkipNative means "reuse dist/", and a dist/ older than the native
# sources is exactly how a package ends up carrying a core without the last
# round of fixes. This script only WARNS (timestamps after a git checkout are
# not proof); the hard guarantee is the build stamp the release workflow looks
# for inside the packaged DLL.
if ($SkipNative) {
    $coreTime = (Get-Item $coreDll).LastWriteTimeUtc
    $newestSource = Get-ChildItem (Join-Path $native 'src') -File -Recurse -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
    if ($newestSource -and ($newestSource.LastWriteTimeUtc -gt $coreTime.AddMinutes(2))) {
        # Format first, warn after: mixing "+" and the format operator in one
        # expression depends on their precedence, which is not worth relying on.
        $staleText = ("dist/Win7TaskbarCore.dll ({0:u}) is older than native/src/{1} ({2:u}): " +
                      "the package may be carrying a stale native core - rebuild the native project") -f `
                     $coreTime, $newestSource.Name, $newestSource.LastWriteTimeUtc
        Warn $staleText
    } else {
        Write-Host "    dist/Win7TaskbarCore.dll is at least as new as native/src/" -ForegroundColor Green
    }
}

# ---------------------------------------------------------------------------
# 2. managed application, self-contained
# ---------------------------------------------------------------------------
Step 'Publishing the WPF application (self-contained, win-x64)'
$publishArgs = @(
    'publish', (Join-Path $root 'src/Win7Taskbar/Win7Taskbar.csproj'),
    '-c', $Configuration,
    '-r', 'win-x64',
    '--self-contained', 'true',
    '-o', $out
)
if ($SingleFile) { $publishArgs += '-p:PublishSingleFile=true' }
& dotnet @publishArgs
if ($LASTEXITCODE -ne 0) { Fail 'dotnet publish failed' }

# ---------------------------------------------------------------------------
# 3. files the build does not copy by itself
# ---------------------------------------------------------------------------
Step 'Copying the native DLLs into the package'
Copy-Item (Join-Path $dist 'Win7TaskbarCore.dll') $out -Force
$inject = Join-Path $dist 'W7TInject.dll'
if (Test-Path $inject) {
    Copy-Item $inject $out -Force
} else {
    Write-Host '    (W7TInject.dll not present in dist/: the frozen clock flyout will be skipped)' -ForegroundColor Yellow
}
$overlayKill = Join-Path $dist 'W7TTrayOverlayKill.dll'
if (Test-Path $overlayKill) {
    Copy-Item $overlayKill $out -Force
} else {
    Write-Host '    (W7TTrayOverlayKill.dll not present in dist/: Win11 XAML overlay kill will be skipped)' -ForegroundColor Yellow
}
$helper = Join-Path $dist 'Win7StartHelper.exe'
if (Test-Path $helper) {
    Copy-Item $helper $out -Force
} else {
    Fail 'dist/Win7StartHelper.exe not found: the Windows-key helper is required'
}

$readme = @'
Win7Taskbar - ready to run
==========================

1. Extract the whole folder (keep Themes\ , Resources\ and Languages\ next to
   Win7Taskbar.exe: the theme is read from disk at runtime).
2. Run Win7Taskbar.exe.
3. To close it: right-click the clock -> Properties -> "Close Win7Taskbar".

No .NET installation is required: this package contains its own runtime.
Requires Windows 10 or Windows 11, x64.
'@
Set-Content -Path (Join-Path $out 'LEGGIMI.txt') -Value $readme -Encoding UTF8

# ---------------------------------------------------------------------------
# 4. verification: the package must not need anything installed
# ---------------------------------------------------------------------------
Step 'Verifying the package'
$exe = Join-Path $out 'Win7Taskbar.exe'
if (-not (Test-Path $exe)) { Fail 'Win7Taskbar.exe is missing from the package' }
foreach ($required in @('Win7TaskbarCore.dll', 'Win7StartHelper.exe', 'System.Private.CoreLib.dll', 'PresentationFramework.dll', 'PresentationCore.dll', 'WindowsBase.dll')) {
    if (-not (Test-Path (Join-Path $out $required))) {
        Fail "$required is missing: the package is NOT self-contained"
    }
}
foreach ($folder in @('Themes', 'Resources', 'Languages')) {
    if (-not (Test-Path (Join-Path $out $folder))) { Fail "$folder\ is missing from the package" }
}

# The managed assembly must carry the embedded fallback copy of the native
# core: NativeCore restores Win7TaskbarCore.dll from it at startup when the
# file next to the executable is missing, different or unloadable (partial
# ZIP extraction, executable copied alone, antivirus quarantine). The name
# below is the LogicalName of the EmbeddedResource in Win7Taskbar.csproj and
# the constant in NativeCore.cs - keep the three places in sync. The check
# reads the manifest of the published assembly, so a frontend built without
# the embedded resource fails here instead of shipping a package that can no
# longer repair itself. Skipped for the experimental -SingleFile layout,
# where Win7Taskbar.dll lives inside the bundle (like the runtime DLLs the
# loop above already requires as loose files).
if (-not $SingleFile) {
    $managedAssembly = Join-Path $out 'Win7Taskbar.dll'
    if (-not (Test-Path $managedAssembly)) { Fail 'Win7Taskbar.dll is missing from the package' }
    $managedText = [System.Text.Encoding]::ASCII.GetString([System.IO.File]::ReadAllBytes($managedAssembly))
    if (-not $managedText.Contains('Win7Taskbar.Win7TaskbarCore.dll')) {
        Fail 'Win7Taskbar.dll does not embed the native-core fallback copy (see the EmbeddedResource items in Win7Taskbar.csproj): the package could not repair a missing Win7TaskbarCore.dll'
    }
    Write-Host '    embedded native-core fallback present in Win7Taskbar.dll' -ForegroundColor Green
}

# v1.21.18: the native core carries the revision it was compiled from (CMake
# stamp: -DW7T_BUILD_STAMP, or the checkout's HEAD when that is not given).
# If the checkout has a git revision, the packaged DLL must contain it: a
# package built from this source tree with a stale dist/Win7TaskbarCore.dll is
# exactly what made a whole round of fixes invisible to the tester, and it
# stops the release HERE instead of shipping.
$packedCore = Join-Path $out 'Win7TaskbarCore.dll'
$stampCandidates = @()
$headSha = $null
try {
    $headSha = (& git -C $root rev-parse HEAD 2>$null | Select-Object -First 1)
    if ($headSha) { $headSha = $headSha.Trim() }
} catch { $headSha = $null }
if ($headSha -and $headSha.Length -ge 7) {
    $stampCandidates += $headSha
    # A pull_request run checks out the MERGE commit (refs/pull/N/merge): its
    # second parent is the revision of the branch the package is built from.
    # Both describe "this source tree", so either stamp is accepted - otherwise
    # a perfectly fresh core would fail the check on a technicality. A tag or
    # main push has no second parent here and falls back to HEAD alone.
    try {
        $branchSha = (& git -C $root rev-parse "$headSha^2" 2>$null | Select-Object -First 1)
        if ($branchSha) {
            $branchSha = $branchSha.Trim()
            if ($branchSha.Length -ge 7 -and $stampCandidates -notcontains $branchSha) {
                $stampCandidates += $branchSha
            }
        }
    } catch { }
}
if ($stampCandidates.Count -gt 0) {
    $coreText = [System.Text.Encoding]::Unicode.GetString(
        [System.IO.File]::ReadAllBytes($packedCore))
    $matched = $null
    foreach ($candidate in $stampCandidates) {
        if ($coreText.Contains($candidate)) { $matched = $candidate; break }
    }
    if (-not $matched) {
        # Say WHICH revision the packaged DLL does carry: without it the only
        # visible symptom is "exit code 1" on the run page.
        $inside = @()
        foreach ($m in [regex]::Matches($coreText, '(?<![0-9a-fA-F])[0-9a-fA-F]{40}(?![0-9a-fA-F])')) {
            if ($inside.Count -ge 3) { break }
            if ($inside -notcontains $m.Value) { $inside += $m.Value }
        }
        $insideText = if ($inside.Count -gt 0) { $inside -join ', ' } else { 'none found' }
        Fail ("$packedCore does not contain the build stamp $($stampCandidates -join ' / '): " +
              "the package is carrying a native core that was not built from this revision " +
              "(revisions inside the packaged DLL: $insideText; rebuild it with " +
              "cmake --build native/build --config $Configuration)")
    }
    Write-Host "    native core build stamp verified: $matched" -ForegroundColor Green
} else {
    Warn "no git revision available: the build stamp of the packaged native core was not verified"
}
$runtimeConfig = Get-Content (Join-Path $out 'Win7Taskbar.runtimeconfig.json') -Raw | ConvertFrom-Json
$included = $runtimeConfig.runtimeOptions.includedFrameworks
if (-not $included) {
    Fail 'runtimeconfig.json has no includedFrameworks: this build would require .NET to be installed'
}
Write-Host ("    runtime included: " + (($included | ForEach-Object { $_.name + ' ' + $_.version }) -join ', '))

# ---------------------------------------------------------------------------
# 5. zip
# ---------------------------------------------------------------------------
if ($Zip) {
    $version = (Get-Content (Join-Path $root 'src/Win7Taskbar/Win7Taskbar.csproj') -Raw |
                Select-String -Pattern '<Version>([^<]+)</Version>').Matches[0].Groups[1].Value
    $zipPath = Join-Path $root ("Win7Taskbar-$version-win-x64.zip")
    Step "Creating $zipPath"
    if (Test-Path $zipPath) { Remove-Item $zipPath -Force }
    Compress-Archive -Path (Join-Path $out '*') -DestinationPath $zipPath
}

$size = [math]::Round((Get-ChildItem $out -Recurse | Measure-Object -Property Length -Sum).Sum / 1MB, 1)
Step "Done: $out ($size MB, self-contained, win-x64)"
