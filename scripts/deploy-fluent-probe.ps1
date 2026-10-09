#requires -Version 7.0
. "$PSScriptRoot/common.ps1"
Invoke-ProjectEnvironment {
    $tools = Initialize-Toolchain
    $build = Get-ProjectPath 'out/build/ui-library-probe-slim-release'
    $stage = Get-ProjectPath ('out/packages/fluent-probe-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
    $extra = Get-ProjectPath '.deps/ui-library-probe/qt-extra'
    $source = Get-ProjectPath 'tools/ui-library-probe'
    New-Item -ItemType Directory -Force "$stage/app-qml","$stage/qml/FluentUI" | Out-Null
    Copy-Item "$build/ui_library_probe.exe","$build/fluent_probe.dll" $stage
    foreach ($name in @('FluentAdapted.qml','ProbeComboBox.qml','ProbeExpander.qml')) { Copy-Item (Join-Path $source $name) "$stage/app-qml" }
    Copy-Item "$build/imports/FluentUI/*" "$stage/qml/FluentUI" -Recurse
    $env:PATH = "$extra/bin;$env:PATH"
    $env:QML_IMPORT_PATH = "$extra/qml;$build/imports;$env:QML_IMPORT_PATH"
    # windeployqt resolves DLLs through QT_INSTALL_BINS, not PATH. Create a
    # disposable, project-local SDK view; never add files to the locked SDK.
    $sdkView = Get-ProjectPath '.cache/ui-library-probe/deploy-sdk-view'
    New-Item -ItemType Directory -Force "$sdkView/bin" | Out-Null
    foreach ($directory in @("$($tools.Qt)/bin", "$extra/bin")) {
        Get-ChildItem $directory -File | Where-Object Name -ne 'qt.conf' | ForEach-Object {
            $link = Join-Path "$sdkView/bin" $_.Name
            if (!(Test-Path $link)) { New-Item -ItemType HardLink -Path $link -Target $_.FullName | Out-Null }
        }
    }
    @('[Paths]',"Prefix=$($tools.Qt.Replace('\','/'))", "Binaries=$($sdkView.Replace('\','/'))/bin") | Set-Content "$sdkView/bin/qt.conf"
    & "$($tools.Qt)/bin/windeployqt.exe" --qtpaths "$sdkView/bin/qtpaths.exe" --release --no-translations --no-compiler-runtime --no-opengl-sw --qmldir "$stage/app-qml" --qmlimport "$build/imports" --qmlimport "$extra/qml" "$stage/ui_library_probe.exe" *> "$stage/deploy.log"
    if ($LASTEXITCODE -ne 0) { throw "Deployment failed: $stage/deploy.log" }
    # Extra modules live outside the SDK; copy their exact locked runtime DLLs if used.
    Copy-Item "$extra/bin/Qt6ShaderTools.dll","$extra/bin/Qt6Core5Compat.dll" $stage
    @('[Paths]','Prefix=.','QmlImports=qml','Plugins=.') | Set-Content "$stage/qt.conf"
    Copy-Item (Get-ProjectPath '.cache/ui-library-probe/FluentUI-7e33a2f672d18239ef49ab960075b1137a1d18e7/License') "$stage/FluentUI-LICENSE.txt"
    Copy-Item "$source/dependencies.lock.json","$build/fluent-source-manifest.txt" $stage
    @'
param([ValidateSet('Light','Dark')][string]$Theme = 'Dark')
$env:QML_DISABLE_DISK_CACHE = '1'
& "$PSScriptRoot/ui_library_probe.exe" "$PSScriptRoot/app-qml/FluentAdapted.qml" "$PSScriptRoot/qml" $Theme "$PSScriptRoot/preview" --interactive
'@ | Set-Content "$stage/preview.ps1"
    'Local evaluation deployment only. Requires installed Microsoft VC++ x64 runtime. Not a public release; full Qt/font/third-party redistribution materials remain to be completed.' | Set-Content "$stage/README.txt"
    $dumpbin = Join-Path $env:VCToolsInstallDir 'bin/Hostx64/x64/dumpbin.exe'
    & $dumpbin /DEPENDENTS "$stage/fluent_probe.dll" | Set-Content "$stage/fluent-dependencies.txt"
    # Verify with SDK/import paths removed from this child environment.
    $env:PATH = "$env:SystemRoot/System32;$env:SystemRoot"
    foreach ($name in @('QT_PLUGIN_PATH','QML_IMPORT_PATH','QML2_IMPORT_PATH','QT_QPA_PLATFORM_PLUGIN_PATH')) { [Environment]::SetEnvironmentVariable($name,$null,'Process') }
    foreach ($theme in @('Light','Dark')) {
        $output = "$stage/verification-$theme"
        & "$stage/ui_library_probe.exe" "$stage/app-qml/FluentAdapted.qml" "$stage/qml" $theme $output 2>&1 | Tee-Object "$stage/run-$theme.log"
        if ($LASTEXITCODE -ne 0) { throw "Deployed $theme probe failed: $LASTEXITCODE" }
    }
    Set-Content (Get-ProjectPath 'out/validation/ui-library-probe/latest-deployment.txt') $stage
    Write-Host "Verified deployment: $stage"
}
