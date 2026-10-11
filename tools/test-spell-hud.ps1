$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$build=Join-Path $root 'build';New-Item -ItemType Directory -Path $build -Force | Out-Null
$vs=& 'C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe' -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$environment=Join-Path $vs 'VC/Auxiliary/Build/vcvars32.bat'
# Synthetic structures and image bytes verify the production adapters without owning any game files or focus.
$script=Join-Path $build 'test-spell-hud.cmd'
@('@echo off',('call "'+$environment+'" >nul'),
('cl /nologo /std:c++17 /EHsc /O2 /W4 /DNOMINMAX "'+(Join-Path $root 'tests/SpellHudTests.cpp')+'" "'+(Join-Path $root 'src/platform/NativeAbilityInfo.cpp')+'" "'+(Join-Path $root 'src/platform/SpellIconImage.cpp')+'" ole32.lib windowscodecs.lib /Fe:spell-hud-tests.exe'),
'if errorlevel 1 exit /b 1','spell-hud-tests.exe') | Set-Content -LiteralPath $script -Encoding ascii
Push-Location $build
try {& $env:COMSPEC /d /c $script;if($LASTEXITCODE){throw 'Spell HUD regression failed.'}}
finally {Pop-Location}
