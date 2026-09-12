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
    pwsh -File build/publish.ps1
    pwsh -File build/publish.ps1 -Zip
    pwsh -File build/publish.ps1 -SkipNative          # reuse an existing dist/

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

$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$native = Join-Path $root 'native'
$dist = Join-Path $root 'dist'
$out = Join-Path $root $OutputDir
# 1.0.0-alpha: $IsWindows esiste solo in PowerShell 6+; su Windows
# PowerShell 5.1 (quello di serie su Windows 10/11) la sua lettura con
# Set-StrictMode fa fallire lo script. Il test sull'ambiente e' equivalente.
$isWindowsHost = [bool]$env:OS -and ($env:OS -eq 'Windows_NT')

function Step($text) { Write-Host "==> $text" -ForegroundColor Cyan }
function Fail($text) { Write-Host "!!  $text" -ForegroundColor Red; exit 1 }

# ---------------------------------------------------------------------------
# 1. native core
# ---------------------------------------------------------------------------
if (-not $SkipNative) {
    Step 'Native core: CMake configure'
    $buildDir = Join-Path $native 'build'
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

$coreDll = Join-Path $dist 'Win7TaskbarCore.dll'
if (-not (Test-Path $coreDll)) {
    Fail "dist/Win7TaskbarCore.dll not found: build the native core first (native/), or drop -SkipNative."
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
foreach ($required in @('Win7TaskbarCore.dll', 'System.Private.CoreLib.dll', 'PresentationFramework.dll', 'PresentationCore.dll', 'WindowsBase.dll')) {
    if (-not (Test-Path (Join-Path $out $required))) {
        Fail "$required is missing: the package is NOT self-contained"
    }
}
foreach ($folder in @('Themes', 'Resources', 'Languages')) {
    if (-not (Test-Path (Join-Path $out $folder))) { Fail "$folder\ is missing from the package" }
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
