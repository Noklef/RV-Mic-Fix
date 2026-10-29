[CmdletBinding()]
param(
    [switch]$Check,
    [string]$ClangFormatPath
)

$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))

if (-not $ClangFormatPath) {
    $formatter = Get-Command clang-format -ErrorAction SilentlyContinue
    if ($formatter) {
        $ClangFormatPath = $formatter.Source
    } else {
        $ClangFormatPath = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\Llvm\bin\clang-format.exe'
    }
}
if (-not (Test-Path -LiteralPath $ClangFormatPath)) {
    throw 'clang-format 19 is required. Supply -ClangFormatPath or add it to PATH.'
}
$version = & $ClangFormatPath --version
if ($LASTEXITCODE -ne 0 -or $version -notmatch 'clang-format version 19\.') {
    throw "Use clang-format 19 for consistent formatting. Found: $version"
}

$files = @(Get-ChildItem -LiteralPath (Join-Path $root 'src'), (Join-Path $root 'tests') -File -Recurse |
    Where-Object Extension -in '.cpp', '.hpp' |
    ForEach-Object FullName)
[string[]]$arguments = if ($Check) { @('--dry-run', '--Werror') } else { @('-i') }
& $ClangFormatPath @arguments @files
if ($LASTEXITCODE -ne 0) { throw 'C++ formatting check failed. Run scripts/Format-Code.ps1 to apply formatting.' }
Write-Output "Formatting $(if ($Check) { 'checked' } else { 'applied' }) for $($files.Count) C++ files."
