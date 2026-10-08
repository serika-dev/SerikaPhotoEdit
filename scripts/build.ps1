param([string]$QtRoot = "", [switch]$Package, [switch]$Test, [string[]]$ConfigureArgs = @())
$ErrorActionPreference = 'Stop'
$workspace = Split-Path -Parent $PSScriptRoot
if (-not $QtRoot) {
    $bundledQt = Join-Path $workspace '.tools/Qt/6.8.3/mingw_64'
    $QtRoot = if (Test-Path -LiteralPath $bundledQt) { $bundledQt } else { $env:QT_ROOT_DIR }
}
if (-not $QtRoot -or -not (Test-Path -LiteralPath "$QtRoot/lib/cmake/Qt6/Qt6Config.cmake")) {
    throw 'Provide -QtRoot pointing to a Qt 6.8 or newer desktop kit, or set QT_ROOT_DIR.'
}
function Resolve-BuildTool([string]$Bundled, [string]$Name) {
    if (Test-Path -LiteralPath $Bundled) { return $Bundled }
    $command = Get-Command $Name -CommandType Application -ErrorAction SilentlyContinue
    if (-not $command) { throw "Required build tool '$Name' was not found in the bundled tools or PATH." }
    return $command.Source
}
$compilerRoot = Join-Path $workspace '.tools/Qt/Tools/mingw1310_64/bin'
$cmakeBin = Join-Path $workspace '.tools/python/cmake/data/bin'
$cmakeExe = Resolve-BuildTool "$cmakeBin/cmake.exe" 'cmake'
$ctestExe = Resolve-BuildTool "$cmakeBin/ctest.exe" 'ctest'
$ninjaExe = Resolve-BuildTool (Join-Path $workspace '.tools/python/bin/ninja.exe') 'ninja'
$deps = Join-Path $workspace '.tools/deps/install'
$env:PATH = "$QtRoot/bin;$cmakeBin;$workspace/.tools/wix;$env:PATH"
$prefix = $QtRoot
$configure = @('-S', $workspace, '-B', "$workspace/build", '-G', 'Ninja', "-DCMAKE_MAKE_PROGRAM=$ninjaExe", '-DCMAKE_BUILD_TYPE=Release')
# The bundled dependencies use the MinGW ABI. An MSVC kit must use its own dependencies
# and be launched from a Visual Studio developer shell.
if ($QtRoot -match 'mingw') {
    $env:PATH = "$compilerRoot;$deps/bin;$env:PATH"
    $prefix += ";$deps"
    if (Test-Path -LiteralPath "$compilerRoot/g++.exe") { $configure += "-DCMAKE_CXX_COMPILER=$compilerRoot/g++.exe" }
    if (Test-Path -LiteralPath "$deps/lib/libraw.dll.a") { $configure += "-DLIBRAW_INCLUDE_DIR=$deps/include", "-DLIBRAW_LIBRARY=$deps/lib/libraw.dll.a" }
    if (Test-Path -LiteralPath "$deps/lib/libzlib.dll.a") { $configure += "-DZLIB_INCLUDE_DIR=$deps/include", "-DZLIB_LIBRARY=$deps/lib/libzlib.dll.a" }
}
$configure += "-DCMAKE_PREFIX_PATH=$prefix"
$configure += $ConfigureArgs
& $cmakeExe @configure
if ($LASTEXITCODE) { throw 'Configuration failed' }
& $cmakeExe --build "$workspace/build" --parallel 6
if ($LASTEXITCODE) { throw 'Build failed' }
if ($Test) { & $ctestExe --test-dir "$workspace/build" --output-on-failure; if ($LASTEXITCODE) { throw 'Tests failed' } }
if ($Package) { & (Join-Path $PSScriptRoot 'package-windows.ps1') -QtRoot $QtRoot }
