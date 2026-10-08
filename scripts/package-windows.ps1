param([string]$QtRoot = '', [switch]$SkipMsi)
$ErrorActionPreference = 'Stop'
$workspace = Split-Path -Parent $PSScriptRoot
if (-not $QtRoot) {
    $bundledQt = "$workspace/.tools/Qt/6.8.3/mingw_64"
    $QtRoot = if (Test-Path -LiteralPath $bundledQt) { $bundledQt } else { $env:QT_ROOT_DIR }
}
if (-not $QtRoot -or -not (Test-Path -LiteralPath "$QtRoot/bin/windeployqt.exe")) {
    throw 'Provide -QtRoot pointing to the Qt desktop kit used for the build.'
}
$env:PATH = "$QtRoot/bin;$workspace/.tools/wix;$env:PATH"
if ($QtRoot -match 'mingw') {
    $env:PATH = "$workspace/.tools/Qt/Tools/mingw1310_64/bin;$workspace/.tools/deps/install/bin;$env:PATH"
}
$cmakeBin = "$workspace/.tools/python/cmake/data/bin"
function Resolve-PackageTool([string]$Bundled, [string]$Name) {
    if (Test-Path -LiteralPath $Bundled) { return $Bundled }
    $command = Get-Command $Name -CommandType Application -ErrorAction SilentlyContinue
    if (-not $command) { throw "Required packaging tool '$Name' was not found." }
    return $command.Source
}
$cmakeExe = Resolve-PackageTool "$cmakeBin/cmake.exe" 'cmake'
$cpackExe = Resolve-PackageTool "$cmakeBin/cpack.exe" 'cpack'
& $cmakeExe --install "$workspace/build" --prefix "$workspace/dist/portable"
if ($LASTEXITCODE) { throw 'Installation staging failed' }
# CMake stages the dependencies actually linked into the executable. Do not inject
# optional MinGW DLLs into a build from another kit.
$licenses = "$workspace/dist/portable/share/serika-photoedit/licenses"
New-Item -ItemType Directory -Force -Path $licenses | Out-Null
Copy-Item -Path "$workspace/resources/licenses/*" -Destination $licenses -Force
$librawDocs = "$workspace/.tools/deps/install/share/doc/libraw"
if (Test-Path -LiteralPath $librawDocs) { Copy-Item -Path "$librawDocs/*" -Destination $licenses -Force }
& $cpackExe --config "$workspace/build/CPackConfig.cmake" -G ZIP -B "$workspace/dist"
if ($LASTEXITCODE) { throw 'Portable package failed' }
if (-not $SkipMsi) {
    if (-not (Get-Command candle.exe -ErrorAction SilentlyContinue) -or -not (Get-Command light.exe -ErrorAction SilentlyContinue)) {
        throw 'ZIP and portable files were staged. MSI requires WiX v3 candle.exe and light.exe in PATH; use -SkipMsi for ZIP only.'
    }
    & $cpackExe --config "$workspace/build/CPackConfig.cmake" -G WIX -B "$workspace/dist"
    if ($LASTEXITCODE) { throw 'MSI packaging failed; verify WiX v3 binaries and the CPack log.' }
}
