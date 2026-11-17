@echo off
setlocal
set "RANGE_ROOT=%~dp0.."
set "RANGE_CMAKE="
for /f "delims=" %%C in ('where cmake 2^>nul') do if not defined RANGE_CMAKE set "RANGE_CMAKE=%%C"
if defined RANGE_CMAKE goto configure

set "RANGE_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%RANGE_VSWHERE%" goto missing_tools
for /f "usebackq delims=" %%V in (`"%RANGE_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "RANGE_CMAKE=%%V\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not defined RANGE_CMAKE goto missing_tools
if not exist "%RANGE_CMAKE%" goto missing_tools

:configure
"%RANGE_CMAKE%" -S "%RANGE_ROOT%" -B "%RANGE_ROOT%\build\native" -DWEAPON_LIBRARY_BUILD_DEMO=ON -DWEAPON_LIBRARY_BUILD_BALLISTICS=ON
if errorlevel 1 exit /b 1
"%RANGE_CMAKE%" --build "%RANGE_ROOT%\build\native" --config Release --parallel 4
if errorlevel 1 exit /b 1
if /I not "%~1"=="--run" exit /b 0

set "RANGE_EXE=%RANGE_ROOT%\build\native\Release\weapon_library_range.exe"
if not exist "%RANGE_EXE%" set "RANGE_EXE=%RANGE_ROOT%\build\native\weapon_library_range.exe"
if not exist "%RANGE_EXE%" exit /b 1
start "" /D "%RANGE_ROOT%" "%RANGE_EXE%" --visuals "%RANGE_ROOT%\examples\range\visuals"
exit /b 0

:missing_tools
echo Install CMake and a C++17 toolchain, or Visual Studio with C++ and CMake tools.
exit /b 1
