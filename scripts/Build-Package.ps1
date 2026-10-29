[CmdletBinding()]
param(
    [string]$UE4SSDll,
    [string]$OutputPath
)
$ErrorActionPreference = 'Stop'
$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (-not $OutputPath) { $OutputPath = Join-Path $repositoryRoot 'out\dist\RVVoiceFix-0.3.0-test.zip' }
$archivePath = [IO.Path]::GetFullPath($OutputPath)
if ([IO.Path]::GetExtension($archivePath) -ine '.zip') { throw 'OutputPath must end in .zip.' }
& (Join-Path $PSScriptRoot 'Format-Code.ps1') -Check
& (Join-Path $PSScriptRoot 'Build-Native.ps1') -UE4SSDll $UE4SSDll -Analyze -Test
New-Item -ItemType Directory -Path (Split-Path -Parent $archivePath) -Force | Out-Null
$stage = Join-Path $repositoryRoot 'out\package'
if (Test-Path -LiteralPath $stage) {
    $resolved = (Resolve-Path -LiteralPath $stage).Path
    if ($resolved -ne [IO.Path]::GetFullPath($stage) -or
        -not $resolved.StartsWith($repositoryRoot + [IO.Path]::DirectorySeparatorChar) -or
        ((Get-Item -LiteralPath $stage).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw 'Package staging directory must be inside this repository and cannot be a link.'
    }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
New-Item -ItemType Directory -Path (Join-Path $stage 'docs') -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $repositoryRoot 'out\dist\RVVoiceFix') -Destination $stage -Recurse
foreach ($file in @('README.md', 'LICENSE')) {
    Copy-Item -LiteralPath (Join-Path $repositoryRoot $file) -Destination $stage
}
foreach ($file in @('TESTING.md', 'STATUS.md', 'NATIVE-EVIDENCE.md', 'DEVELOPMENT.md', 'VOICE-DIAGNOSTICS.md', 'DESIGN.md')) {
    Copy-Item -LiteralPath (Join-Path $repositoryRoot "docs\$file") -Destination (Join-Path $stage 'docs')
}
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $archivePath -Force
Write-Output "Created $archivePath (native mod only; no UE4SS loader included)"
