#requires -Version 7.0
param([ValidateSet('windows-debug','windows-release','windows-core')][string] $Preset = 'windows-debug', [switch] $Test)
. "$PSScriptRoot/common.ps1"
Invoke-ProjectEnvironment {
    $tools = Initialize-Toolchain -CoreOnly:($Preset -eq 'windows-core')
    $includePrefix = Get-MsvcIncludesPrefix
    Invoke-Checked $tools.CMake @('--preset', $Preset, "-DDANMAKU_MSVC_INCLUDES_PREFIX=$includePrefix")
    Invoke-Checked $tools.CMake @('--build', '--preset', $Preset, '--parallel')
    if ($Test) { Invoke-Checked $tools.CTest @('--preset', $Preset) }
}
