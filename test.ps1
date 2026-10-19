# Offline test: builds the test variant of dinput8.dll + tests\harness.exe into build\test and runs the harness
# against the game's CloakNTEngine.dll (mapped read-only, never started; no window). Usage: .\test.ps1
param([string]$GameDir = "D:\SteamLibrary\steamapps\common\Cabela's Dangerous Hunts Demo")
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$out = Join-Path $root 'build\test'
New-Item -ItemType Directory -Force $out | Out-Null
$vcvars = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat"
$env:PATH = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer;$env:PATH"
$bat = Join-Path $out '_build.bat'
@"
@echo off
call "$vcvars" >nul || exit /b 1
cd /d "$root"
cl /nologo /std:c++17 /O2 /MT /W3 /GS- /DDH2013WS_TEST /DWIN32_LEAN_AND_MEAN /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS ^
   /Fo"$out\\" /LD src\main.cpp src\hook.cpp src\patches.cpp ^
   /link /DEF:src\dinput8.def /OUT:"$out\dinput8.dll" /IMPLIB:"$out\dinput8.lib" user32.lib kernel32.lib || exit /b 1
cl /nologo /std:c++17 /O1 /MT /W3 /Fo"$out\\" tests\harness.cpp /link /OUT:"$out\harness.exe" user32.lib || exit /b 1
"@ | Set-Content -Encoding ascii $bat
cmd /c "`"$bat`" 2>&1" | Where-Object { $_ -notmatch '^(main|hook|patches|harness)\.cpp$|Generating Code|Creating library' }
if ($LASTEXITCODE -ne 0) { throw "build failed" }
# Test settings: force 3440x1440 so the Renderer::Init hook is installed too.
(Get-Content "$root\dh2013ws.ini") -replace '^Width=.*', 'Width=3440' -replace '^Height=.*', 'Height=1440' |
    Set-Content -Encoding ascii "$out\dh2013ws.ini"
Push-Location $out
try { & .\harness.exe (Join-Path $GameDir 'CloakNTEngine.dll'); $code = $LASTEXITCODE } finally { Pop-Location }
Write-Host "--- dh2013ws.log"; Get-Content "$out\dh2013ws.log"
exit $code
