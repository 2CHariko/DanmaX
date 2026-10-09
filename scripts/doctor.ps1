#requires -Version 7.0
. "$PSScriptRoot/common.ps1"
$tools = Get-ToolPaths
$missing = $false
foreach ($key in @('CMake','CTest','Ninja','Qt')) {
    $present = Test-Path -LiteralPath $tools[$key]
    Write-Host "$key : $($tools[$key]) [$(if ($present) { 'found' } else { 'missing' })]"
    if (-not $present) { $missing = $true }
}
$localPath = Get-ProjectPath '.local/toolchain.local.json'
if (Test-Path -LiteralPath $localPath) {
    $local = Get-Content $localPath -Raw | ConvertFrom-Json
    Write-Host "Explicit system toolchain: $($local.visualStudioPath)"
    if (-not $local.allowExternalSystemToolchain -or -not (Test-Path (Join-Path $local.visualStudioPath 'Common7/Tools/VsDevCmd.bat'))) { $missing = $true }
} else {
    Write-Host 'Missing .local/toolchain.local.json; see toolchain/local.example.json.'
    $missing = $true
}
Write-Host "Locked MSVC=$($DependencyLock.msvcToolsetVersion); SDK=$($DependencyLock.windowsSdkVersion)"
if ($missing) { throw 'Toolchain is incomplete.' }
