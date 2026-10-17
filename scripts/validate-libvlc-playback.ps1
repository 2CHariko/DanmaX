#requires -Version 7.0
param([Parameter(Mandatory)][string] $Video,
      [ValidateRange(60,7200)][int] $Seconds = 1800,
      [ValidatePattern('^[a-zA-Z0-9-]+$')][string] $RunName = (Get-Date -Format 'yyyyMMdd-HHmmss'),
      [switch] $NoBuild, [switch] $Opaque, [ValidateRange(1,3)][double] $Scale = 1,
      [ValidateSet('system','light','dark')][string] $Theme = 'system')
. "$PSScriptRoot/common.ps1"
. "$PSScriptRoot/deployment.ps1"
Invoke-ProjectEnvironment {
    $videoFile = Get-Item -LiteralPath $Video
    if ($videoFile.PSIsContainer) { throw 'Video must be a local file.' }
    $preset = 'windows-libvlc-release'
    $tools = Initialize-Toolchain
    $build = Get-ProjectPath "out/build/$preset"
    if (-not $NoBuild) { & "$PSScriptRoot/build.ps1" -Preset $preset }
    $cacheText = Get-Content -LiteralPath (Join-Path $build 'CMakeCache.txt') -Raw
    if ($cacheText -notmatch '(?m)^DANMAKU_ENABLE_LIBVLC:BOOL=(?:ON|TRUE|1|YES)\r?$') { throw 'Validation requires an embedded build.' }
    if ($cacheText -notmatch '(?m)^CMAKE_BUILD_TYPE:STRING=Release\r?$' -or
        $cacheText -notmatch '(?m)^DANMAKU_STATIC_QT:BOOL=(?:OFF|FALSE|0|NO)\r?$') { throw 'Validation requires the dynamic Release preset.' }
    $runtime = Get-ProjectPath "out/run/validation/libvlc-$RunName"
    $exe = Sync-DevelopmentRuntime (Join-Path $build 'bin/DanmaX.exe') $runtime $build $tools $preset
    $report = Get-ProjectPath "out/validation/libvlc-integration/$RunName"
    if (Test-Path -LiteralPath $report) { throw 'Choose a fresh RunName to preserve validation evidence.' }
    New-Item -ItemType Directory -Force $report | Out-Null
    $mica = if ($Opaque) { 'false' } else { 'true' }
    # Deterministic offline matching and a real XML source selected in history.
    "[Meta]`nformatVersion=1`n[Online]`ndanmakuServers=`"[]`"`n[Appearance]`nmicaEnabled=$mica`n" |
        Set-Content -LiteralPath (Join-Path $report 'settings.ini') -Encoding utf8
    $xml = Join-Path $report 'validation.xml'
    $comments = foreach ($second in 0..7200) { '<d p="' + $second + ',1,25,16777215">内置播放同步验证 ' + $second + '</d>' }
    ('<i>' + ($comments -join '') + '</i>') | Set-Content -LiteralPath $xml -Encoding utf8
    @{version=1; entries=@(@{path=$videoFile.FullName.Replace('\','/'); title=$videoFile.Name;
        size=[string]$videoFile.Length; modified=[string]([DateTimeOffset]$videoFile.LastWriteTimeUtc).ToUnixTimeMilliseconds();
        position=0; duration=0; source=@{kind='xml'; path=$xml.Replace('\','/')}})} |
        ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $report 'recent-videos.json') -Encoding utf8
    $env:QT_SCALE_FACTOR = [string]$Scale
    $env:VLC_PLUGIN_PATH = Join-Path $runtime 'libvlc/plugins'
    foreach ($variable in @('APPDATA','LOCALAPPDATA')) {
        $directory = Join-Path $report "runtime/$variable"
        New-Item -ItemType Directory -Force $directory | Out-Null
        [Environment]::SetEnvironmentVariable($variable, $directory, 'Process')
    }
    $info = [Diagnostics.ProcessStartInfo]::new($exe)
    $info.UseShellExecute = $false; $info.WorkingDirectory = $runtime
    foreach ($argument in @('--data-dir',$report,'--video',$videoFile.FullName,'--theme',$Theme,'--playback-validation',[string]$Seconds,
            '--report',(Join-Path $report 'result.json'),'--capture',(Join-Path $report 'composition.png'))) { $info.ArgumentList.Add($argument) }
    $info.RedirectStandardOutput = $true; $info.RedirectStandardError = $true
    $process = [Diagnostics.Process]::Start($info)
    $stdout = $process.StandardOutput.ReadToEndAsync(); $stderr = $process.StandardError.ReadToEndAsync()
    @{pid=$process.Id; started=$process.StartTime.ToUniversalTime().ToString('o'); seconds=$Seconds; scale=$Scale; theme=$Theme; micaRequested=(-not $Opaque);
        exeSha256=(Get-FileHash -LiteralPath $exe).Hash; lockSha256=(Get-FileHash -LiteralPath (Get-ProjectPath 'toolchain/libvlc.lock.json')).Hash;
        cpu=@(Get-CimInstance Win32_Processor | Select-Object Name,NumberOfLogicalProcessors);
        gpu=@(Get-CimInstance Win32_VideoController | Select-Object Name,DriverVersion)} |
        ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $report 'environment.json') -Encoding utf8
    Write-Host "Real playback validation PID $($process.Id); results: $report"
    # This orchestration waits for normal application exit; no process is killed.
    $process.WaitForExit()
    [IO.File]::WriteAllText((Join-Path $report 'stdout.txt'), $stdout.Result)
    [IO.File]::WriteAllText((Join-Path $report 'stderr.txt'), $stderr.Result)
    @{exitCode=$process.ExitCode; exitedAt=[DateTime]::UtcNow.ToString('o')} | ConvertTo-Json |
        Set-Content -LiteralPath (Join-Path $report 'exit.json') -Encoding utf8
    $result = Get-Content -LiteralPath (Join-Path $report 'result.json') -Raw | ConvertFrom-Json
    if ($process.ExitCode -ne 0 -or $result.validationElapsedSeconds -lt $Seconds -or $result.validationQmlWarning -or
        -not $result.videoActive -or $result.embeddedPlayback.displayedPictures -le 0 -or $result.validationPlayingSamples -lt ($Seconds - 20) -or
        @($result.validationChecks.PSObject.Properties | Where-Object { -not $_.Value }).Count -gt 0) { throw 'Real playback validation failed; inspect recorded evidence.' }
    Write-Host 'Playback duration/state checks passed. Visual, audible output and window interactions require separate review.'
}
