# Builds the release and packs it for players: dist\dh2013ws-<version>.zip with dinput8.dll, dh2013ws.ini,
# install.bat / uninstall.bat / install.ps1, README, CHANGELOG and LICENSE. Also writes
# dist\release-notes.md (the newest CHANGELOG section) for the GitHub release.
# Usage: .\package.ps1 [-SkipBuild]
param([switch]$SkipBuild)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot

if (-not $SkipBuild) { & (Join-Path $root 'build.ps1') }
$header = Get-Content (Join-Path $root 'src\dh2013ws.h') -Raw
$version = [regex]::Match($header, 'DH2013WS_VERSION "([^"]+)"').Groups[1].Value
if (-not $version) { throw 'DH2013WS_VERSION not found in src\dh2013ws.h' }

$name = "dh2013ws-$version"
$dist = Join-Path $root 'dist'
$stage = Join-Path $dist $name
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Force $stage | Out-Null

Copy-Item (Join-Path $root 'build\dinput8.dll') $stage
# Text files with Windows line endings (the .bat files need them).
foreach ($f in 'dh2013ws.ini', 'install.ps1', 'install.bat', 'uninstall.bat', 'README.md', 'CHANGELOG.md', 'LICENSE') {
    $text = [IO.File]::ReadAllText((Join-Path $root $f)) -replace "`r?`n", "`r`n"
    [IO.File]::WriteAllText((Join-Path $stage $f), $text, (New-Object Text.UTF8Encoding($false)))
}

$zip = Join-Path $dist "$name.zip"
if (Test-Path $zip) { Remove-Item $zip }
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip

# Release notes: the CHANGELOG section of this version.
$changelog = Get-Content (Join-Path $root 'CHANGELOG.md') -Raw
$section = [regex]::Match($changelog, "(?ms)^## \[?$([regex]::Escape($version))\]?.*?(?=^## |\z)").Value
Set-Content -Path (Join-Path $dist 'release-notes.md') -Value $section.Trim() -Encoding UTF8

Write-Host "packed $zip"
