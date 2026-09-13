<#
.SYNOPSIS
    Compatibility shim: forwards to "compilation files/publish.ps1".

.DESCRIPTION
    The official packaging script moved from build/publish.ps1 to
    "compilation files/publish.ps1" (all the manual build/packaging scripts now
    live in one folder). This file stays behind so that anything still calling
    the old path keeps working: in particular the copy of the release workflow
    that GitHub already has in .github/workflows/, which cannot be updated by
    the automation and is refreshed by hand.

    New references should call the moved script directly:

        pwsh -File "compilation files/publish.ps1" -SkipNative -OutputDir dist-package

.EXAMPLE
    pwsh -File build/publish.ps1 -Zip
    pwsh -File build/publish.ps1 -SkipNative -OutputDir dist-package
#>
[CmdletBinding()]
param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$ForwardArgs
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$target = Join-Path $root 'compilation files/publish.ps1'
if (-not (Test-Path $target)) {
    Write-Host "!!  $target not found: the packaging script has been moved or removed." -ForegroundColor Red
    exit 1
}

Write-Host "==> build/publish.ps1 is a compatibility shim: running 'compilation files/publish.ps1'" -ForegroundColor Yellow

# The arguments are handed to a new instance of the current shell on purpose.
# Forwarding them with "& $target @ForwardArgs" would pass "-SkipNative" as a
# positional string and the real script would refuse it; handing them to a
# native command keeps the "-Name value" shape intact, whatever the caller used.
$shell = (Get-Process -Id $PID).Path
& $shell -NoProfile -ExecutionPolicy Bypass -File $target @ForwardArgs
exit $LASTEXITCODE
