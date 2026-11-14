@echo off
powershell -NoProfile -File "%~dp0scripts\build.ps1" -Demo -Run
if errorlevel 1 pause
