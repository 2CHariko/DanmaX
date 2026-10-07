#requires -Version 7.0
param([ValidateSet('windows-debug','windows-release','windows-static-release')][string] $Preset = 'windows-debug',
      [switch] $SmokeTest, [string] $BuildDirectory = '')
. "$PSScriptRoot/common.ps1"
Invoke-ProjectEnvironment {
    $tools = Initialize-Toolchain -StaticQt:($Preset -eq 'windows-static-release')
    $build = Get-ProjectPath $(if ($BuildDirectory) { $BuildDirectory } else { "out/build/$Preset" })
    $buildRoot = Get-ProjectPath 'out/build'
    if (-not $build.StartsWith($buildRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'BuildDirectory must name a build directory under project out/build.'
    }
    $exe = Join-Path $build 'bin/danmaku_app.exe'
    if (-not (Test-Path -LiteralPath $exe)) { throw 'Build the application first.' }
    $arguments = @('--data-dir', (Get-ProjectPath '.local/data'), '--cache-dir', $env:QML_DISK_CACHE_PATH)
    if ($SmokeTest) { $arguments += '--smoke-test' }
    Invoke-Checked $exe $arguments
}
