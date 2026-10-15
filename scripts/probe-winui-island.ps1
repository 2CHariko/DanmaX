#requires -Version 7.0
param([ValidateSet('Build','Stage','Smoke','Run')][string]$Mode = 'Smoke', [switch]$StaticQt, [switch]$QtOnly,
      [switch]$SelfContained, [switch]$Staged,
      [ValidateSet('baseline','popup','modal','clip')][string]$Scenario = 'baseline', [string]$Scale = '1',
      [ValidatePattern('^[a-zA-Z0-9-]+$')][string]$RunLabel = 'default')
. "$PSScriptRoot/common.ps1"
. "$PSScriptRoot/deployment.ps1"
Invoke-ProjectEnvironment {
    $tools = Initialize-Toolchain -StaticQt:$StaticQt
    $variant = if ($StaticQt) { 'static' } else { 'dynamic' }
    $build = Get-ProjectPath "out/build/winui-island-$variant-release"
    $deployment = if ($SelfContained) { 'self-contained' } else { 'framework' }
    $stage = Get-ProjectPath "out/stage/winui-island/$variant-$deployment"
    $kind = if ($QtOnly) { 'qt-only' } else { "native-$deployment" }
    $report = Get-ProjectPath "out/validation/winui-island/$variant-$Scale-$Scenario/$kind-$($Mode.ToLowerInvariant())/$RunLabel"
    New-Item -ItemType Directory -Force $build,$report | Out-Null
    if ($Mode -eq 'Build') {
        $prefix = Get-MsvcIncludesPrefix
        Invoke-Checked $tools.CMake @('-S',$ProjectRoot,'-B',$build,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release',
            '-DDANMAKU_BUILD_APP=OFF','-DBUILD_TESTING=OFF','-DDANMAKU_BUILD_WINUI_ISLAND_PROBE=ON',
            "-DDANMAKU_STATIC_QT=$($StaticQt.IsPresent)","-DCMAKE_MAKE_PROGRAM=$($tools.Ninja)","-DDANMAKU_MSVC_INCLUDES_PREFIX=$prefix")
        Invoke-Checked $tools.CMake @('--build',$build,'--target','winui_island_probe','--parallel','4')
        return
    }
    $exe = Join-Path $build 'bin/winui_island_probe.exe'
    if ($Mode -eq 'Stage') {
        Assert-DeploymentDirectory $stage
        New-Item -ItemType Directory -Force $stage | Out-Null
        Copy-Item -LiteralPath $exe -Destination $stage -Force
        Copy-Item -LiteralPath (Join-Path $build 'bin/Microsoft.WindowsAppRuntime.Bootstrap.dll') -Destination $stage -Force
        $packages = Get-ProjectPath '.deps/winui-island/packages'
        if ($SelfContained) {
            $manifest = [xml]'<assembly manifestVersion="1.0" xmlns="urn:schemas-microsoft-com:asm.v1" xmlns:asmv3="urn:schemas-microsoft-com:asm.v3" xmlns:winrtv1="urn:schemas-microsoft-com:winrt.v1"/>'
            foreach ($component in @('foundation','interactiveexperiences','winui','dwrite')) {
                $componentRoot = Join-Path $packages "microsoft.windowsappsdk.$component"
                $native = Join-Path $componentRoot 'runtimes-framework/win-x64/native'
                if (!(Test-Path -LiteralPath $native)) { throw "Missing native payload: $native" }
                Get-ChildItem -LiteralPath $native | ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $stage -Recurse -Force }
                $metadata = Join-Path $componentRoot 'metadata'
                if (Test-Path -LiteralPath $metadata) {
                    Get-ChildItem -LiteralPath $metadata -Filter '*.winmd' -File | ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $stage -Force }
                }
                # Follow the package's Microsoft.WindowsAppSDK.SelfContained.targets:
                # WinRT activation classes and COM proxy registrations must be in the EXE manifest.
                $fragmentFile = Join-Path $componentRoot 'runtimes-framework/package.appxfragment'
                if (!(Test-Path -LiteralPath $fragmentFile)) { continue }
                $fragment = [xml](Get-Content -LiteralPath $fragmentFile -Raw)
                $namespaces = [Xml.XmlNamespaceManager]::new($fragment.NameTable)
                $namespaces.AddNamespace('m','http://schemas.microsoft.com/appx/manifest/foundation/windows10')
                foreach ($server in $fragment.SelectNodes('//m:InProcessServer',$namespaces)) {
                    $node = $manifest.CreateElement('asmv3','file','urn:schemas-microsoft-com:asm.v3')
                    $node.SetAttribute('name',$server.Path)
                    foreach ($type in $server.SelectNodes('m:ActivatableClass',$namespaces)) {
                        $activation = $manifest.CreateElement('winrtv1','activatableClass','urn:schemas-microsoft-com:winrt.v1')
                        $activation.SetAttribute('name',$type.ActivatableClassId)
                        $activation.SetAttribute('threadingModel','both')
                        [void]$node.AppendChild($activation)
                    }
                    [void]$manifest.DocumentElement.AppendChild($node)
                }
                foreach ($server in $fragment.SelectNodes('//m:ProxyStub',$namespaces)) {
                    if ($server.Path -in @('PushNotificationsLongRunningTask.ProxyStub.dll','Microsoft.Windows.Widgets.dll')) { continue }
                    $node = $manifest.CreateElement('asmv3','file','urn:schemas-microsoft-com:asm.v3')
                    $node.SetAttribute('name',$server.Path)
                    $class = $manifest.CreateElement('asmv3','comClass','urn:schemas-microsoft-com:asm.v3')
                    $class.SetAttribute('clsid',"{$($server.ClassId)}")
                    [void]$node.AppendChild($class)
                    foreach ($type in $server.SelectNodes('m:Interface',$namespaces)) {
                        $proxy = $manifest.CreateElement('asmv3','comInterfaceProxyStub','urn:schemas-microsoft-com:asm.v3')
                        $proxy.SetAttribute('name',$type.Name)
                        $proxy.SetAttribute('iid',"{$($type.InterfaceId)}")
                        [void]$node.AppendChild($proxy)
                    }
                    [void]$manifest.DocumentElement.AppendChild($node)
                }
            }
            $winrtManifest = Join-Path $report 'winrt.manifest'
            $manifest.Save($winrtManifest)
            $baseManifest = Join-Path $report 'base.manifest'
            $mt = Join-Path $env:WindowsSdkDir "bin/$($DependencyLock.windowsSdkVersion)/x64/mt.exe"
            $stagedExe = Join-Path $stage 'winui_island_probe.exe'
            Invoke-Checked $mt @('-nologo',"-inputresource:$stagedExe;#1","-out:$baseManifest")
            Invoke-Checked $mt @('-nologo','-manifest',$baseManifest,$winrtManifest,"-outputresource:$stagedExe;#1")
            $priInput = Join-Path $report 'pri-input'
            New-Item -ItemType Directory -Force $priInput | Out-Null
            foreach ($component in @('foundation','interactiveexperiences','winui')) {
                $native = Join-Path $packages "microsoft.windowsappsdk.$component/runtimes-framework/win-x64/native"
                Get-ChildItem -LiteralPath $native -File -Filter '*.pri' | ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $priInput -Force }
            }
            $makepri = Join-Path $env:WindowsSdkDir "bin/$($DependencyLock.windowsSdkVersion)/x64/makepri.exe"
            $priOutput = Join-Path $report 'resources.pri'
            # Merge only native indexes; scanning physical assets as well causes duplicate resources.
            & $makepri new /pr $priInput /cf (Get-ProjectPath 'tools/winui-island-probe/priconfig.xml') /of $priOutput /in WinUIProbe /o *> (Join-Path $report 'makepri.log')
            if ($LASTEXITCODE -ne 0) { throw "MakePRI failed; see $report/makepri.log" }
            Copy-Item -LiteralPath $priOutput -Destination (Join-Path $stage 'resources.pri') -Force
        }
        $licenses = Join-Path $stage 'licenses'
        New-Item -ItemType Directory -Force $licenses | Out-Null
        $lock = Get-Content (Get-ProjectPath 'toolchain/winui-island.lock.json') -Raw | ConvertFrom-Json
        foreach ($package in $lock.packages) {
            $licenseFiles = Get-ChildItem -LiteralPath (Join-Path $packages $package.id.ToLowerInvariant()) -File |
                Where-Object { $_.Name -match 'license|notice' }
            foreach ($file in $licenseFiles) { Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $licenses "$($package.id)-$($file.Name)") -Force }
        }
        foreach ($notice in @('LICENSE','docs/THIRD_PARTY.md','docs/PACKAGING.md')) {
            Copy-Item -LiteralPath (Get-ProjectPath $notice) -Destination $licenses -Force
        }
        if (!$StaticQt) {
            Invoke-Checked (Join-Path $tools.Qt 'bin/windeployqt.exe') @('--release','--verbose','0','--no-translations','--qmldir',(Get-ProjectPath 'tools/winui-island-probe'),'--dir',$stage,(Join-Path $stage 'winui_island_probe.exe'))
        }
        $files = @(Get-ChildItem -LiteralPath $stage -File -Recurse)
        [ordered]@{deployment=$deployment;staticQt=$StaticQt.IsPresent;files=$files.Count;bytes=($files|Measure-Object Length -Sum).Sum} |
            ConvertTo-Json | Set-Content -LiteralPath (Join-Path $report 'deployment-size.json')
        Write-Host "Staged probe: $stage (internal evaluation, not a production release)"
        return
    }
    if ($SelfContained -and !$Staged) { throw 'Self-contained runs require -Staged after an explicit Stage step.' }
    if ($Staged) {
        $exe = Join-Path $stage 'winui_island_probe.exe'
        $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
        foreach ($name in @('QT_PLUGIN_PATH','QT_QPA_PLATFORM_PLUGIN_PATH','QML_IMPORT_PATH','QML2_IMPORT_PATH')) {
            [Environment]::SetEnvironmentVariable($name,$null,'Process')
        }
    }
    $env:QT_SCALE_FACTOR = $Scale
    $env:WINUI_PROBE_CACHE_ROOT = Join-Path $report 'cache'
    $env:QML_DISK_CACHE_PATH = Join-Path $report 'cache/qml'
    $env:TEMP = Join-Path $report 'cache/tmp'
    $env:TMP = $env:TEMP
    New-Item -ItemType Directory -Force $env:TEMP,$env:QML_DISK_CACHE_PATH | Out-Null
    Get-AppxPackage -Name 'Microsoft.WindowsAppRuntime.1.8' | Select-Object Name,Version,Architecture,InstallLocation |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $report 'installed-runtime.json')
    $arguments = @('--report-dir',$report,'--scenario',$Scenario)
    if ($Mode -eq 'Smoke') { $arguments += '--smoke-test' }
    if ($QtOnly) { $arguments += '--qt-only' }
    if ($SelfContained) { $arguments += '--self-contained' }
    Invoke-Checked $exe $arguments
}
