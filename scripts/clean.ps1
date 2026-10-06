#requires -Version 7.0
[CmdletBinding(SupportsShouldProcess)]
param([ValidateSet('windows-debug','windows-release','windows-core')][string] $Preset = 'windows-debug')
. "$PSScriptRoot/common.ps1"
$target = Get-ProjectPath "out/build/$Preset"
$allowedRoot = Get-ProjectPath 'out/build'
if (-not $target.StartsWith($allowedRoot + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Unsafe clean target.' }
if (Test-Path -LiteralPath $target) {
    foreach ($entry in @((Get-Item -LiteralPath $target)) + @(Get-ChildItem -LiteralPath $target -Recurse -Force)) {
        if ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Refusing to recursively delete a build tree containing reparse points.' }
    }
    if ($PSCmdlet.ShouldProcess($target, 'Remove generated build directory')) { Remove-Item -LiteralPath $target -Recurse -Force }
}
