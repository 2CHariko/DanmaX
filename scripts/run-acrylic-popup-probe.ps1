#requires -Version 7.0
param([switch] $Verify, [double] $Scale = 1, [switch] $NativeStyleContrast, [switch] $NoBuild,
    [ValidateSet('Baseline','NoRedirection','QWindowKit','Combined')] [string] $Mode = 'Baseline')
. "$PSScriptRoot/common.ps1"
Invoke-ProjectEnvironment {
    $tools = Initialize-Toolchain
    $build = Get-ProjectPath 'out/build/acrylic-popup-probe'
    $variant = $Mode.ToLowerInvariant()
    if ($NativeStyleContrast) { $variant += '-native-contrast' }
    $evidence = Get-ProjectPath "out/validation/acrylic-popup-probe/$variant-scale-$Scale"
    New-Item -ItemType Directory -Force $evidence | Out-Null
    $env:QT_SCALE_FACTOR = [string]$Scale
    $env:DANMAKU_ACRYLIC_EVIDENCE = $evidence
    $env:DANMAKU_ACRYLIC_MODE = $Mode
    # Isolated child process only; no permanent/user environment changes.
    Remove-Item Env:QT_QPA_DISABLE_REDIRECTION_SURFACE -ErrorAction SilentlyContinue
    if ($NativeStyleContrast) { $env:DANMAKU_ACRYLIC_NATIVE_CONTRAST = '1' }
    else { Remove-Item Env:DANMAKU_ACRYLIC_NATIVE_CONTRAST -ErrorAction SilentlyContinue }
    Remove-Item Env:QT_QUICK_BACKEND,Env:QT_QPA_PLATFORM -ErrorAction SilentlyContinue
    if (-not $NoBuild) {
        $includePrefix = Get-MsvcIncludesPrefix
        Invoke-Checked $tools.CMake @('-S',$ProjectRoot,'-B',$build,'-G','Ninja',"-DCMAKE_MAKE_PROGRAM=$($tools.Ninja)",'-DCMAKE_BUILD_TYPE=Release',
            '-DDANMAKU_BUILD_APP=OFF','-DBUILD_TESTING=OFF','-DDANMAKU_BUILD_ACRYLIC_POPUP_PROBE=ON',"-DDANMAKU_MSVC_INCLUDES_PREFIX=$includePrefix")
        Invoke-Checked $tools.CMake @('--build',$build,'--target','acrylic_popup_probe','--parallel','4')
    }
    $arguments = if ($Verify) { @('--verify','-o',"$evidence/tests.txt,txt") } else { @() }
    Invoke-Checked (Join-Path $build 'bin/acrylic_popup_probe.exe') $arguments
}
