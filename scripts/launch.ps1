$workspace = Split-Path -Parent $PSScriptRoot
$portableExe = Join-Path $workspace 'dist/portable/bin/SerikaPhotoEdit.exe'
if (Test-Path -LiteralPath $portableExe) { Start-Process -FilePath $portableExe -WorkingDirectory (Split-Path $portableExe) }
else { $env:PATH = "$workspace/.tools/Qt/6.8.3/mingw_64/bin;$workspace/.tools/Qt/Tools/mingw1310_64/bin;$workspace/.tools/deps/install/bin;$env:PATH"; Start-Process -FilePath "$workspace/build/SerikaPhotoEdit.exe" -WorkingDirectory $workspace }
