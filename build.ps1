# Builds the mod with the Visual Studio C++ tools (any edition or the Build Tools, found through vswhere).
#   .\build.ps1                    release build -> build\dinput8.dll
#   .\build.ps1 -Test              test build (extra exports) and tests\harness.exe -> build\test
#   .\build.ps1 -Install [-GameDir <folder>]   release build, then install.ps1
param([switch]$Test, [switch]$Install, [string]$GameDir)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { throw 'vswhere.exe not found: install Visual Studio or the Build Tools with "Desktop development with C++".' }
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'No Visual Studio installation with the C++ x86/x64 tools found.' }
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars32.bat'
$env:PATH = "$(Split-Path $vswhere);$env:PATH"  # vcvars needs vswhere

$out = Join-Path $root $(if ($Test) { 'build\test' } else { 'build' })
New-Item -ItemType Directory -Force $out | Out-Null
$flags = '/nologo /std:c++17 /MT /W3 /GS- /DWIN32_LEAN_AND_MEAN /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS'
$src = 'src\main.cpp src\hook.cpp src\patches.cpp'
$link = "/DEF:src\dinput8.def /OUT:`"$out\dinput8.dll`" /IMPLIB:`"$out\dinput8.lib`" user32.lib kernel32.lib"
if ($Test) {
    $commands = @(
        "cl $flags /O2 /DDH2013WS_TEST /Fo`"$out\\`" /LD $src /link $link",
        "cl $flags /O1 /Fo`"$out\\`" tests\harness.cpp /link /OUT:`"$out\harness.exe`" user32.lib"
    )
} else {
    $commands = @("cl $flags /O2 /Zi /Fo`"$out\\`" /Fd`"$out\vc.pdb`" /LD $src /link $link /PDB:`"$out\dinput8.pdb`" /DEBUG /OPT:REF /OPT:ICF")
}
$bat = Join-Path $out '_build.bat'
$lines = @('@echo off', "call `"$vcvars`" >nul || exit /b 1", "cd /d `"$root`"") + ($commands | ForEach-Object { "$_ || exit /b 1" })
Set-Content -Path $bat -Value $lines -Encoding Ascii
cmd /c "`"$bat`" 2>&1" | Where-Object { $_ -notmatch '^(main|hook|patches|harness)\.cpp$|Generating Code|Creating library' }
if ($LASTEXITCODE -ne 0) { throw "build failed ($LASTEXITCODE)" }
Write-Host "built $out\dinput8.dll"

if ($Install) {
    $installArgs = @{}
    if ($GameDir) { $installArgs.GameDir = $GameDir }
    & (Join-Path $root 'install.ps1') @installArgs
}
