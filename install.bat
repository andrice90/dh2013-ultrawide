@echo off
rem Installs the widescreen mod. Extra arguments go to install.ps1 (e.g. -GameDir "D:\Games\Cabela's Dangerous Hunts Demo").
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1" %*
echo.
pause
