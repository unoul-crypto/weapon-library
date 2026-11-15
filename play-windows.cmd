@echo off
call "%~dp0scripts\build-windows.cmd" --run
if errorlevel 1 pause
