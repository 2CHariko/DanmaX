#requires -Version 7.0
param([Parameter(Mandatory)][string] $Video, [switch] $BuildOnly, [switch] $NoBuild, [switch] $KeepOpen,
      [ValidatePattern('^[a-zA-Z0-9-]+$')][string] $RunName = (Get-Date -Format 'yyyyMMdd-HHmmss'),
      [ValidateRange(1,3)][double] $Scale = 1)
. "$PSScriptRoot/common.ps1"
Invoke-ProjectEnvironment {
    $tools = Initialize-Toolchain
    $lockPath = Get-ProjectPath 'toolchain/libvlc.lock.json'
    $lock = Get-Content -LiteralPath $lockPath -Raw | ConvertFrom-Json
    $deps = Get-ProjectPath $lock.destination
    $marker = Join-Path $deps '.prepared-lock'
    if (-not (Test-Path -LiteralPath $marker) -or (Get-Content -LiteralPath $marker -Raw).Trim() -ne (Get-FileHash -LiteralPath $lockPath).Hash) {
        throw 'Run scripts/prepare-libvlc-probe.ps1 explicitly before building/running.'
    }
    $build = Get-ProjectPath 'out/build/libvlc-probe-release'
    $compiler = Join-Path $env:VCToolsInstallDir 'bin/Hostx64/x64/cl.exe'
    $identity = [ordered]@{root=$ProjectRoot;compiler=$compiler;qt=$tools.Qt;sdk=$env:WindowsSdkDir;sdkVersion=$env:WindowsSDKVersion} | ConvertTo-Json -Compress
    $identityPath = Join-Path $build '.probe-identity.json'
    if ((Test-Path -LiteralPath (Join-Path $build 'CMakeCache.txt')) -and
        (-not (Test-Path -LiteralPath $identityPath) -or (Get-Content -LiteralPath $identityPath -Raw).Trim() -ne $identity)) {
        throw 'Probe location/toolchain changed; use a fresh project build directory.'
    }
    if (-not $NoBuild) {
        New-Item -ItemType Directory -Force $build | Out-Null
        $includePrefix = Get-MsvcIncludesPrefix
        Invoke-Checked $tools.CMake @('-S',(Get-ProjectPath 'tools/libvlc-probe'),'-B',$build,'-G','Ninja',
            '-DCMAKE_BUILD_TYPE=Release',"-DCMAKE_MAKE_PROGRAM=$($tools.Ninja)","-DCMAKE_CXX_COMPILER=$compiler",
            "-DDANMAKU_MSVC_INCLUDES_PREFIX=$includePrefix")
        Set-Content -LiteralPath $identityPath -Value $identity -Encoding utf8
        Invoke-Checked $tools.CMake @('--build',$build,'--parallel','4')
    }
    if ($BuildOnly) { return }
    $mediaPath = (Get-Item -LiteralPath $Video).FullName
    $report = Get-ProjectPath "out/validation/libvlc-probe/$RunName"
    if (Test-Path -LiteralPath (Join-Path $report 'result.json')) { throw 'Choose a new RunName to preserve prior evidence.' }
    New-Item -ItemType Directory -Force $report | Out-Null
    $library = Join-Path $deps "$($lock.sourceDirectory)/libvlc.dll"
    $executable = Join-Path $build 'libvlc_probe.exe'
    [ordered]@{toolchain=($identity | ConvertFrom-Json);libvlcLockSha256=(Get-FileHash -LiteralPath $lockPath).Hash;
        libvlcSha256=(Get-FileHash -LiteralPath $library).Hash;executableSha256=(Get-FileHash -LiteralPath $executable).Hash;
        scale=$Scale;video=$mediaPath;videoBytes=(Get-Item -LiteralPath $mediaPath).Length;
        gpu=@(Get-CimInstance Win32_VideoController | Select-Object Name,DriverVersion);
        cpu=@(Get-CimInstance Win32_Processor | Select-Object Name,NumberOfLogicalProcessors)} |
        ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $report 'environment.json') -Encoding utf8
    $env:QT_SCALE_FACTOR = [string]$Scale
    $env:QT_QUICK_CONTROLS_STYLE = 'FluentWinUI3'
    $env:QT_QUICK_CONTROLS_FALLBACK_STYLE = 'Fusion'
    $env:QSG_INFO = '1'
    $env:VLC_PLUGIN_PATH = Join-Path $deps "$($lock.sourceDirectory)/plugins"
    $env:DANMAX_PROBE_QML_WARNINGS = '0'
    foreach ($name in @('APPDATA','LOCALAPPDATA')) {
        $cache = Join-Path $report "runtime/$name"
        New-Item -ItemType Directory -Force $cache | Out-Null
        [Environment]::SetEnvironmentVariable($name,$cache,'Process')
    }
    $arguments = @('--library',$library,'--video',$mediaPath,'--report',$report)
    if ($KeepOpen) { $arguments += '--keep-open' }
    Write-Host "Probe evidence: $report"
    if ($KeepOpen) {
        $start = [Diagnostics.ProcessStartInfo]::new($executable)
        $start.UseShellExecute = $false
        foreach ($argument in $arguments) { $start.ArgumentList.Add($argument) }
        $process = [Diagnostics.Process]::Start($start)
        [ordered]@{pid=$process.Id;executable=$executable;startedUtc=$process.StartTime.ToUniversalTime().ToString('o')} |
            ConvertTo-Json | Set-Content -LiteralPath (Join-Path $report 'process.json') -Encoding utf8
        $process.Dispose()
    } else {
        & $executable @arguments 2>&1 | Tee-Object -FilePath (Join-Path $report 'console.log') | Out-Host
        if ($LASTEXITCODE -ne 0) { throw "libVLC probe exited $LASTEXITCODE. See $report" }
        $result = Get-Content -LiteralPath (Join-Path $report 'result.json') -Raw | ConvertFrom-Json
        if (-not $result.passed -or -not $result.shutdownCompleted -or $result.qmlWarnings -ne 0) { throw "Probe checks failed. See $report" }
    }
}
