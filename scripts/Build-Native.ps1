[CmdletBinding()]
param(
    [string]$UE4SSDll,
    [string]$BuildToolsPath,
    [switch]$CompileOnly,
    [switch]$Test,
    [switch]$Analyze
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$expected = 'c273e7228687b647226fd63d7ca65888d313ece6be18996ec1320d00a5efe1f8'
$dll = $null
if ($UE4SSDll) {
    $dll = (Get-Item -LiteralPath $UE4SSDll).FullName
    if ((Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash -ine $expected) {
        throw 'Unsupported UE4SS DLL. Use experimental build cdbc0b66 as documented in README.md.'
    }
}
if (-not $BuildToolsPath) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) {
        throw 'Visual Studio 2022 C++ tools not found. Supply -BuildToolsPath.'
    }
    $BuildToolsPath = & $vswhere -latest -products '*' -version '[17.0,18.0)' `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($LASTEXITCODE -ne 0 -or -not $BuildToolsPath) {
        throw 'Visual Studio 2022 with x64 C++ tools is required for local builds.'
    }
}
$devCmd = Join-Path $BuildToolsPath 'Common7\Tools\VsDevCmd.bat'
if (-not (Test-Path -LiteralPath $devCmd)) { throw "Missing MSVC environment: $devCmd" }
# Import the compiler environment; subsequent build operations use native PowerShell.
$environment = & $env:ComSpec /d /c "call `"$devCmd`" -no_logo -arch=x64 -host_arch=x64 && set"
if ($LASTEXITCODE -ne 0) { throw 'MSVC environment setup failed.' }
foreach ($line in $environment) {
    if ($line -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process') }
}
$build = Join-Path $root 'out\build'
New-Item -ItemType Directory -Path $build -Force | Out-Null
Push-Location $build
try {
    $definition = Join-Path $root 'src\UE4SS.def'
    if ($dll) {
        $exports = & dumpbin.exe /nologo /exports $dll
        if ($LASTEXITCODE -ne 0) { throw 'Reading UE4SS exports failed.' }
        $available = @($exports | ForEach-Object {
            if ($_ -match '^\s+\d+\s+[0-9A-F]+\s+[0-9A-F]+\s+(\S+)') { $matches[1] }
        })
        $required = Get-Content -LiteralPath $definition | Where-Object { $_ -match '^\s+\?' } |
            ForEach-Object { $_.Trim() }
        foreach ($symbol in $required) {
            if ($symbol -cnotin $available) { throw "Pinned UE4SS import is missing: $symbol" }
        }
    }
    & lib.exe /nologo /machine:x64 "/def:$definition" /out:UE4SS.lib
    if ($LASTEXITCODE -ne 0) { throw 'Creating the UE4SS import library failed.' }
    $sources = @(Get-ChildItem -LiteralPath (Join-Path $root 'src') -Filter '*.cpp' | ForEach-Object FullName)
    $flags = @('/nologo', '/std:c++20', '/EHsc', '/MD', '/O2', '/W4', '/WX', '/utf-8', '/D_ITERATOR_DEBUG_LEVEL=0', '/DUNICODE', '/D_UNICODE')
    if ($Analyze) { $flags += @('/analyze', '/analyze:external-') }
    if ($CompileOnly) {
        & cl.exe @flags /c @sources
    } else {
        & cl.exe @flags /LD @sources /Fe:main.dll /link UE4SS.lib bcrypt.lib ole32.lib uuid.lib dsound.lib dxguid.lib user32.lib /MACHINE:X64 /INCREMENTAL:NO
    }
    if ($LASTEXITCODE -ne 0) { throw 'Native compilation/linking failed.' }
    if (-not $CompileOnly) {
        $modExports = & dumpbin.exe /nologo /exports main.dll
        if ($LASTEXITCODE -ne 0 -or -not ($modExports -match '\bstart_mod$') -or
            -not ($modExports -match '\buninstall_mod$')) {
            throw 'Native DLL is missing the required UE4SS entry points.'
        }
    }
    if ($Test) {
        & cl.exe @flags (Join-Path $root 'tests\native.cpp') (Join-Path $root 'src\Common.cpp') (Join-Path $root 'src\Devices.cpp') (Join-Path $root 'src\Overlay.cpp') (Join-Path $root 'src\Prefs.cpp') /Fe:native-tests.exe /link bcrypt.lib ole32.lib uuid.lib dsound.lib dxguid.lib user32.lib
        if ($LASTEXITCODE -ne 0) { throw 'Compiling native checks failed.' }
        & .\native-tests.exe
        if ($LASTEXITCODE -ne 0) { throw 'Native checks failed.' }
    }
    if (-not $CompileOnly) {
        $mod = Join-Path $root 'out\dist\RVVoiceFix\dlls'
        New-Item -ItemType Directory -Path $mod -Force | Out-Null
        Copy-Item -LiteralPath main.dll -Destination (Join-Path $mod 'main.dll') -Force
        New-Item -ItemType File -Path (Join-Path $mod '..\enabled.txt') -Force | Out-Null
        [ordered]@{
            version = '0.3.0-test'
            gameSHA256 = '8c229aded5833756b124e9bbad59871b4417f3a7af26b55f194cd448df5537da'
            loaderSHA256 = $expected
            modSHA256 = (Get-FileHash -LiteralPath main.dll -Algorithm SHA256).Hash.ToLowerInvariant()
            localChecksPassed = [bool]$Test
        } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $mod '..\build-info.json') -Encoding utf8
        Write-Output "Built $(Join-Path $mod 'main.dll')"
    }
} finally { Pop-Location }
