[CmdletBinding()]
param([string]$OutputPath)

$ErrorActionPreference = 'Stop'
$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (-not $OutputPath) {
    $OutputPath = Join-Path $repositoryRoot 'dist\RVProximityFix-0.1.0.zip'
}
$archivePath = [IO.Path]::GetFullPath($OutputPath)
if ([IO.Path]::GetExtension($archivePath) -ine '.zip') { throw 'OutputPath must end in .zip.' }
$archiveParent = Split-Path -Parent $archivePath
if (-not (Test-Path -LiteralPath $archiveParent)) {
    New-Item -ItemType Directory -Path $archiveParent -Force | Out-Null
}
$members = @('Mods', 'README.md', 'docs', 'LICENSE') | ForEach-Object { Join-Path $repositoryRoot $_ }
Compress-Archive -LiteralPath $members -DestinationPath $archivePath -Force
Write-Output "Created $archivePath"
