#requires -Version 7.0
param([Parameter(Mandatory)][string] $Executable, [string] $SessionId = '')
. "$PSScriptRoot/common.ps1"
Invoke-ProjectEnvironment {
    $source = Get-ProjectPath $Executable
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw 'Build and package static-exe first.' }
    $root = Get-ProjectPath ('out/validation/static-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
    $directory = Join-Path $root '中文 portable'
    New-Item -ItemType Directory -Force $directory | Out-Null
    Copy-Item -LiteralPath $source -Destination (Join-Path $directory 'DanmaX.exe')
    $results = [Collections.Generic.List[object]]::new()
    function Assert-NoRuntimeFiles([switch] $AllowBlockedCache) {
        $files = @(Get-ChildItem -LiteralPath $directory -File -Recurse)
        foreach ($file in $files) {
            $relative = [IO.Path]::GetRelativePath($directory,$file.FullName)
            if ($relative -eq 'DanmaX.exe') { continue }
            if ($AllowBlockedCache -and $relative -eq 'cache') { continue }
            if ($file.Extension -in @('.dll','.exe','.qml','.rcc','.cab','.qsb')) {
                throw "Static executable released a runtime file: $relative"
            }
            if ($relative -notmatch '^(settings\.ini(\.backup-\d+)?|app\.log(\.1)?|cache\\.+)$') {
                throw "Unexpected file beside static executable: $relative"
            }
        }
        if (Test-Path -LiteralPath (Join-Path $directory 'data/runtime')) { throw 'Static package created an extraction directory.' }
        if ((Get-FileHash -LiteralPath (Join-Path $directory 'DanmaX.exe')).Hash -ne
            (Get-FileHash -LiteralPath $source).Hash) { throw 'Running application modified its executable.' }
    }
    function Invoke-StaticCheck([string] $Name, [string[]] $Extra = @(), [switch] $Native, [switch] $ImageBackend,
                               [int] $Expected = 0, [switch] $Benchmark) {
        $info = [Diagnostics.ProcessStartInfo]::new()
        $info.FileName = Join-Path $directory 'DanmaX.exe'
        $info.WorkingDirectory = $ProjectRoot
        $info.UseShellExecute = $false
        $info.RedirectStandardOutput = $true
        $info.RedirectStandardError = $true
        $info.Environment['PATH'] = [Environment]::GetFolderPath('System')
        $info.Environment['QT_PLUGIN_PATH'] = Join-Path $root 'missing-plugins'
        $info.Environment['QML_IMPORT_PATH'] = Join-Path $root 'missing-qml'
        if ($Native) {
            $info.Environment.Remove('QT_QPA_PLATFORM') | Out-Null
            $info.Environment.Remove('QT_QUICK_BACKEND') | Out-Null
        } else {
            $info.Environment['QT_QPA_PLATFORM'] = 'offscreen'
            $info.Environment['QT_QUICK_BACKEND'] = 'software'
        }
        if ($ImageBackend) { $info.Environment['DANMAKU_RENDER_BACKEND'] = 'image' }
        if ($Benchmark) {
            foreach ($argument in @('--benchmark','50','--seconds','3')) { $info.ArgumentList.Add($argument) }
        } else { $info.ArgumentList.Add('--smoke-test') }
        foreach ($argument in $Extra) { $info.ArgumentList.Add($argument) }
        $events = [Collections.Generic.List[string]]::new()
        $watcher = [IO.FileSystemWatcher]::new($directory)
        $watcher.IncludeSubdirectories = $true
        $watcher.NotifyFilter = [IO.NotifyFilters]::FileName
        Register-ObjectEvent $watcher Created -SourceIdentifier "StaticCreated-$Name" | Out-Null
        $watcher.EnableRaisingEvents = $true
        $watch = [Diagnostics.Stopwatch]::StartNew()
        $process = [Diagnostics.Process]::Start($info)
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        if ($Benchmark) {
            if ($process.WaitForExit(1000)) { throw "Static process exited before module audit: $Name" }
            $modules = @($process.Modules | ForEach-Object { $_.FileName })
            $modules | Set-Content -LiteralPath (Join-Path $root "$Name-modules.txt") -Encoding utf8
            $sdkModules = @($modules | Where-Object {
                $_ -match '[\\/](Qt\d[^\\/]*|dxcompiler|dxil)\.dll$' -or $_ -like "$ProjectRoot\.tools\*"
            })
            if ($sdkModules.Count) { throw "Static executable loaded an SDK/external Qt module: $sdkModules" }
        }
        if (-not $process.WaitForExit(30000)) { $process.Kill($true); throw "Static validation timeout: $Name" }
        $watch.Stop()
        $code = $process.ExitCode
        $stdout.Result + $stderr.Result | Set-Content -LiteralPath (Join-Path $root "$Name.log") -Encoding utf8
        $process.Dispose()
        $watcher.EnableRaisingEvents = $false
        foreach ($event in @(Get-Event -SourceIdentifier "StaticCreated-$Name" -ErrorAction SilentlyContinue)) {
            $events.Add($event.SourceEventArgs.FullPath)
            Remove-Event -EventIdentifier $event.EventIdentifier
        }
        Unregister-Event -SourceIdentifier "StaticCreated-$Name"
        $watcher.Dispose()
        $events | Set-Content -LiteralPath (Join-Path $root "$Name-created-files.txt") -Encoding utf8
        if (@($events | Where-Object { $_ -match '\.(dll|exe|qml|rcc|cab|qsb)$' }).Count) {
            throw "Runtime extraction observed during static launch: $Name"
        }
        if ($code -ne $Expected) { throw "Static validation failed: $Name ($code expected $Expected); see $root/$Name.log" }
        Assert-NoRuntimeFiles -AllowBlockedCache:($Name -eq 'blocked-cache')
        $results.Add([ordered]@{name=$Name;exitCode=$code;expected=$Expected;elapsedMs=$watch.ElapsedMilliseconds;noExtractedRuntime=$true})
    }
    Invoke-StaticCheck 'cold' -Extra @('--theme','dark')
    if (-not (Test-Path -LiteralPath (Join-Path $directory 'settings.ini')) -or
        -not (Test-Path -LiteralPath (Join-Path $directory 'app.log'))) { throw 'Configuration/log not beside EXE.' }
    $before = Get-Content -LiteralPath (Join-Path $directory 'settings.ini') -Raw -Encoding utf8
    if ($before -notmatch '(?m)^theme="dark"\r?$' -or $before -notmatch '滚动弹幕速度') {
        throw 'Configuration is not an annotated UTF-8 INI with saved theme.'
    }
    Invoke-StaticCheck 'warm'
    $after = Get-Content -LiteralPath (Join-Path $directory 'settings.ini') -Raw -Encoding utf8
    if ($before -ne $after) { throw 'Configuration did not survive warm launch.' }
    $moved = Join-Path $root '搬迁后 moved'
    foreach ($target in @($directory,$moved)) {
        if (-not ([IO.Path]::GetFullPath($target)).StartsWith($root + [IO.Path]::DirectorySeparatorChar,
                [StringComparison]::OrdinalIgnoreCase)) { throw 'Unsafe relocation path.' }
    }
    Move-Item -LiteralPath $directory -Destination $moved
    $directory = $moved
    Invoke-StaticCheck 'relocated-xml' -Extra @('--file','tests/fixtures/sample.xml')
    Invoke-StaticCheck 'native-dark' -Native -Extra @('--theme','dark','--page','settings',
        '--capture',(Join-Path $root 'dark.png'),'--capture-overlay',(Join-Path $root 'overlay.png'))
    Invoke-StaticCheck 'native-light' -Native -Extra @('--theme','light','--page','settings','--capture',(Join-Path $root 'light.png'))
    Invoke-StaticCheck 'image-backend' -ImageBackend
    Invoke-StaticCheck 'native-module-audit' -Native -Benchmark -Extra @('--report',(Join-Path $root 'native-metrics.json'))
    if ($SessionId) { Invoke-StaticCheck 'real-smtc' -Native -Extra @('--media-session',$SessionId) }
    $inventory = Get-ChildItem -LiteralPath $directory -Recurse -File | ForEach-Object {
        [ordered]@{path=[IO.Path]::GetRelativePath($directory,$_.FullName);bytes=$_.Length}
    }
    $inventory | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $root 'files.json') -Encoding utf8
    # A file at the cache location must fail without overwriting it or falling back elsewhere.
    $directory = Join-Path $root 'blocked-cache'
    New-Item -ItemType Directory -Force $directory | Out-Null
    Copy-Item -LiteralPath $source -Destination (Join-Path $directory 'DanmaX.exe')
    Set-Content -LiteralPath (Join-Path $directory 'cache') -Value 'Preserve this file.'
    Invoke-StaticCheck 'blocked-cache' -Expected 1
    if ((Get-Content -LiteralPath (Join-Path $directory 'cache') -Raw).Trim() -ne 'Preserve this file.') {
        throw 'Blocked cache file was overwritten.'
    }
    $results | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $root 'results.json') -Encoding utf8
    Write-Host "Static portable checks passed: $root"
    $results | ForEach-Object { [PSCustomObject]$_ } | Format-Table -AutoSize
}
