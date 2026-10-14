param([string]$FixtureDirectory='')
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
. (Join-Path $root 'setup/host-build.ps1')
. (Join-Path $root 'setup/audio-runtime.ps1')
# The real Player-package validator and the client validator are exercised, not reimplemented.
. (Join-Path $root 'setup/prebuilt-runtime.ps1')
. (Join-Path $root 'launcher/client-validation.ps1')
# Project-authored x86 assemblies supply Game.dll/version metadata and module bytes only; no owned
# Warcraft, CS or dependency file is read, so the host contract is checked without a game install.
$compiler=Join-Path $env:WINDIR 'Microsoft.NET/Framework64/v4.0.30319/csc.exe'
if (!(Test-Path -LiteralPath $compiler)) { throw 'The Windows .NET Framework C# compiler is required to build host fixtures.' }
if (!$FixtureDirectory) { $FixtureDirectory=Join-Path $root ('build/host-build-tests-'+[guid]::NewGuid().ToString('N')) }
function Require([bool]$Condition,[string]$Message) { if (!$Condition) { throw $Message } }
function Reject([scriptblock]$Action,[string]$Message) {
    $failed=$false;try { & $Action | Out-Null } catch { $failed=$true };Require $failed $Message
}
function RejectMessage([scriptblock]$Action) {
    try { & $Action | Out-Null; return '' } catch { return $_.Exception.Message }
}
function New-X86Fixture([string]$Path) {
    # A real x86 DLL: package validation checks the MZ/PE machine type and IMAGE_FILE_DLL flag.
    $source=[IO.Path]::ChangeExtension($Path,'.cs')
    New-Item -ItemType Directory -Path (Split-Path -Parent $Path) -Force | Out-Null
    '[assembly: System.Reflection.AssemblyFileVersion("0.0.0.0")] public sealed class ModuleFixture {}' |
        Set-Content -LiteralPath $source -Encoding ascii
    & $compiler /nologo /target:library /platform:x86 "/out:$Path" $source | Out-Null
    if ($LASTEXITCODE -or !(Test-Path -LiteralPath $Path)) { throw "Module fixture compilation failed: $Path" }
}
function New-VersionFixture([string]$Version,[string]$Path) {
    # Game.dll is replaced by an x86 assembly whose file version is the only interesting property.
    $source=[IO.Path]::ChangeExtension($Path,'.cs')
    New-Item -ItemType Directory -Path (Split-Path -Parent $Path) -Force | Out-Null
    ('[assembly: System.Reflection.AssemblyFileVersion("'+$Version+'")] public sealed class HostFixture {}') |
        Set-Content -LiteralPath $source -Encoding ascii
    & $compiler /nologo /target:library /platform:x86 "/out:$Path" $source | Out-Null
    if ($LASTEXITCODE -or !(Test-Path -LiteralPath $Path)) { throw "Version fixture compilation failed: $Version" }
}
function New-WarcraftTree([string]$Path,[string]$Version) {
    New-Item -ItemType Directory -Path $Path -Force | Out-Null
    foreach ($name in @('war3.exe','Mss32.dll','Storm.dll','War3.mpq','War3x.mpq','War3Patch.mpq','War3xlocal.mpq')) {
        [IO.File]::WriteAllText((Join-Path $Path $name),'owned '+$name)
    }
    foreach ($name in @('Mssfast.m3d','Mp3dec.asi','Reverb3.flt')) {
        $provider=Join-Path $Path "redist/miles/$name"
        New-Item -ItemType Directory -Path (Split-Path -Parent $provider) -Force | Out-Null
        [IO.File]::WriteAllText($provider,'owned provider')
    }
    if ($Version) { New-VersionFixture $Version (Join-Path $Path 'Game.dll') }
    else { [IO.File]::WriteAllText((Join-Path $Path 'Game.dll'),'module without a version resource') }
    return $Path
}
function New-CounterStrikeTree([string]$Path) {
    foreach ($weapon in @('ak47','m4a1','usp','awp','knife','c4')) {
        $model=Join-Path $Path "models/v_$weapon.mdl"
        New-Item -ItemType Directory -Path (Split-Path -Parent $model) -Force | Out-Null
        [IO.File]::WriteAllText($model,'synthetic model')
    }
    foreach ($name in @('sound/weapons/knife_slash1.wav','sound/player/pl_step1.wav')) {
        $sound=Join-Path $Path $name
        New-Item -ItemType Directory -Path (Split-Path -Parent $sound) -Force | Out-Null
        [IO.File]::WriteAllText($sound,'synthetic sound')
    }
    return $Path
}
function New-PrebuiltPackage([string]$Path,[string]$HostVersion,[string]$WarcraftDirectory) {
    New-Item -ItemType Directory -Path $Path -Force | Out-Null
    $hashes=@{}
    foreach ($name in @('WarcraftCS.mix','Mss32.dll')) {
        New-X86Fixture (Join-Path $Path $name)
        $hashes[$name]=(Get-FileHash -LiteralPath (Join-Path $Path $name) -Algorithm SHA256).Hash.ToLowerInvariant()
    }
    $sound=Join-Path $WarcraftDirectory 'WarcraftOriginalMss.dll'
    if (!(Test-Path -LiteralPath $sound)) { $sound=Join-Path $WarcraftDirectory 'Mss32.dll' }
    @{Version='0.8.0';SourceRevision='host-fixture';HostVersion=$HostVersion;Files=$hashes;
        OriginalSoundSha256=(Get-FileHash -LiteralPath $sound -Algorithm SHA256).Hash.ToLowerInvariant()} |
        ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $Path 'runtime.json') -Encoding UTF8
    return $Path
}
$fixture=$FixtureDirectory
$cs=New-CounterStrikeTree (Join-Path $fixture 'Owned CS')
$host126=New-WarcraftTree (Join-Path $fixture 'Owned Warcraft 1.26a') '1.26.0.6401'
$host127=New-WarcraftTree (Join-Path $fixture 'Owned Warcraft 1.27a') '1.27.0.52240'
$hostUnknown=New-WarcraftTree (Join-Path $fixture 'Owned Warcraft later patch') '1.28.0.6000'
$hostUnversioned=New-WarcraftTree (Join-Path $fixture 'Owned Warcraft unversioned') ''
# Both supported hosts are classified from Game.dll alone, and the support level is disclosed.
Require ((Get-WarcraftHostBuild $host126).Version -ceq '1.26.0.6401') '1.26a host was not classified.'
Require ((Get-WarcraftHostBuild $host127).Version -ceq '1.27.0.52240') '1.27a host was not classified.'
Require ((Get-WarcraftHostBuild $host126).Support -ceq 'Complete') 'Wrong 1.26a support level.'
Require ((Get-WarcraftHostBuild $host127).Support -ceq 'Partial') 'Wrong 1.27a support level.'
Require ((Get-WarcraftHostVersionList) -ceq '1.26.0.6401 or 1.27.0.52240') 'Supported host list changed unexpectedly.'
Require (Test-WarcraftHostVersion '1.27.0.52240') 'Supported host name was rejected.'
Require (!(Test-WarcraftHostVersion '1.28.0.6000') -and !(Test-WarcraftHostVersion '')) 'Unknown host name was accepted.'
# The client gate accepts both hosts and refuses anything the runtime cannot bind to.
Assert-ClientGames $host126 $cs | Out-Null
Assert-ClientGames $host127 $cs | Out-Null
Require ((RejectMessage { Assert-ClientGames $hostUnknown $cs }).Contains('1.28.0.6000')) 'Unsupported Game.dll rejection omits the reported version.'
Reject { Assert-ClientGames $hostUnknown $cs } 'A later patch was accepted as a host.'
Reject { Assert-ClientGames $hostUnversioned $cs } 'Game.dll without a version resource was accepted.'
# A Player package installs only into the host it recorded, even when both hosts look identical on disk.
$package126=New-PrebuiltPackage (Join-Path $fixture 'Package 1.26a') '1.26.0.6401' $host126
$package127=New-PrebuiltPackage (Join-Path $fixture 'Package 1.27a') '1.27.0.52240' $host127
$packageUnknown=New-PrebuiltPackage (Join-Path $fixture 'Package later patch') '1.28.0.6000' $host126
$packageSilent=New-PrebuiltPackage (Join-Path $fixture 'Package without a host') '' $host126
Assert-PrebuiltHost $package126 $host126
Assert-PrebuiltHost $package127 $host127
$mismatch=RejectMessage { Assert-PrebuiltHost $package126 $host127 }
Require ($mismatch.Contains('1.26.0.6401') -and $mismatch.Contains('1.27.0.52240')) 'Host mismatch message hides one of the two builds.'
Reject { Assert-PrebuiltHost $package126 $host127 } '1.26a package was accepted for a 1.27a host.'
Reject { Assert-PrebuiltHost $package127 $host126 } '1.27a package was accepted for a 1.26a host.'
Reject { Assert-PrebuiltHost $packageUnknown $host126 } 'Package with an unsupported host was accepted.'
Reject { Assert-PrebuiltHost $packageSilent $host126 } 'Package without a recorded host was accepted.'
# A mismatched Mss32.dll still stands down: the forwarder ABI is owned per installation.
$otherSound=New-WarcraftTree (Join-Path $fixture 'Owned Warcraft other Miles') '1.26.0.6401'
[IO.File]::WriteAllText((Join-Path $otherSound 'Mss32.dll'),'different owned Miles library')
Reject { Assert-PrebuiltHost $package126 $otherSound } 'Package accepted a different Miles library.'
Write-Output ("PASS host builds: 1.26a/1.27a accepted, unknown and unversioned Game.dll refused, Player package requires its exact host. Fixtures: $fixture")
