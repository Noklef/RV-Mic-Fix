[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$UE4SSDll,
    [string]$OutputPath
)
$ErrorActionPreference = 'Stop'
$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (-not $OutputPath) { $OutputPath = Join-Path $repositoryRoot 'dist\RVVoiceFix-0.2.0-test.zip' }
$archivePath = [IO.Path]::GetFullPath($OutputPath)
if ([IO.Path]::GetExtension($archivePath) -ine '.zip') { throw 'OutputPath must end in .zip.' }
& (Join-Path $PSScriptRoot 'Format-Code.ps1') -Check
& (Join-Path $PSScriptRoot 'Build-Native.ps1') -UE4SSDll $UE4SSDll -Analyze -Test
New-Item -ItemType Directory -Path (Split-Path -Parent $archivePath) -Force | Out-Null
$members = @('dist\RVVoiceFix', 'README.md', 'docs', 'LICENSE') | ForEach-Object { Join-Path $repositoryRoot $_ }
Compress-Archive -LiteralPath $members -DestinationPath $archivePath -Force
Write-Output "Created $archivePath (native mod only; no UE4SS loader included)"
