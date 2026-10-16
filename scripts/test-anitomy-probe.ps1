#requires -Version 7.0
param([ValidateSet('Debug','Release')][string] $Configuration = 'Release', [string] $BuildDirectory = '')
. "$PSScriptRoot/common.ps1"
Invoke-ProjectEnvironment {
    $tools = Initialize-Toolchain
    $build = Get-ProjectPath $(if ($BuildDirectory) { $BuildDirectory } else { "out/build/anitomy-evaluation-$($Configuration.ToLowerInvariant())" })
    if (!$build.StartsWith((Get-ProjectPath 'out/build') + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'BuildDirectory must remain under project out/build.'
    }
    $report = Get-ProjectPath "out/validation/anitomy-probe/$($Configuration.ToLowerInvariant())"
    New-Item -ItemType Directory -Force $report | Out-Null
    $compiler = Join-Path $env:VCToolsInstallDir 'bin/Hostx64/x64/cl.exe'
    $identity = [ordered]@{
        projectRoot = $ProjectRoot
        compiler = $compiler
        qt = $tools.Qt
        windowsSdk = $env:WindowsSdkDir
        windowsSdkVersion = $env:WindowsSDKVersion
        toolchainLockSha256 = (Get-FileHash -LiteralPath (Get-ProjectPath 'toolchain/dependencies.lock.json')).Hash
        anitomyLockSha256 = (Get-FileHash -LiteralPath (Get-ProjectPath 'toolchain/anitomy.lock.json')).Hash
    } | ConvertTo-Json -Compress
    $identityPath = Join-Path $build '.probe-identity.json'
    if (Test-Path -LiteralPath (Join-Path $build 'CMakeCache.txt')) {
        if (!(Test-Path -LiteralPath $identityPath) -or (Get-Content -LiteralPath $identityPath -Raw).Trim() -ne $identity) {
            throw "Probe location/toolchain changed. Use a fresh build directory: $build"
        }
    }
    New-Item -ItemType Directory -Force $build | Out-Null
    Set-Content -LiteralPath $identityPath -Value $identity -Encoding utf8
    $prefix = Get-MsvcIncludesPrefix
    Invoke-Checked $tools.CMake @('-S', $ProjectRoot, '-B', $build, '-G', 'Ninja',
        '-DDANMAKU_BUILD_APP=OFF', '-DDANMAKU_BUILD_ANITOMY_PROBE=ON', '-DBUILD_TESTING=ON',
        "-DCMAKE_BUILD_TYPE=$Configuration", "-DCMAKE_MAKE_PROGRAM=$($tools.Ninja)",
        "-DCMAKE_CXX_COMPILER=$compiler", "-DCMAKE_PREFIX_PATH=$($tools.Qt)",
        "-DDANMAKU_MSVC_INCLUDES_PREFIX=$prefix", '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON')
    Invoke-Checked $tools.CMake @('--build', $build, '--parallel')
    Set-Content -LiteralPath (Join-Path $report 'build-identity.json') -Value $identity -Encoding utf8
    $env:DANMAX_ANITOMY_REPORT = Join-Path $report 'comparison.json'
    Invoke-Checked $tools.CTest @('--test-dir', $build, '--output-on-failure', '--output-junit', (Join-Path $report 'ctest.xml'))
    Copy-Item -LiteralPath (Join-Path $build 'tools/anitomy-probe/qtest.txt') -Destination (Join-Path $report 'qtest.txt')
    Write-Host "Probe report: $report"
}
