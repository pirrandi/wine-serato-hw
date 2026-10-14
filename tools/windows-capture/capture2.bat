@echo off
rem Runs capture2.ps1 elevated. Right click -> Run as administrator.
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0capture2.ps1"
pause
