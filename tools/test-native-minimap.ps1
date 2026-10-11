$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$build=Join-Path $root 'build'
New-Item -ItemType Directory -Path $build -Force | Out-Null
$vs=& 'C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe' -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$environment=Join-Path $vs 'VC/Auxiliary/Build/vcvars32.bat'
# A hidden synthetic OpenGL window tests framebuffer restoration without opening Warcraft.
$script=Join-Path $build 'test-native-minimap.cmd'
@('@echo off',('call "'+$environment+'" >nul'),
('cl /nologo /std:c++17 /EHsc /O2 /W4 /DNOMINMAX "'+(Join-Path $root 'tests/NativeMinimapTests.cpp')+'" "'+(Join-Path $root 'src/platform/NativeMinimap.cpp')+'" user32.lib gdi32.lib opengl32.lib /Fe:native-minimap-tests.exe'),
'if errorlevel 1 exit /b 1','native-minimap-tests.exe') | Set-Content -LiteralPath $script -Encoding ascii
Push-Location $build
try {& $env:COMSPEC /d /c $script;if ($LASTEXITCODE) {throw 'Native minimap GL verification failed.'};Write-Output 'PASS native minimap capture, exact background restoration and display/hidden state.'}
finally {Pop-Location}
