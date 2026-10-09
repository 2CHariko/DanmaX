#requires -Version 7.0
param([ValidateSet('Build','Run')][string]$Mode = 'Run', [ValidateSet('Rin','Fluent','FluentAdapted')][string]$Library = 'Fluent', [ValidateSet('Light','Dark')][string]$Theme = 'Light', [switch]$Interactive, [string]$Scale = '1', [switch]$Slim)
. "$PSScriptRoot/common.ps1"
Invoke-ProjectEnvironment {
    $tools = Initialize-Toolchain
    $variant = if ($Slim) { '-slim' } else { '' }
    $build = Get-ProjectPath "out/build/ui-library-probe$variant-release"
    $source = Get-ProjectPath 'tools/ui-library-probe'
    $fluent = Get-ProjectPath '.cache/ui-library-probe/FluentUI-7e33a2f672d18239ef49ab960075b1137a1d18e7'
    $rin = Get-ProjectPath '.cache/ui-library-probe/Rin-UI-36ad74c888a39c86568b1b8b566103f216c3aff5'
    $extra = Get-ProjectPath '.deps/ui-library-probe/qt-extra'
    if ($Mode -eq 'Build') {
        $prefix = Get-MsvcIncludesPrefix
        & $tools.CMake '-S' $source '-B' $build '-G' 'Ninja' '-DCMAKE_BUILD_TYPE=Release' "-DCMAKE_PREFIX_PATH=$($tools.Qt)" "-DFLUENT_SOURCE=$fluent" "-DCMAKE_CL_SHOWINCLUDES_PREFIX=$prefix" "-DPROBE_SLIM_FLUENT=$($Slim.IsPresent)" *> (Get-ProjectPath 'out/ui-library-probe-configure.log')
        if ($LASTEXITCODE -ne 0) { throw 'Probe configuration failed; see out/ui-library-probe-configure.log' }
        & $tools.CMake '--build' $build '--parallel' '4' *> (Get-ProjectPath 'out/ui-library-probe-build.log')
        if ($LASTEXITCODE -ne 0) { throw 'Probe build failed; see out/ui-library-probe-build.log' }
        return
    }
    $env:PATH = "$extra/bin;$env:PATH"
    $env:QML_IMPORT_PATH = "$extra/qml;$env:QML_IMPORT_PATH"
    $env:QT_SCALE_FACTOR = $Scale
    $imports = if ($Library -eq 'Rin') { $rin } else { "$build/imports" }
    $output = Get-ProjectPath "out/validation/ui-library-probe/$Library$variant-$Theme-$Scale"
    New-Item -ItemType Directory -Force $output | Out-Null
    $probeArguments = @("$source/$Library.qml",$imports,$Theme,$output)
    if ($Interactive) { $probeArguments += '--interactive' }
    & "$build/ui_library_probe.exe" @probeArguments 2>&1 | Tee-Object "$output/runtime.log"
    if ($LASTEXITCODE -ne 0) { throw "Probe failed: $LASTEXITCODE" }
}

