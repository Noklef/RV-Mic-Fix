[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$ModsPath,
    [Parameter(Mandatory = $true)][string]$GameExecutable
)
$ErrorActionPreference = 'Stop'
$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$sourcePath = Join-Path $repositoryRoot 'dist\RVVoiceFix'
$manifest = Get-Content -LiteralPath (Join-Path $sourcePath 'build-info.json') -Raw | ConvertFrom-Json
$modsDirectory = Get-Item -LiteralPath $ModsPath
if (-not $modsDirectory.PSIsContainer -or $modsDirectory.Name -ine 'Mods') {
    throw 'ModsPath must identify the existing Mods directory of your UE4SS installation.'
}
$game = Get-Item -LiteralPath $GameExecutable
if ($game.Name -ine 'Ride-Win64-Shipping.exe') { throw 'GameExecutable must identify Ride-Win64-Shipping.exe.' }
if (Get-Process -Name 'Ride-Win64-Shipping', 'Ride' -ErrorAction SilentlyContinue) { throw 'Exit the game before installing.' }
$loader = Join-Path $modsDirectory.Parent.FullName 'UE4SS.dll'
if ((Get-FileHash -LiteralPath $game.FullName -Algorithm SHA256).Hash -ine $manifest.gameSHA256) { throw 'Unsupported game executable.' }
if ((Get-FileHash -LiteralPath $loader -Algorithm SHA256).Hash -ine $manifest.loaderSHA256) { throw 'Unsupported UE4SS loader.' }
if ((Get-FileHash -LiteralPath (Join-Path $sourcePath 'dlls\main.dll') -Algorithm SHA256).Hash -ine $manifest.modSHA256) { throw 'Mod DLL does not match the build manifest. Rebuild first.' }
$destinationPath = Join-Path $modsDirectory.FullName 'RVVoiceFix'
foreach ($name in @('RVVoiceFix', 'RVProximityFix')) {
    if (Test-Path -LiteralPath (Join-Path $modsDirectory.FullName $name)) {
        throw "An installation of $name already exists. Back up and remove that mod before installing."
    }
}
Copy-Item -LiteralPath $sourcePath -Destination $modsDirectory.FullName -Recurse
Write-Output "Installed RVVoiceFix to $destinationPath"
Write-Output 'Launch windowed/borderless. Check RVVoiceFix.log. F7=panel F8=diagnose F9=repair F10=auto.'
