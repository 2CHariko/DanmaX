#requires -Version 7.0
param([string] $Player = '', [switch] $BuildOnly, [switch] $Ipc)
. "$PSScriptRoot/common.ps1"
Invoke-ProjectEnvironment {
    $tools = Initialize-Toolchain
    $build = Get-ProjectPath 'out/build/potplayer-lifecycle-probe'
    $report = Get-ProjectPath ('out/validation/potplayer-' + $(if ($Ipc) {'ipc-'} else {'lifecycle-'}) + (Get-Date -Format 'yyyyMMdd-HHmmss'))
    $compiler = Join-Path $env:VCToolsInstallDir 'bin/Hostx64/x64/cl.exe'
    $identity = [ordered]@{root=$ProjectRoot;compiler=$compiler;qt=$tools.Qt;sdk=$env:WindowsSdkDir;sdkVersion=$env:WindowsSDKVersion} | ConvertTo-Json -Compress
    $identityPath = Join-Path $build '.probe-identity.json'
    if ((Test-Path -LiteralPath (Join-Path $build 'CMakeCache.txt')) -and
        (-not (Test-Path -LiteralPath $identityPath) -or (Get-Content -LiteralPath $identityPath -Raw).Trim() -ne $identity)) {
        throw 'Probe location/toolchain changed; use a fresh project build directory.'
    }
    New-Item -ItemType Directory -Force $build | Out-Null
    $includePrefix = Get-MsvcIncludesPrefix
    Invoke-Checked $tools.CMake @('-S',(Get-ProjectPath 'tools/potplayer-lifecycle-probe'),'-B',$build,'-G','Ninja',
        '-DCMAKE_BUILD_TYPE=Release',"-DCMAKE_MAKE_PROGRAM=$($tools.Ninja)","-DCMAKE_CXX_COMPILER=$compiler",
        "-DDANMAKU_MSVC_INCLUDES_PREFIX=$includePrefix")
    Set-Content -LiteralPath $identityPath -Value $identity -Encoding utf8
    Invoke-Checked $tools.CMake @('--build',$build,'--parallel','4')
    if ($BuildOnly) { return }
    if ($Player -eq '') {
        $candidateValues = foreach ($hive in @('HKCU:','HKLM:')) {
            foreach ($view in @('Software','Software/WOW6432Node')) {
                $key = "$hive/$view/Microsoft/Windows/CurrentVersion/App Paths/PotPlayerMini64.exe"
                if (Test-Path -LiteralPath $key) {
                    $value = (Get-Item -LiteralPath $key).GetValue('')
                    if ($value) { $path = $value.Trim().Trim('"'); if (Test-Path -LiteralPath $path -PathType Leaf) { [IO.Path]::GetFullPath($path) } }
                }
            }
        }
        $candidates = @($candidateValues | Sort-Object -Unique)
        if ($candidates.Count -ne 1) { throw 'Specify -Player explicitly; no unique registered PotPlayer was found.' }
        $Player = $candidates[0]
    }
    if ((Split-Path $Player -Leaf) -ne 'PotPlayerMini64.exe' -or -not (Test-Path -LiteralPath $Player -PathType Leaf)) { throw 'Expected an existing PotPlayerMini64.exe.' }
    New-Item -ItemType Directory -Force $report | Out-Null
    [ordered]@{toolchain=($identity | ConvertFrom-Json);player=[IO.Path]::GetFullPath($Player);version=(Get-Item -LiteralPath $Player).VersionInfo.FileVersion;
        playerDllVersion=(Get-Item -LiteralPath (Join-Path (Split-Path $Player) 'PotPlayer64.dll')).VersionInfo.FileVersion;
        executableSha256=(Get-FileHash -LiteralPath $Player).Hash;commandLineHelpSha256=(Get-FileHash -LiteralPath (Join-Path (Split-Path $Player) 'CmdLine64.txt')).Hash} |
        ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $report 'environment.json') -Encoding utf8
    Write-Host "Probe evidence: $report"
    $probeArguments = @($Player, $report)
    if ($Ipc) { $probeArguments += '--ipc' }
    Invoke-Checked (Join-Path $build 'potplayer_lifecycle_probe.exe') $probeArguments
}
