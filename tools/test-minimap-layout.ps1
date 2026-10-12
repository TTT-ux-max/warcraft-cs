$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$build=Join-Path $root 'build'
New-Item -ItemType Directory -Path $build -Force | Out-Null
$vs=& 'C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe' -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$environment=Join-Path $vs 'VC/Auxiliary/Build/vcvars32.bat'
# Portable layout checks need neither a native game nor GPU/framebuffer fixtures.
$script=Join-Path $build 'test-minimap-layout.cmd'
@('@echo off',('call "'+$environment+'" >nul'),
('cl /nologo /std:c++17 /EHsc /O2 /W4 /DNOMINMAX "'+(Join-Path $root 'tests/MinimapLayoutTests.cpp')+'" /Fe:minimap-layout-tests.exe'),
'if errorlevel 1 exit /b 1','minimap-layout-tests.exe') | Set-Content -LiteralPath $script -Encoding ascii
Push-Location $build
try {& $env:COMSPEC /d /c $script;if ($LASTEXITCODE) {throw 'Minimap layout verification failed.'};Write-Output 'PASS minimap bounds and compact hint wrapping.'}
finally {Pop-Location}
