[CmdletBinding()]
param([Parameter(Mandatory = $true)][string]$ModsPath)

$ErrorActionPreference = 'Stop'
$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$sourcePath = Join-Path $repositoryRoot 'Mods\RVProximityFix'
$modsDirectory = Get-Item -LiteralPath $ModsPath
if (-not $modsDirectory.PSIsContainer -or $modsDirectory.Name -ine 'Mods') {
    throw 'ModsPath must identify the existing Mods directory of your UE4SS installation.'
}
$destinationPath = Join-Path $modsDirectory.FullName 'RVProximityFix'
if (Test-Path -LiteralPath $destinationPath) {
    throw "An installation already exists at $destinationPath. Back it up or remove that mod directory before installing."
}
Copy-Item -LiteralPath $sourcePath -Destination $modsDirectory.FullName -Recurse
Write-Output "Installed RVProximityFix to $destinationPath"
Write-Output 'Launch the game and check UE4SS.log for the Loaded line. F8=diagnose, F9=repair, F10=toggle auto.'
