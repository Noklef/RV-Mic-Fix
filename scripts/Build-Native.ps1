[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$UE4SSDll,
    [string]$BuildToolsPath = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools',
    [switch]$CompileOnly,
    [switch]$Test
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$dll = (Get-Item -LiteralPath $UE4SSDll).FullName
$expected = 'c273e7228687b647226fd63d7ca65888d313ece6be18996ec1320d00a5efe1f8'
if ((Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash -ine $expected) {
    throw 'Unsupported UE4SS DLL. Use the pinned zDEV cdbc0b66 build documented in README.md.'
}
$devCmd = Join-Path $BuildToolsPath 'Common7\Tools\VsDevCmd.bat'
if (-not (Test-Path -LiteralPath $devCmd)) { throw "Missing MSVC environment: $devCmd" }
# Import the compiler environment; subsequent build operations use native PowerShell.
$environment = & $env:ComSpec /d /c "call `"$devCmd`" -no_logo -arch=x64 -host_arch=x64 && set"
if ($LASTEXITCODE -ne 0) { throw 'MSVC environment setup failed.' }
foreach ($line in $environment) {
    if ($line -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process') }
}
$build = Join-Path $root 'build'
New-Item -ItemType Directory -Path $build -Force | Out-Null
Push-Location $build
try {
    $exports = & dumpbin.exe /nologo /exports $dll
    if ($LASTEXITCODE -ne 0) { throw 'Reading UE4SS exports failed.' }
    $symbols = @($exports | ForEach-Object {
        if ($_ -match '^\s+\d+\s+[0-9A-F]+\s+[0-9A-F]+\s+(\S+)') { $matches[1] }
    })
    if (-not $symbols.Count) { throw 'No UE4SS exports found.' }
    @('LIBRARY UE4SS.dll', 'EXPORTS') + $symbols | Set-Content -LiteralPath UE4SS.def -Encoding ascii
    & lib.exe /nologo /machine:x64 /def:UE4SS.def /out:UE4SS.lib
    if ($LASTEXITCODE -ne 0) { throw 'Creating the UE4SS import library failed.' }
    $sources = @(Get-ChildItem -LiteralPath (Join-Path $root 'src') -Filter '*.cpp' | ForEach-Object FullName)
    $flags = @('/nologo', '/std:c++20', '/EHsc', '/MD', '/O2', '/W4', '/utf-8', '/D_ITERATOR_DEBUG_LEVEL=0', '/DUNICODE', '/D_UNICODE')
    if ($CompileOnly) {
        & cl.exe @flags /c @sources
    } else {
        & cl.exe @flags /LD @sources /Fe:main.dll /link UE4SS.lib bcrypt.lib ole32.lib uuid.lib dsound.lib dxguid.lib user32.lib gdi32.lib /MACHINE:X64 /INCREMENTAL:NO
    }
    if ($LASTEXITCODE -ne 0) { throw 'Native compilation/linking failed.' }
    if ($Test) {
        & cl.exe @flags (Join-Path $root 'tests\native.cpp') (Join-Path $root 'src\Common.cpp') (Join-Path $root 'src\Devices.cpp') (Join-Path $root 'src\Panel.cpp') /Fe:native-tests.exe /link bcrypt.lib ole32.lib uuid.lib dsound.lib dxguid.lib user32.lib gdi32.lib
        if ($LASTEXITCODE -ne 0) { throw 'Compiling native checks failed.' }
        & .\native-tests.exe
        if ($LASTEXITCODE -ne 0) { throw 'Native checks failed.' }
        Copy-Item -LiteralPath native-tests.exe -Destination panel-preview.exe -Force
    }
    if (-not $CompileOnly) {
        $mod = Join-Path $root 'dist\RVVoiceFix\dlls'
        New-Item -ItemType Directory -Path $mod -Force | Out-Null
        Copy-Item -LiteralPath main.dll -Destination (Join-Path $mod 'main.dll') -Force
        New-Item -ItemType File -Path (Join-Path $mod '..\enabled.txt') -Force | Out-Null
        [ordered]@{
            version = '0.2.0-test'
            gameSHA256 = '12b24146b30d550fe47a4179c7b05d1f21b69f058524e0bba4b41397e3e66221'
            loaderSHA256 = $expected
            modSHA256 = (Get-FileHash -LiteralPath main.dll -Algorithm SHA256).Hash.ToLowerInvariant()
            localChecksPassed = [bool]$Test
        } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $mod '..\build-info.json') -Encoding utf8
        Write-Output "Built $(Join-Path $mod 'main.dll')"
    }
} finally { Pop-Location }
