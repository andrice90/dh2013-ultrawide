# Offline test: builds the test variant and tests\harness.exe, then runs the harness against the
# CloakNTEngine.dll of every installed copy of the game (demo and/or full game; the DLL is mapped, never
# started, no window). Usage: .\test.ps1 [-GameDir <folder>]
param([string]$GameDir)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$dirs = if ($GameDir) { @($GameDir) } else { @(& (Join-Path $root 'install.ps1') -Locate) }
if (-not $dirs) { throw 'No game found; pass -GameDir.' }

& (Join-Path $root 'build.ps1') -Test
$out = Join-Path $root 'build\test'
# Test settings: force 3440x1440 so the Renderer::Init hook is installed too.
(Get-Content (Join-Path $root 'dh2013ws.ini')) -replace '^Width=.*', 'Width=3440' -replace '^Height=.*', 'Height=1440' |
    Set-Content -Encoding Ascii (Join-Path $out 'dh2013ws.ini')

$failed = 0
foreach ($dir in $dirs) {
    # Once at the engine's own base, once relocated (proves nothing depends on fixed addresses).
    foreach ($mode in @(), @('--rebase')) {
        Write-Host ''
        Push-Location $out
        try { & .\harness.exe (Join-Path $dir 'CloakNTEngine.dll') @mode; if ($LASTEXITCODE -ne 0) { $failed++ } } finally { Pop-Location }
        Write-Host '--- dh2013ws.log'
        Get-Content (Join-Path $out 'dh2013ws.log') | Where-Object { $_ -notmatch ' listed$' }
    }
}
if ($failed) { Write-Host "$failed harness run(s) failed" -ForegroundColor Red } else { Write-Host 'all harness runs passed' -ForegroundColor Green }
exit $failed
