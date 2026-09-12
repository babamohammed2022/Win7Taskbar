<#
.SYNOPSIS
    Scarica i bundle MediaFire di Win7Taskbar e li mette dentro il repository.

.DESCRIPTION
    Script "un clic" per Windows: non richiede Python, non richiede il .NET SDK.
    Usa solo PowerShell 5.1+ (presente di serie su Windows 10/11).

    - scarica i due bundle pubblicati su MediaFire;
    - verifica che siano archivi ZIP validi;
    - li estrae in una cartella di lavoro (default: _incoming);
    - opzionalmente committa e pusha tutto sul branch corrente.

.EXAMPLE
    # Scarica ed estrae soltanto
    powershell -ExecutionPolicy Bypass -File tools\Get-Sources.ps1 -Extract

.EXAMPLE
    # Scarica, estrae, committa e pusha sul branch corrente
    powershell -ExecutionPolicy Bypass -File tools\Get-Sources.ps1 -Extract -CommitPush

.EXAMPLE
    # Push verso un branch specifico
    powershell -ExecutionPolicy Bypass -File tools\Get-Sources.ps1 -Extract -CommitPush -Branch arena/01a0966f-win7taskbar
#>
[CmdletBinding()]
param(
    [string]$Out = "_incoming",
    [switch]$Extract,
    [switch]$ExtractRoot,
    [switch]$CommitPush,
    [string]$Branch = "",
    [string]$Message = "feat: import Win7Taskbar sources"
)

$ErrorActionPreference = "Stop"
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$UserAgent = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36"

$Sources = [ordered]@{
    "win7taskbarforgithub.zip"       = "https://www.mediafire.com/file/erka1phr202p1nz/win7taskbarforgithub.zip/file"
    "Win7Taskbar-v2.58-win-x64.zip"  = "https://www.mediafire.com/file/wyfsjmgwvg0bohc/Win7Taskbar-v2.58-win-x64.zip/file"
}

function Write-Step([string]$Text) {
    Write-Host "[fetch-sources] $Text" -ForegroundColor Cyan
}

function Get-DirectLink([string]$PageUrl) {
    $headers = @{ "User-Agent" = $UserAgent; "Referer" = "https://www.mediafire.com/"; "Accept-Language" = "en-US,en;q=0.9" }
    $html = (Invoke-WebRequest -Uri $PageUrl -Headers $headers -UseBasicParsing -TimeoutSec 60).Content

    $match = [regex]::Match($html, 'id="downloadButton"[^>]*href="([^"]+)"', 'IgnoreCase')
    if ($match.Success) { return $match.Groups[1].Value }

    $match = [regex]::Match($html, 'https://download[0-9]*\.mediafire\.com/[^"''<>\s]+')
    if ($match.Success) { return $match.Value }

    $match = [regex]::Match($html, 'href="(https://www\.mediafire\.com/[^"]*(?:dkey=|download)[^"]*)"')
    if ($match.Success) { return $match.Groups[1].Value }

    return $null
}

function Save-MediaFireFile([string]$PageUrl, [string]$Destination) {
    if ((Test-Path $Destination) -and ((Get-Item $Destination).Length -gt 1024)) {
        Write-Step "$(Split-Path $Destination -Leaf) gia' presente, salto."
        return
    }

    Write-Step "pagina MediaFire: $PageUrl"
    $link = Get-DirectLink $PageUrl
    if (-not $link) { throw "Link di download non trovato per $PageUrl" }
    Write-Step "link diretto: $link"

    $headers = @{ "User-Agent" = $UserAgent; "Referer" = $PageUrl }
    Invoke-WebRequest -Uri $link -Headers $headers -OutFile $Destination -UseBasicParsing -TimeoutSec 900
    $size = [math]::Round((Get-Item $Destination).Length / 1MB, 2)
    Write-Step "scaricato $Destination ($size MB)"
}

$destination = Join-Path (Get-Location) $Out
New-Item -ItemType Directory -Force -Path $destination | Out-Null

foreach ($entry in $Sources.GetEnumerator()) {
    $archive = Join-Path $destination $entry.Key
    Save-MediaFireFile -PageUrl $entry.Value -Destination $archive

    if ($Extract) {
        $target = if ($ExtractRoot) { $destination } else { Join-Path $destination ([IO.Path]::GetFileNameWithoutExtension($entry.Key)) }
        if (Test-Path $target) { Remove-Item -Recurse -Force $target }
        Write-Step "estraggo in $target"
        Expand-Archive -Path $archive -DestinationPath $target -Force

        # Se c'e' una sola cartella radice, promuoviamo il contenuto.
        $entries = @(Get-ChildItem -Force $target | Where-Object { $_.Name -ne "__MACOSX" })
        if ($entries.Count -eq 1 -and $entries[0].PSIsContainer) {
            Write-Step "rimuovo la cartella radice ridondante '$($entries[0].Name)'"
            Get-ChildItem -Force $entries[0].FullName | ForEach-Object { Move-Item $_.FullName (Join-Path $target $_.Name) -Force }
            Remove-Item -Force $entries[0].FullName
        }
    }
}

Write-Step "contenuto di ${destination}:"
Get-ChildItem -Force $destination | ForEach-Object { Write-Step "  - $($_.Name)" }

if ($CommitPush) {
    if (-not (Get-Command git -ErrorAction SilentlyContinue)) { throw "git non trovato nel PATH" }
    Write-Step "git add"
    git add -A $Out
    Get-ChildItem -Recurse -File $destination |
        Where-Object { @(".dll", ".lib", ".exe", ".pdb", ".ico", ".png", ".res", ".rc") -contains $_.Extension.ToLower() } |
        ForEach-Object { git add -f $_.FullName }
    if (git status --porcelain $Out) {
        git commit -m $Message
        if ($Branch) { git push origin "HEAD:$Branch" } else { git push }
        Write-Step "push completato"
    } else {
        Write-Step "nessuna modifica da committare"
    }
}

Write-Step "fatto."
