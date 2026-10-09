param([string]$Prefix = "$PSScriptRoot/../.tools/deps/install",
      [string]$Work = "$PSScriptRoot/../.tools/deps/lcms-ci")
$ErrorActionPreference = 'Stop'
$taskPrefix = [IO.Path]::GetFullPath($Prefix)
$taskWork = [IO.Path]::GetFullPath($Work)
New-Item -ItemType Directory -Force -Path $taskWork | Out-Null
$taskArchive = Join-Path $taskWork 'lcms2.19.1.zip'
if (!(Test-Path -LiteralPath $taskArchive)) {
    Invoke-WebRequest -Uri 'https://github.com/mm2/Little-CMS/archive/refs/tags/lcms2.19.1.zip' -OutFile $taskArchive
}
$taskHash = (Get-FileHash -LiteralPath $taskArchive -Algorithm SHA256).Hash.ToLowerInvariant()
if ($taskHash -ne '8b7fdc5a708d9f5f41baeecafd2c32b993f0614448f2533eea5deaf841ea1c78') {
    throw 'LittleCMS archive checksum mismatch.'
}
$taskSource = Join-Path $taskWork 'Little-CMS-lcms2.19.1'
if (!(Test-Path -LiteralPath "$taskSource/CMakeLists.txt")) {
    Expand-Archive -LiteralPath $taskArchive -DestinationPath $taskWork -Force
}
$taskBuild = Join-Path $taskWork 'build'
& cmake -S $taskSource -B $taskBuild -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_INSTALL_PREFIX=$taskPrefix" -DLCMS2_BUILD_SHARED=ON -DLCMS2_BUILD_STATIC=OFF -DLCMS2_BUILD_TOOLS=OFF -DLCMS2_BUILD_TESTS=OFF
if ($LASTEXITCODE -ne 0) { throw 'LittleCMS configure failed.' }
& cmake --build $taskBuild --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'LittleCMS build failed.' }
& cmake --install $taskBuild
if ($LASTEXITCODE -ne 0) { throw 'LittleCMS install failed.' }
