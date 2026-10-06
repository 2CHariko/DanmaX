#requires -Version 7.0
param([ValidateSet('windows-release')][string] $Preset = 'windows-release')
. "$PSScriptRoot/common.ps1"
Invoke-ProjectEnvironment {
    $tools = Initialize-Toolchain
    $exe = Get-ProjectPath "out/build/$Preset/bin/danmaku_app.exe"
    if (-not (Test-Path -LiteralPath $exe)) { throw 'Build Release before packaging.' }
    $stage = Get-ProjectPath ('out/stage/LocalDanmaku-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
    New-Item -ItemType Directory -Force $stage | Out-Null
    Copy-Item -LiteralPath $exe -Destination $stage
    Invoke-Checked (Join-Path $tools.Qt 'bin/windeployqt.exe') @('--release','--no-compiler-runtime','--qmldir',(Get-ProjectPath 'qml'),(Join-Path $stage 'danmaku_app.exe'))
    Copy-Item -LiteralPath (Get-ProjectPath 'LICENSE') -Destination $stage
    Copy-Item -LiteralPath (Get-ProjectPath 'docs/THIRD_PARTY.md') -Destination $stage
    $licenses = Join-Path $tools.Qt 'LICENSES'
    if (Test-Path -LiteralPath $licenses) { Copy-Item -LiteralPath $licenses -Destination (Join-Path $stage 'qt-licenses') -Recurse }
    Write-Host "Development deployment created: $stage"
    Write-Host 'Requires the official Microsoft Visual C++ 2015-2022 x64 runtime. Review THIRD_PARTY.md before public distribution.'
}
