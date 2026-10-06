#requires -Version 7.0
param([ValidateSet('windows-debug','windows-release')][string] $Preset = 'windows-debug', [switch] $SmokeTest)
. "$PSScriptRoot/common.ps1"
Invoke-ProjectEnvironment {
    $tools = Initialize-Toolchain
    $exe = Get-ProjectPath "out/build/$Preset/bin/danmaku_app.exe"
    if (-not (Test-Path -LiteralPath $exe)) { throw 'Build the application first.' }
    $arguments = @('--data-dir', (Get-ProjectPath '.local/data'), '--cache-dir', $env:QML_DISK_CACHE_PATH)
    if ($SmokeTest) { $arguments += '--smoke-test' }
    Invoke-Checked $exe $arguments
}
