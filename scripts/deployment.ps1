# Shared deployment rules for development runs and dynamic packages.
# Dot-source common.ps1 before this file.

function Assert-DeploymentDirectory([string] $Directory) {
    $directoryPath = Get-ProjectPath $Directory
    $allowed = $false
    foreach ($relative in @('out/run', 'out/stage')) {
        $base = Get-ProjectPath $relative
        if ($directoryPath.StartsWith($base + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { $allowed = $true }
    }
    if (-not $allowed) { throw "Deployment directory must be below out/run or out/stage: $Directory" }
    # Reject junctions both above and inside the destination before any writes/removals.
    for ($current = $directoryPath; $current -ne $ProjectRoot; $current = Split-Path $current) {
        if ((Test-Path -LiteralPath $current) -and
            ((Get-Item -LiteralPath $current -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw "Deployment path contains a reparse point: $current"
        }
    }
    if (Test-Path -LiteralPath $directoryPath) {
        foreach ($entry in Get-ChildItem -LiteralPath $directoryPath -Recurse -Force) {
            if ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Deployment contains a reparse point: $($entry.FullName)" }
        }
    }
}

function Invoke-QtDeployment([string] $Directory, [hashtable] $Tools, [switch] $Debug) {
    Assert-DeploymentDirectory $Directory
    $mode = if ($Debug) { '--debug' } else { '--release' }
    $deployArguments = @($mode, '--no-compiler-runtime', '--verbose', '0', '--json',
        '--qtpaths', (Join-Path $Tools.Qt 'bin/qtpaths.exe'),
        '--skip-plugin-types', 'qmltooling,generic,networkinformation',
        '--exclude-plugins', 'qdirect2d,qminimal,qopensslbackend', '--include-plugins', 'qoffscreen,qschannelbackend,qsvg,qsvgicon',
        '--qmldir', (Get-ProjectPath 'qml'), (Join-Path $Directory 'DanmaX.exe'))
    $json = & (Join-Path $Tools.Qt 'bin/windeployqt.exe') @deployArguments
    if ($LASTEXITCODE -ne 0) { throw "Qt deployment failed with exit code $LASTEXITCODE" }
    $deployed = ($json -join "`n") | ConvertFrom-Json
    # Keep the official Fluent -> Fusion -> Basic fallback and standard dialogs.
    $suffix = if ($Debug) { 'd' } else { '' }
    $unwanted = @("tls/qopensslbackend$suffix.dll")
    foreach ($style in @('Imagine', 'Material', 'Universal', 'Windows')) {
        $unwanted += "qml/QtQuick/Controls/$style", "qml/QtQuick/Dialogs/quickimpl/qml/+$style",
            "Qt6QuickControls2$style$suffix.dll", "Qt6QuickControls2${style}StyleImpl$suffix.dll"
    }
    foreach ($relative in $unwanted) {
        $target = [IO.Path]::GetFullPath((Join-Path $Directory $relative))
        if (-not $target.StartsWith($Directory + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Deployment cleanup escaped its directory: $target"
        }
        if (Test-Path -LiteralPath $target) { Remove-Item -LiteralPath $target -Recurse -Force }
    }
    if (Get-ChildItem -LiteralPath $Directory -Recurse -Filter FluentIcons.ttf) { throw 'Bundled Microsoft icon font must not be distributed.' }
    # Relative paths keep DLLs, platform plugins and QML imports local when moved.
    [IO.File]::WriteAllText((Join-Path $Directory 'qt.conf'), "[Paths]`nPrefix=.`nPlugins=.`nQmlImports=qml`nTranslations=translations`n")
    foreach ($file in $deployed.files) {
        $target = [IO.Path]::GetFullPath((Join-Path $file.target ([IO.Path]::GetFileName($file.source))))
        if (-not $target.StartsWith($Directory + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Qt deployment reported an unexpected destination: $target"
        }
        if (Test-Path -LiteralPath $target -PathType Leaf) { $target }
    }
    Join-Path $Directory 'qt.conf'
}

function Sync-DevelopmentRuntime([string] $Executable, [string] $Directory, [string] $BuildDirectory,
                                 [hashtable] $Tools, [string] $Preset) {
    Assert-DeploymentDirectory $Directory
    $destination = Join-Path $Directory 'DanmaX.exe'
    $identity = (@($ProjectRoot, $Directory, $Preset, $Tools.Qt) + @(
        foreach ($inputFile in @($Executable, (Get-ProjectPath 'scripts/deployment.ps1'),
            (Get-ProjectPath 'toolchain/dependencies.lock.json'), (Get-ProjectPath 'LICENSE'),
            (Get-ProjectPath 'docs/THIRD_PARTY.md'), (Get-ProjectPath 'docs/PACKAGING.md'),
            (Get-ProjectPath 'resources/fluent/LICENSE'))) {
            (Get-FileHash -LiteralPath $inputFile -Algorithm SHA256).Hash
        }
    )) -join "`n"
    $stateFile = Join-Path $BuildDirectory 'run-deployment.json'
    $valid = $false
    $repair = Test-Path -LiteralPath $destination -PathType Leaf
    if (Test-Path -LiteralPath $stateFile -PathType Leaf) {
        try {
            $state = Get-Content -LiteralPath $stateFile -Raw | ConvertFrom-Json
            $valid = $state.identity -ceq $identity
            $repair = $false
            foreach ($file in $state.files) {
                $path = [IO.Path]::GetFullPath((Join-Path $Directory $file.path))
                if (-not $path.StartsWith($Directory + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase) -or
                    -not (Test-Path -LiteralPath $path -PathType Leaf)) { $valid = $false; $repair = $true; break }
                $item = Get-Item -LiteralPath $path
                if ($item.Length -ne $file.length -or $item.LastWriteTimeUtc.Ticks -ne $file.modified) { $valid = $false; $repair = $true; break }
            }
        } catch { $valid = $false; $repair = $true }
    }
    if ($valid) {
        Write-Host "Using existing runtime: $Directory"
        return $destination
    }
    Write-Host "Updating runtime: $Directory"
    New-Item -ItemType Directory -Force $Directory | Out-Null
    Copy-Item -LiteralPath $Executable -Destination $destination -Force
    $managed = @($destination)
    if ($Preset -ne 'windows-static-release') {
        if ($repair) {
            # Forced in-place deployment can fail on icuuc.dll even after the
            # application has exited. Repair from a fresh, disposable stage.
            $stage = Get-ProjectPath ('out/stage/runtime-repair-' + [guid]::NewGuid().ToString('N'))
            Assert-DeploymentDirectory $stage
            New-Item -ItemType Directory -Force $stage | Out-Null
            Copy-Item -LiteralPath $Executable -Destination (Join-Path $stage 'DanmaX.exe')
            foreach ($file in @(Invoke-QtDeployment $stage $Tools -Debug:($Preset -eq 'windows-debug'))) {
                $target = Join-Path $Directory ([IO.Path]::GetRelativePath($stage, $file))
                New-Item -ItemType Directory -Force (Split-Path $target) | Out-Null
                Copy-Item -LiteralPath $file -Destination $target -Force
                $managed += $target
            }
            Assert-DeploymentDirectory $stage
            Remove-Item -LiteralPath $stage -Recurse -Force
        } else {
            $managed += @(Invoke-QtDeployment $Directory $Tools -Debug:($Preset -eq 'windows-debug'))
        }
    }
    # Keep the application's debug symbols beside its deployed EXE for debugging.
    $pdb = [IO.Path]::ChangeExtension($Executable, '.pdb')
    if (Test-Path -LiteralPath $pdb -PathType Leaf) {
        $targetPdb = Join-Path $Directory 'DanmaX.pdb'
        Copy-Item -LiteralPath $pdb -Destination $targetPdb -Force
        $managed += $targetPdb
    }
    foreach ($notice in @('LICENSE', 'docs/THIRD_PARTY.md', 'docs/PACKAGING.md', 'resources/fluent/LICENSE')) {
        $name = if ($notice -eq 'resources/fluent/LICENSE') { 'Fluent-System-Icons-LICENSE.txt' } else { Split-Path $notice -Leaf }
        $target = Join-Path $Directory $name
        Copy-Item -LiteralPath (Get-ProjectPath $notice) -Destination $target -Force
        $managed += $target
    }
    $files = @(foreach ($path in $managed | Sort-Object -Unique) {
        $item = Get-Item -LiteralPath $path
        @{ path = [IO.Path]::GetRelativePath($Directory, $path); length = $item.Length; modified = $item.LastWriteTimeUtc.Ticks }
    })
    # State is outside the runtime and is only committed after successful deployment.
    @{ identity = $identity; files = $files } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $stateFile -Encoding utf8
    return $destination
}
