#requires -Version 7.0
param([ValidateRange(1,32)][int] $Jobs = 8, [switch] $Reconfigure)
. "$PSScriptRoot/common.ps1"
Invoke-ProjectEnvironment {
    $tools = Initialize-Toolchain -CoreOnly
    $lockFile = Get-ProjectPath 'toolchain/qt-static.lock.json'
    $lock = Get-Content $lockFile -Raw | ConvertFrom-Json
    $lockHash = (Get-FileHash -LiteralPath $lockFile).Hash
    $prefix = Get-ProjectPath $lock.destination
    $marker = Join-Path $prefix '.installed-static-lock'
    $identity = Get-StaticQtBuildIdentity
    $identityFile = Join-Path $prefix '.installed-static-identity.json'
    if (Test-Path -LiteralPath $identityFile) {
        $oldIdentity = Get-Content $identityFile -Raw | ConvertFrom-Json
        $newIdentity = $identity | ConvertFrom-Json
        foreach ($key in @('projectRoot','dependencyLockSha256','compiler','windowsSdk','windowsSdkVersion')) {
            if ($oldIdentity.$key -ne $newIdentity.$key) {
                throw 'Static Qt toolchain identity changed; use a fresh SDK/build directory.'
            }
        }
    }
    foreach ($asset in $lock.assets) {
        $source = Get-ProjectPath "$($lock.sourceRoot)/$($asset.module)"
        $moduleBuild = if ($asset.PSObject.Properties['buildDirectory']) { $asset.buildDirectory } else { "$($lock.buildRoot)/$($asset.module)" }
        $cacheFile = Get-ProjectPath "$moduleBuild/CMakeCache.txt"
        if (Test-Path -LiteralPath $cacheFile) {
            $cache = Get-Content -LiteralPath $cacheFile -Raw
            $sourceForward = $source.Replace('\','/')
            $compilerForward = (Join-Path $env:VCToolsInstallDir 'bin/Hostx64/x64/cl.exe').Replace('\','/')
            if ($cache -notmatch ('(?m)^CMAKE_HOME_DIRECTORY:INTERNAL=' + [regex]::Escape($sourceForward) + '\r?$') -or
                $cache -notmatch ('(?m)^CMAKE_CXX_COMPILER:FILEPATH=' + [regex]::Escape($compilerForward) + '\r?$') -or
                $cache -notmatch ('(?m)^CMAKE_INSTALL_PREFIX:PATH=' + [regex]::Escape($prefix.Replace('\','/')) + '\r?$')) {
                throw 'Moved source/compiler/prefix detected; configure static Qt in a new build directory.'
            }
        }
    }
    if ((Test-Path -LiteralPath $marker) -and -not $Reconfigure) {
        if ((Get-Content $marker -Raw).Trim() -ne $lockHash) { throw 'Static Qt configuration changed; use a fresh tool/build directory.' }
        $identityFile = Join-Path $prefix '.installed-static-identity.json'
        if ((Test-Path -LiteralPath $identityFile) -and (Get-Content $identityFile -Raw).Trim() -ne $identity) {
            throw 'Static Qt toolchain identity changed; use a fresh tool/build directory.'
        }
        Set-Content -LiteralPath $identityFile -Value $identity -Encoding utf8
        Write-Host "Static Qt ready: $prefix"
        return
    }
    $includePrefix = Get-MsvcIncludesPrefix
    foreach ($asset in $lock.assets) {
        $source = Get-ProjectPath "$($lock.sourceRoot)/$($asset.module)"
        $sourceMarker = Join-Path $source '.prepared-sha256'
        if (-not (Test-Path -LiteralPath $sourceMarker) -or (Get-Content $sourceMarker -Raw).Trim() -ne $asset.sha256) {
            throw 'Run scripts/prepare-qt-static.ps1 first. Static Qt builds never download dependencies.'
        }
        $moduleBuild = if ($asset.PSObject.Properties['buildDirectory']) { $asset.buildDirectory } else { "$($lock.buildRoot)/$($asset.module)" }
        $build = Get-ProjectPath $moduleBuild
        $buildMarker = Join-Path $build '.configured-lock'
        if ((Test-Path -LiteralPath $build) -and (-not (Test-Path -LiteralPath $buildMarker) -or
            (Get-Content $buildMarker -Raw).Trim() -ne $lockHash)) {
            $cacheFile = Join-Path $build 'CMakeCache.txt'
            if (-not $Reconfigure -or -not (Test-Path -LiteralPath $cacheFile)) {
                throw "Unverified/outdated static Qt build: $build. Feature-only changes require explicit -Reconfigure."
            }
            $cache = Get-Content -LiteralPath $cacheFile -Raw
            $sourceForward = $source.Replace('\','/')
            $compilerForward = (Join-Path $env:VCToolsInstallDir 'bin/Hostx64/x64/cl.exe').Replace('\','/')
            if ($cache -notmatch ('(?m)^CMAKE_HOME_DIRECTORY:INTERNAL=' + [regex]::Escape($sourceForward) + '\r?$') -or
                $cache -notmatch ('(?m)^CMAKE_CXX_COMPILER:FILEPATH=' + [regex]::Escape($compilerForward) + '\r?$')) {
                throw 'Moved source/compiler detected; configure static Qt in a new build directory.'
            }
        }
        New-Item -ItemType Directory -Force $build | Out-Null
        Set-Content -LiteralPath $buildMarker -Value $lockHash -Encoding ascii
        $arguments = @('-S',$source,'-B',$build,'-G','Ninja',"-DCMAKE_MAKE_PROGRAM=$($tools.Ninja)",
            "-DCMAKE_INSTALL_PREFIX=$prefix", "-DCMAKE_PREFIX_PATH=$prefix",
            "-DCMAKE_PROJECT_INCLUDE=$(Get-ProjectPath 'cmake/MsvcIncludesPrefix.cmake')",
            "-DDANMAKU_MSVC_INCLUDES_PREFIX=$includePrefix")
        if ($asset.module -eq 'qtbase') { $arguments += $lock.configure }
        else { $arguments += @('-DCMAKE_BUILD_TYPE=Release','-DQT_BUILD_TESTS=OFF','-DQT_BUILD_EXAMPLES=OFF',
            '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded') }
        if ($asset.module -eq 'qtdeclarative') { $arguments += $lock.declarativeConfigure }
        Write-Host "Building static Qt module: $($asset.module)"
        Invoke-Checked $tools.CMake $arguments
        Invoke-Checked $tools.CMake @('--build',$build,'--parallel',"$Jobs")
        Invoke-Checked $tools.CMake @('--install',$build)
    }
    Set-Content -LiteralPath $marker -Value $lockHash -Encoding ascii
    Set-Content -LiteralPath (Join-Path $prefix '.installed-static-identity.json') -Value $identity -Encoding utf8
    Write-Host "Static Qt installed: $prefix"
}
