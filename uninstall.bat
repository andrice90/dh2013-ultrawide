@echo off
rem Removes the widescreen mod. Add -KeepConfig to keep dh2013ws.ini.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1" -Uninstall %*
echo.
pause
