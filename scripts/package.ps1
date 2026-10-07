#requires -Version 7.0
param([ValidateSet('','windows-release','windows-static-release')][string] $Preset = '',
      [ValidateSet('static-exe','directory','single-exe')][string] $Format = 'static-exe')
. "$PSScriptRoot/common.ps1"
. "$PSScriptRoot/portable.ps1"
Invoke-ProjectEnvironment {
    if (-not $Preset) { $Preset = if ($Format -eq 'static-exe') { 'windows-static-release' } else { 'windows-release' } }
    if (($Format -eq 'static-exe') -ne ($Preset -eq 'windows-static-release')) {
        throw 'static-exe requires windows-static-release; directory/single-exe require windows-release.'
    }
    $tools = Initialize-Toolchain -StaticQt:($Format -eq 'static-exe')
    # Build from current sources so deleting out/ is recoverable and an existing
    # executable cannot silently package stale changes. Dependency preparation
    # remains a separate explicit step; configuration/build never downloads.
    $includePrefix = Get-MsvcIncludesPrefix
    Invoke-Checked $tools.CMake @('--preset', $Preset, "-DDANMAKU_MSVC_INCLUDES_PREFIX=$includePrefix")
    Invoke-Checked $tools.CMake @('--build', '--preset', $Preset, '--target', 'danmaku_app', '--parallel')
    $exe = Get-ProjectPath "out/build/$Preset/bin/danmaku_app.exe"
    if (-not (Test-Path -LiteralPath $exe)) { throw 'Application build did not produce the packaging executable.' }
    $timestamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
    if ($Format -eq 'static-exe') {
        $output = Get-ProjectPath "out/packages/LocalDanmaku-static-$timestamp"
        $materials = Get-ProjectPath "out/packages/materials-static-$timestamp"
        New-Item -ItemType Directory -Force $output,$materials | Out-Null
        $destination = Join-Path $output 'LocalDanmaku.exe'
        Copy-Item -LiteralPath $exe -Destination $destination
        # Audit the actual executable before reporting it as static. System DLLs remain normal dependencies.
        $dumpbin = Join-Path $env:VCToolsInstallDir 'bin/Hostx64/x64/dumpbin.exe'
        $imports = & $dumpbin /DEPENDENTS $destination
        if ($LASTEXITCODE -ne 0) { throw 'Cannot audit static executable dependencies.' }
        $imports | Set-Content -LiteralPath (Join-Path $materials 'dependencies.txt') -Encoding utf8
        if (($imports -join "`n") -match '(?im)^\s+(Qt\d.*|MSVCP\d.*|VCRUNTIME\d.*|CONCRT\d.*|dxcompiler|dxil)\.dll\s*$') {
            throw 'Static package still imports a Qt, compiler-runtime or external shader-compiler DLL.'
        }
        foreach ($notice in @('LICENSE','docs/THIRD_PARTY.md','docs/PACKAGING.md')) {
            Copy-Item -LiteralPath (Get-ProjectPath $notice) -Destination $materials
        }
        $staticLock = Get-Content (Get-ProjectPath 'toolchain/qt-static.lock.json') -Raw | ConvertFrom-Json
        foreach ($asset in $staticLock.assets) {
            $source = Get-ProjectPath "$($staticLock.sourceRoot)/$($asset.module)"
            $moduleMaterials = Join-Path $materials $asset.module
            New-Item -ItemType Directory -Force $moduleMaterials | Out-Null
            Copy-Item -LiteralPath (Join-Path $source 'LICENSES') -Destination $moduleMaterials -Recurse
            if (Test-Path -LiteralPath (Join-Path $source 'qt_attribution.json')) {
                Copy-Item -LiteralPath (Join-Path $source 'qt_attribution.json') -Destination $moduleMaterials
            }
            $archive = Get-ProjectPath ".cache/downloads/$($asset.name)"
            if (-not (Test-Path -LiteralPath $archive) -or
                (Get-FileHash -LiteralPath $archive).Hash.ToLowerInvariant() -ne $asset.sha256) {
                throw "Matching Qt source archive missing or changed: $($asset.name)"
            }
            Copy-Item -LiteralPath $archive -Destination $materials
        }
        # Preserve the exact application sources, including authorized uncommitted changes.
        # Archives, generated files and private local configuration are not release sources.
        $applicationSource = Join-Path $materials 'application-source'
        New-Item -ItemType Directory -Force $applicationSource | Out-Null
        $sourcePaths = @('CMakeLists.txt','CMakePresets.json','README.md','LICENSE','AGENTS.md',
            'src','qml','cmake','scripts','toolchain','tests','docs')
        foreach ($relative in $sourcePaths) {
            $original = Get-ProjectPath $relative
            if (Test-Path -LiteralPath $original) { Copy-Item -LiteralPath $original -Destination $applicationSource -Recurse }
        }
        Copy-Item -LiteralPath (Get-ProjectPath 'toolchain/qt-static.lock.json') -Destination $materials
        Copy-Item -LiteralPath (Join-Path $tools.Qt '.installed-static-identity.json') -Destination $materials
        $linkMap = Get-ProjectPath "out/build/$Preset/qml/danmaku_app.map"
        if (-not (Test-Path -LiteralPath $linkMap)) { throw 'Static link map is missing; rebuild the static application.' }
        Copy-Item -LiteralPath $linkMap -Destination $materials
        $sbom = Join-Path $tools.Qt 'sbom'
        if (Test-Path -LiteralPath $sbom) { Copy-Item -LiteralPath $sbom -Destination $materials -Recurse }
        @'
This folder is separate from the portable executable. Preserve it for distribution/relinking review.
The application source and verified Qt source archives match this build; Qt sources were not patched.
Rebuild instructions are in application-source/docs/PACKAGING.md. Place the Qt archives under
.cache/downloads, then prepare-qt-static.ps1 -Offline and build-qt-static.ps1 before building the app.
MSVC and Windows SDK require an explicit .local/toolchain.local.json for your installation.
Qt modifications can be rebuilt and relinked into the application; there is no runtime Qt hash gate
in static-exe. These materials are not a completed component-by-component licensing audit.
'@ | Set-Content -LiteralPath (Join-Path $materials 'RELINKING.txt') -Encoding utf8
        [ordered]@{format='static-exe'; qtVersion=$DependencyLock.qtVersion
            executable=[IO.Path]::GetRelativePath($ProjectRoot,$destination)
            executableBytes=(Get-Item -LiteralPath $destination).Length
            sha256=(Get-FileHash -LiteralPath $destination).Hash.ToLowerInvariant()
            staticQtLockSha256=(Get-FileHash -LiteralPath (Get-ProjectPath 'toolchain/qt-static.lock.json')).Hash
            extraction='none'; data='annotated UTF-8 settings.ini and app.log beside EXE'; cache='cache/ beside EXE'
            relinkingMaterials='Matching application source, verified Qt sources, lockfiles and rebuild scripts in this folder'
            publicDistribution='Development validation; component-license review and clean-machine validation still pending'
        } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $materials 'package-report.json') -Encoding utf8
        Write-Host "Static portable executable: $destination"
        Write-Host "Size: $([Math]::Round((Get-Item -LiteralPath $destination).Length / 1MB, 1)) MiB; no extraction launcher or external Qt/VC++ DLL imports."
        Write-Host "Distribution materials: $materials (review before public release)."
        return
    }
    $stage = Get-ProjectPath ('out/stage/LocalDanmaku-' + $timestamp)
    New-Item -ItemType Directory -Force $stage | Out-Null
    Copy-Item -LiteralPath $exe -Destination $stage
    Invoke-Checked (Join-Path $tools.Qt 'bin/windeployqt.exe') @('--release','--no-compiler-runtime','--verbose','0',
        '--skip-plugin-types','qmltooling,generic,networkinformation',
        '--exclude-plugins','qdirect2d,qminimal,qopensslbackend', '--include-plugins','qoffscreen,qschannelbackend',
        '--qmldir',(Get-ProjectPath 'qml'),(Join-Path $stage 'danmaku_app.exe'))
    # Qt's deployment tool may still copy optional TLS backends. Use Windows
    # Schannel only; never distribute a nonfunctional OpenSSL backend/runtime.
    $unusedTlsBackend = Join-Path $stage 'tls/qopensslbackend.dll'
    if (Test-Path -LiteralPath $unusedTlsBackend) {
        if (-not ([IO.Path]::GetFullPath($unusedTlsBackend)).StartsWith($stage + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
            throw 'TLS deployment cleanup escaped its new stage directory.'
        }
        Remove-Item -LiteralPath $unusedTlsBackend
    }
    # Optional Controls imports make windeployqt copy every style. Keep Fluent,
    # its official Fusion -> Basic fallback, and the standard dialogs' dependencies.
    foreach ($style in @('Imagine','Material','Universal','Windows')) {
        foreach ($relative in @("qml/QtQuick/Controls/$style", "qml/QtQuick/Dialogs/quickimpl/qml/+$style")) {
            $target = Get-ProjectPath ([IO.Path]::GetRelativePath($ProjectRoot, (Join-Path $stage $relative)))
            if (-not $target.StartsWith($stage + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
                throw 'Deployment cleanup escaped its new stage directory.'
            }
            if (Test-Path -LiteralPath $target) { Remove-Item -LiteralPath $target -Recurse -Force }
        }
        foreach ($name in @("Qt6QuickControls2$style.dll", "Qt6QuickControls2${style}StyleImpl.dll")) {
            $target = Get-ProjectPath ([IO.Path]::GetRelativePath($ProjectRoot, (Join-Path $stage $name)))
            if (Test-Path -LiteralPath $target) { Remove-Item -LiteralPath $target -Force }
        }
    }
    Copy-Item -LiteralPath (Get-ProjectPath 'LICENSE') -Destination $stage
    Copy-Item -LiteralPath (Get-ProjectPath 'docs/THIRD_PARTY.md') -Destination $stage
    Copy-Item -LiteralPath (Get-ProjectPath 'docs/PACKAGING.md') -Destination $stage
    $licenses = Join-Path $tools.Qt 'LICENSES'
    if (Test-Path -LiteralPath $licenses) { Copy-Item -LiteralPath $licenses -Destination (Join-Path $stage 'qt-licenses') -Recurse }
    Write-Host "Development deployment created: $stage"
    if ($Format -eq 'single-exe') { New-PortableExecutable -Stage $stage -Tools $tools -Timestamp $timestamp }
    Write-Host 'Requires the official Microsoft Visual C++ 2015-2022 x64 runtime. Review THIRD_PARTY.md before public distribution.'
}
