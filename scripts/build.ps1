param(
    [switch]$Test,
    [switch]$Demo,
    [switch]$Run,
    [string]$RaylibSource,
    [int]$Jobs = 4
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
if ($cmakeCommand) {
    $cmakeExe = $cmakeCommand.Source
} else {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path -LiteralPath $vswhere) {
        $vsPath = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($vsPath) {
            $cmakeExe = Join-Path $vsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
        }
    }
    if (-not $cmakeExe -or -not (Test-Path -LiteralPath $cmakeExe)) {
        throw 'Install CMake and a C++17 compiler, or Visual Studio with C++ and CMake tools.'
    }
}
$buildDir = Join-Path $projectRoot 'build/native'
$testOption = if ($Test) { 'ON' } else { 'OFF' }
$demoOption = if ($Demo -or $Run) { 'ON' } else { 'OFF' }
$configure = @('-S', $projectRoot, '-B', $buildDir, "-DWEAPON_LIBRARY_BUILD_TESTS=$testOption",
    '-DWEAPON_LIBRARY_BUILD_EXAMPLES=ON', "-DWEAPON_LIBRARY_BUILD_DEMO=$demoOption")
if ($RaylibSource) {
    $configure += "-DFETCHCONTENT_SOURCE_DIR_RAYLIB=$((Resolve-Path -LiteralPath $RaylibSource).Path)"
}
& $cmakeExe @configure
if ($LASTEXITCODE -ne 0) { throw 'Configuration failed.' }
& $cmakeExe --build $buildDir --config Release --parallel $Jobs
if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
if ($Test) {
    & (Join-Path (Split-Path -Parent $cmakeExe) 'ctest.exe') --test-dir $buildDir -C Release --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'Tests failed.' }
}
if ($Run) {
    $demoExe = Join-Path $buildDir 'Release/weapon_library_range.exe'
    if (-not (Test-Path -LiteralPath $demoExe)) { $demoExe = Join-Path $buildDir 'weapon_library_range.exe' }
    & $demoExe --visuals (Join-Path $projectRoot 'examples/range/visuals')
    if ($LASTEXITCODE -ne 0) { throw 'Demo failed.' }
}
