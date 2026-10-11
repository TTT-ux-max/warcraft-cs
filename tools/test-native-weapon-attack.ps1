$ErrorActionPreference='Stop'
$modRoot=Split-Path -Parent $PSScriptRoot
$build=Join-Path $modRoot 'build'
$vs=& 'C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe' -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$environment=Join-Path $vs 'VC/Auxiliary/Build/vcvars32.bat'
# Exercise scoped base replacement, native miss handling and callback removal without retail game code.
$script=Join-Path $build 'test-native-weapon-attack.cmd'
@('@echo off',('call "'+$environment+'" >nul'),
    ('cl /nologo /std:c++17 /EHsc /O2 /W4 /DNOMINMAX "'+(Join-Path $modRoot 'tests/NativeWeaponAttackTests.cpp')+'" "'+(Join-Path $modRoot 'src/platform/NativeWeaponAttack.cpp')+'" /Fe:native-weapon-attack-tests.exe'),
    'if errorlevel 1 exit /b 1','native-weapon-attack-tests.exe') | Set-Content -LiteralPath $script -Encoding ascii
Push-Location $build
try { & $env:COMSPEC /d /c $script;if ($LASTEXITCODE) { throw 'Native weapon attack adapter verification failed.' };Write-Output 'PASS native weapon attack scope, callback lifecycle and fallback invariants.' }
finally { Pop-Location }
