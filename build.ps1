# Builds build\dinput8.dll (Win32/x86, static CRT). Usage: .\build.ps1 [-Install]
#   -Install  copies dinput8.dll (and dh2013ws.ini if missing) into the game folder.
param([switch]$Install, [string]$GameDir = "D:\SteamLibrary\steamapps\common\Cabela's Dangerous Hunts Demo")
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$out = Join-Path $root 'build'
New-Item -ItemType Directory -Force $out | Out-Null
$vcvars = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat"
if (-not (Test-Path $vcvars)) { throw "vcvars32.bat not found: $vcvars" }
$env:PATH = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer;$env:PATH"  # vcvars needs vswhere
$bat = Join-Path $out '_build.bat'
@"
@echo off
call "$vcvars" >nul || exit /b 1
cd /d "$root"
cl /nologo /std:c++17 /O2 /MT /W3 /GS- /Zi /DWIN32_LEAN_AND_MEAN /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS ^
   /Fo"$out\\" /Fd"$out\vc.pdb" /LD src\main.cpp src\hook.cpp src\patches.cpp ^
   /link /DEF:src\dinput8.def /OUT:"$out\dinput8.dll" /IMPLIB:"$out\dinput8.lib" /PDB:"$out\dinput8.pdb" /DEBUG /OPT:REF /OPT:ICF user32.lib kernel32.lib
"@ | Set-Content -Encoding ascii $bat
cmd /c "`"$bat`" 2>&1"
if ($LASTEXITCODE -ne 0) { throw "build failed ($LASTEXITCODE)" }
Write-Host "built $out\dinput8.dll"
if ($Install) {
    Copy-Item "$out\dinput8.dll" $GameDir -Force
    if (-not (Test-Path "$GameDir\dh2013ws.ini")) { Copy-Item "$root\dh2013ws.ini" $GameDir }
    Write-Host "installed into $GameDir"
}
