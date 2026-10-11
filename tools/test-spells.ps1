$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$build=Join-Path $root 'build';New-Item -ItemType Directory -Path $build -Force | Out-Null
$vc=& 'C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe' -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$environment=Join-Path $vc 'VC/Auxiliary/Build/vcvars32.bat'
# Real production modules run with synthetic tables/orders/worlds, without game files or keyboard focus.
# Attack dispatch regressions also verify that spell components keep their native validation path.
$cases=@(@('SpellSlotsTests','config/HeroSpellSlots.cpp','input/ShortcutBindings.cpp'),@('SpellCasterTests','platform/SpellCaster.cpp'),@('SpellAimTests','platform/SpellAim.cpp'),@('FpsCombatGuardTests','platform/FpsCombatGuard.cpp'),@('NativeMovementGuardTests','platform/NativeMovementGuard.cpp'),@('SquadFormationTests'))
$commands=@('@echo off',('call "'+$environment+'" >nul'),'if errorlevel 1 exit /b 1')
foreach($case in $cases) {
    $sources=@('"'+(Join-Path $root ('tests/'+$case[0]+'.cpp'))+'"')
    $sources+=@($case | Select-Object -Skip 1 | ForEach-Object { '"'+(Join-Path $root ('src/'+$_))+'"' })
    $commands+=('cl /nologo /std:c++17 /EHsc /O2 /W4 /DNOMINMAX '+($sources -join ' ')+' /Fe:'+$case[0]+'.exe')
    $commands+=@('if errorlevel 1 exit /b 1',($case[0]+'.exe'),'if errorlevel 1 exit /b 1')
}
$script=Join-Path $build 'test-spells.cmd';$commands | Set-Content -LiteralPath $script -Encoding ascii
Push-Location $build
try {& $env:COMSPEC /d /c $script;if($LASTEXITCODE){throw 'Spell regression failed.'}}
finally {Pop-Location}
