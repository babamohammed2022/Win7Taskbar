<#
.SYNOPSIS
    Compila Win7Taskbar e crea il pacchetto per Windows 10/11 x64 (self-contained).

.DESCRIPTION
    Pensato per chi non ha mai compilato nulla: fa tutto in automatico.

      1. cerca il .NET SDK e, se non c'e', lo installa da solo (senza diritti di
         amministratore, nella cartella dell'utente);
      2. se CMake e' disponibile compila anche la parte nativa C++; altrimenti
         usa la DLL nativa gia' inclusa nel repository (dist\Win7TaskbarCore.dll),
         quindi la build riesce anche senza Visual Studio;
      3. chiama lo script ufficiale del progetto (build/publish.ps1) in modalita'
         self-contained, cioe' con il runtime .NET incluso nel pacchetto:
         l'utente finale non dovra' installare nessuna versione di .NET;
      4. crea l'archivio Win7Taskbar-<versione>-win-x64.zip.

    Per avviarlo basta fare doppio clic su COMPILA.bat nella cartella principale
    del repository.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File build\Compila-Release.ps1
.EXAMPLE
    powershell -ExecutionPolicy Bypass -File build\Compila-Release.ps1 -SkipNative -NoZip
#>
[CmdletBinding()]
param(
    # Non compilare la parte nativa C++ (usa dist\Win7TaskbarCore.dll).
    [switch]$SkipNative,

    # Non creare l'archivio .zip finale.
    [switch]$NoZip,

    # Configurazione di compilazione.
    [ValidateSet('Release', 'Debug')]
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

function Step($text) { Write-Host "==> $text" -ForegroundColor Cyan }
function Warn($text) { Write-Host "!!  $text" -ForegroundColor Yellow }
function Fail($text) { Write-Host "!!  $text" -ForegroundColor Red; exit 1 }

# ---------------------------------------------------------------------------
# 1. .NET SDK (installazione automatica se manca)
# ---------------------------------------------------------------------------
function Get-DotNetCommand {
    $cmd = Get-Command dotnet -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    return $null
}

function Get-SdkList {
    param([string]$DotNet)
    try {
        $list = & $DotNet --list-sdks 2>$null
        if ($list) { return @($list) }
    } catch { }
    return @()
}

function Install-DotNetSdk {
    Step 'Il .NET SDK non e'' installato: lo scarico e lo installo per l''utente corrente (nessun diritto di amministratore).'
    $dir = Join-Path $env:LOCALAPPDATA 'Microsoft\dotnet'
    $installer = Join-Path $env:TEMP 'dotnet-install.ps1'

    if (-not (Test-Path $installer)) {
        Step "Scarico l'installer ufficiale di .NET (dotnet-install.ps1)"
        Invoke-WebRequest -Uri 'https://dot.net/v1/dotnet-install.ps1' -OutFile $installer -UseBasicParsing
    }

    Step "Installo il .NET 8 SDK in $dir (qualche minuto, serve la connessione a Internet)"
    & $installer -Channel 8.0 -InstallDir $dir
    if ($LASTEXITCODE -ne 0) {
        Fail 'Installazione del .NET SDK non riuscita. Scaricalo a mano da https://dotnet.microsoft.com/download/dotnet/8.0 e rilancia questo script.'
    }

    # Rende il SDK visibile per questa sessione (e quindi al processo figlio
    # che esegue build\publish.ps1): niente modifiche permanenti al sistema.
    $env:PATH = "$dir;$env:PATH"
}

$dotnet = Get-DotNetCommand
$sdks = @()
if ($dotnet) { $sdks = Get-SdkList -DotNet $dotnet }

if ((-not $dotnet) -or ($sdks.Count -eq 0)) {
    Install-DotNetSdk
    $dotnet = Get-DotNetCommand
    if (-not $dotnet) {
        $local = Join-Path $env:LOCALAPPDATA 'Microsoft\dotnet\dotnet.exe'
        if (Test-Path $local) { $dotnet = $local }
    }
    if (-not $dotnet) { Fail 'Il .NET SDK non risulta disponibile nemmeno dopo l''installazione.' }
}
Step ".NET SDK: $dotnet  ($($sdks -join ' | '))"

# ---------------------------------------------------------------------------
# 2. Parte nativa: compilarla se possibile, altrimenti usare la DLL inclusa
# ---------------------------------------------------------------------------
$coreDll = Join-Path $root 'dist\Win7TaskbarCore.dll'
$useExistingNative = [bool]$SkipNative

if ($SkipNative) {
    Step 'Parte nativa C++: saltata su richiesta (-SkipNative)'
} else {
    $cmake = Get-Command cmake -ErrorAction SilentlyContinue
    if ($cmake) {
        Step "CMake trovato ($($cmake.Source)): compilo anche la parte nativa C++"
    } elseif (Test-Path $coreDll) {
        Warn 'CMake non trovato: uso la DLL nativa gia'' inclusa nel repository (dist\Win7TaskbarCore.dll).'
        Warn 'Va benissimo per compilare e usare l''applicazione; per ricompilare il C++ serve CMake.'
        $useExistingNative = $true
    } else {
        Fail ('Manca sia CMake sia dist\Win7TaskbarCore.dll. Installa "Desktop development with C++" ' +
              'da Visual Studio Installer, oppure CMake + MinGW-w64, e riprova.')
    }
}

# ---------------------------------------------------------------------------
# 3. Publish self-contained tramite lo script ufficiale del progetto
# ---------------------------------------------------------------------------
$publish = Join-Path $PSScriptRoot 'publish.ps1'
if (-not (Test-Path $publish)) { Fail "Script non trovato: $publish" }

$psArgs = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $publish, '-Configuration', $Configuration)
if ($useExistingNative) { $psArgs += '-SkipNative' }
if (-not $NoZip) { $psArgs += '-Zip' }

$hostExe = (Get-Process -Id $PID).Path
if (-not $hostExe) { $hostExe = 'powershell.exe' }

Step 'Compilo l''applicazione in modalita'' self-contained (runtime .NET incluso nel pacchetto)'
& $hostExe @psArgs
if ($LASTEXITCODE -ne 0) { Fail 'Compilazione non riuscita (vedi i messaggi sopra).' }

# ---------------------------------------------------------------------------
# 4. Riepilogo
# ---------------------------------------------------------------------------
$version = (Get-Content (Join-Path $root 'src\Win7Taskbar\Win7Taskbar.csproj') -Raw |
            Select-String -Pattern '<Version>([^<]+)</Version>').Matches[0].Groups[1].Value
$package = Join-Path $root 'dist-package'
$zip = Join-Path $root ("Win7Taskbar-$version-win-x64.zip")

Write-Host ''
Write-Host '============================================================' -ForegroundColor Green
Write-Host "  Build completata - versione $version" -ForegroundColor Green
Write-Host '============================================================' -ForegroundColor Green
Write-Host "  Cartella pronta all'uso : $package"
if (Test-Path $zip) { Write-Host "  Archivio da distribuire : $zip" }
Write-Host ''
Write-Host '  Come si usa: apri la cartella dist-package\ (oppure scompatta lo' -ForegroundColor Gray
Write-Host '  zip) e avvia Win7Taskbar.exe. Per chiudere: tasto destro' -ForegroundColor Gray
Write-Host '  sull''orologio -> Proprieta'' -> "Close Win7Taskbar".' -ForegroundColor Gray
Write-Host '  Non serve installare .NET: il runtime e'' dentro il pacchetto.' -ForegroundColor Gray
Write-Host ''

if (Test-Path $zip) {
    if (Get-Command explorer.exe -ErrorAction SilentlyContinue) {
        Start-Process explorer.exe -ArgumentList ('/select,"' + $zip + '"') | Out-Null
    }
}
