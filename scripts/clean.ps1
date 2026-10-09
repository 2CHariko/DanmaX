#requires -Version 7.0
[CmdletBinding(SupportsShouldProcess)]
param(
    [ValidateSet('windows-debug','windows-release','windows-static-release','windows-core','all')][string] $Preset = 'windows-debug',
    [switch] $All,
    [switch] $Full
)
. "$PSScriptRoot/common.ps1"

$allowedRoot = Get-ProjectPath 'out/build'
$presetsToClean = [Collections.Generic.List[string]]::new()

if ($All -or $Preset -eq 'all') {
    foreach ($p in @('windows-debug', 'windows-release', 'windows-static-release', 'windows-core')) {
        $presetsToClean.Add($p)
    }
} elseif ($Preset) {
    $presetsToClean.Add($Preset)
}

function Remove-BuildDirectorySafe([string] $targetPath, [string] $label) {
    if (-not $targetPath.StartsWith($allowedRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase) -and
        $targetPath -ne $allowedRoot) {
        throw "Unsafe clean target: $targetPath"
    }
    if (-not (Test-Path -LiteralPath $targetPath)) {
        Write-Host "[- ] ${label}: 目录不存在，无需清理 ($targetPath)"
        return
    }
    foreach ($entry in @((Get-Item -LiteralPath $targetPath)) + @(Get-ChildItem -LiteralPath $targetPath -Recurse -Force)) {
        if ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) {
            throw "Refusing to delete directory containing reparse points: $($entry.FullName)"
        }
    }
    if ($PSCmdlet.ShouldProcess($targetPath, "Remove generated build directory [$label]")) {
        Remove-Item -LiteralPath $targetPath -Recurse -Force
        Write-Host "[OK] 已清理 ${label} 产物: $targetPath"
    }
}

if ($Full) {
    Remove-BuildDirectorySafe $allowedRoot 'out/build (全部构建产物)'
} else {
    foreach ($name in $presetsToClean) {
        $dir = Join-Path $allowedRoot $name
        Remove-BuildDirectorySafe $dir "预设 $name"
    }
}
