#requires -Version 7.0
param([ValidateSet('windows-debug','windows-release','windows-static-release')][string] $Preset = 'windows-debug',
      [switch] $SmokeTest, [string] $BuildDirectory = '', [switch] $NoBuild)
. "$PSScriptRoot/common.ps1"
. "$PSScriptRoot/deployment.ps1"

function Stop-RunningApplication([string] $Executable) {
    # Match the full path: another checkout or a released copy must keep running.
    foreach ($process in [Diagnostics.Process]::GetProcessesByName([IO.Path]::GetFileNameWithoutExtension($Executable))) {
        try {
            if ($process.HasExited) { continue }
            $processPath = $process.MainModule.FileName
            if (-not [string]::Equals($processPath, $Executable, [StringComparison]::OrdinalIgnoreCase)) { continue }
            Write-Host "Closing previous instance (PID $($process.Id)): $Executable"
            [void] $process.CloseMainWindow()
            if (-not $process.WaitForExit(5000)) {
                Write-Warning "Previous instance did not close within 5 seconds; terminating PID $($process.Id)."
                $process.Kill()
                if (-not $process.WaitForExit(5000)) {
                    throw "Previous instance (PID $($process.Id)) has not exited; refusing to build or launch."
                }
            }
        } catch {
            # The process can exit between discovery, path lookup and shutdown.
            if (-not $process.HasExited) { throw }
        } finally {
            $process.Dispose()
        }
    }
}

Invoke-ProjectEnvironment {
    $build = Get-ProjectPath $(if ($BuildDirectory) { $BuildDirectory } else { "out/build/$Preset" })
    $buildRoot = Get-ProjectPath 'out/build'
    if (-not $build.StartsWith($buildRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'BuildDirectory must name a build directory under project out/build.'
    }
    $defaultBuild = Get-ProjectPath "out/build/$Preset"
    $runtime = if ($build -eq $defaultBuild) { Get-ProjectPath "out/run/$Preset" } else {
        $relativeBuild = [IO.Path]::GetRelativePath($buildRoot, $build)
        Get-ProjectPath "out/run/custom/$Preset/$relativeBuild"
    }
    Assert-DeploymentDirectory $runtime
    $exe = Join-Path $build 'bin/DanmaX.exe'
    if ($NoBuild -and -not (Test-Path -LiteralPath $exe -PathType Leaf)) {
        throw 'No executable found. Run without -NoBuild to build the application first.'
    }
    if ($NoBuild) {
        $cacheText = Get-Content -LiteralPath (Join-Path $build 'CMakeCache.txt') -Raw
        $buildType = if ($Preset -eq 'windows-debug') { 'Debug' } else { 'Release' }
        $staticQt = if ($Preset -eq 'windows-static-release') { '(?:ON|TRUE|1|YES)' } else { '(?:OFF|FALSE|0|NO)' }
        if ($cacheText -notmatch "(?m)^CMAKE_BUILD_TYPE:STRING=$buildType\r?$" -or
            $cacheText -notmatch "(?m)^DANMAKU_STATIC_QT:BOOL=$staticQt\r?$") {
            throw 'Existing build does not match the selected preset. Run without -NoBuild to configure it first.'
        }
    }
    $tools = Initialize-Toolchain -StaticQt:($Preset -eq 'windows-static-release')
    Stop-RunningApplication (Join-Path $runtime 'DanmaX.exe')
    # Also release an instance launched directly from the build tree by older scripts.
    Stop-RunningApplication $exe
    if (-not $NoBuild) {
        $includePrefix = Get-MsvcIncludesPrefix
        # CMake/Ninja track source and CMakeLists changes. Avoid an unconditional
        # configure, which rewrites QWindowKit headers even when inputs are unchanged.
        $configurationKey = (@($ProjectRoot, $Preset, $includePrefix) + @(
            foreach ($inputFile in @('CMakePresets.json', 'toolchain/dependencies.lock.json',
                                     'toolchain/qt-static.lock.json', '.local/toolchain.local.json')) {
                (Get-FileHash -LiteralPath (Get-ProjectPath $inputFile) -Algorithm SHA256).Hash
            }
        )) -join "`n"
        $cache = Join-Path $build 'CMakeCache.txt'
        $state = Join-Path $build 'run-configure.state'
        $cacheHash = if (Test-Path -LiteralPath $cache -PathType Leaf) { (Get-FileHash -LiteralPath $cache -Algorithm SHA256).Hash } else { '' }
        $expectedState = $configurationKey + "`n" + $cacheHash
        if (-not (Test-Path -LiteralPath (Join-Path $build 'build.ninja') -PathType Leaf) -or
            -not $cacheHash -or -not (Test-Path -LiteralPath $state -PathType Leaf) -or
            [IO.File]::ReadAllText($state) -ne $expectedState) {
            Write-Host "Configuring $Preset in $build"
            Invoke-Checked $tools.CMake @('--preset', $Preset, '-B', $build, "-DDANMAKU_MSVC_INCLUDES_PREFIX=$includePrefix")
            $cacheHash = (Get-FileHash -LiteralPath $cache -Algorithm SHA256).Hash
            [IO.File]::WriteAllText($state, $configurationKey + "`n" + $cacheHash)
        } else {
            Write-Host "Using existing configuration: $build"
        }
        Write-Host 'Building application (incremental)...'
        Invoke-Checked $tools.CMake @('--build', $build, '--target', 'danmaku_app', '--parallel')
    } else {
        Write-Warning 'Skipping build (-NoBuild); source changes may not be included.'
    }
    if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) { throw "Application executable not found: $exe" }
    $runtimeExe = Sync-DevelopmentRuntime $exe $runtime $build $tools $Preset
    $env:QML_DISK_CACHE_PATH = Join-Path $runtime 'cache'
    $env:TEMP = Join-Path $env:QML_DISK_CACHE_PATH 'tmp'
    $env:TMP = $env:TEMP
    New-Item -ItemType Directory -Force $env:TEMP | Out-Null
    foreach ($name in @('QTDIR','QT_PLUGIN_PATH','QT_QPA_PLATFORM_PLUGIN_PATH','QML_IMPORT_PATH','QML2_IMPORT_PATH')) {
        [Environment]::SetEnvironmentVariable($name, $null, 'Process')
    }
    $env:PATH = "${env:SystemRoot}\System32;${env:SystemRoot}"
    Set-Location $runtime
    Write-Host "Starting: $runtimeExe"
    Write-Host "Settings/logs: $runtime; cache: $env:QML_DISK_CACHE_PATH"
    $arguments = @()
    if ($SmokeTest) { $arguments += '--smoke-test' }
    Invoke-Checked $runtimeExe $arguments
}
