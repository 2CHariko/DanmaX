#requires -Version 7.0
param([Parameter(Mandatory)][string] $Executable)
. "$PSScriptRoot/common.ps1"
Invoke-ProjectEnvironment {
    $source = Get-ProjectPath $Executable
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw 'Package single-exe first.' }
    $root = Get-ProjectPath ('out/validation/portable-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
    $directory = Join-Path $root '中文 portable test'
    New-Item -ItemType Directory -Force $directory | Out-Null
    Copy-Item -LiteralPath $source -Destination (Join-Path $directory 'DanmaX.exe')
    $results = [Collections.Generic.List[object]]::new()
    function Invoke-PortableCheck([string] $Name, [string[]] $Extra = @(), [switch] $Native,
                                  [switch] $ImageBackend, [int] $Expected = 0) {
        $info = [Diagnostics.ProcessStartInfo]::new()
        $info.FileName = Join-Path $directory 'DanmaX.exe'
        $info.WorkingDirectory = $ProjectRoot
        $info.UseShellExecute = $false
        $info.RedirectStandardOutput = $true
        $info.RedirectStandardError = $true
        # Deliberately invalid inherited paths must be cleared by the bootstrapper.
        $info.Environment['PATH'] = Join-Path $root 'missing-sdk'
        $info.Environment['QT_PLUGIN_PATH'] = Join-Path $root 'missing-plugins'
        $info.Environment['QML_IMPORT_PATH'] = Join-Path $root 'missing-qml'
        if ($ImageBackend) { $info.Environment['DANMAKU_RENDER_BACKEND'] = 'image' }
        if ($Native) {
            $info.Environment.Remove('QT_QPA_PLATFORM') | Out-Null
            $info.Environment.Remove('QT_QUICK_BACKEND') | Out-Null
        } else {
            $info.Environment['QT_QPA_PLATFORM'] = 'offscreen'
            $info.Environment['QT_QUICK_BACKEND'] = 'software'
        }
        $info.ArgumentList.Add('--smoke-test')
        foreach ($argument in $Extra) { $info.ArgumentList.Add($argument) }
        $watch = [Diagnostics.Stopwatch]::StartNew()
        $process = [Diagnostics.Process]::Start($info)
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit(30000)) {
            $process.Kill($true)
            $process.WaitForExit()
            $stdout.Result + $stderr.Result | Set-Content -LiteralPath (Join-Path $root "$Name.log") -Encoding utf8
            throw "Portable check timed out: $Name"
        }
        $watch.Stop()
        $code = $process.ExitCode
        $process.Dispose()
        $stdout.Result + $stderr.Result | Set-Content -LiteralPath (Join-Path $root "$Name.log") -Encoding utf8
        $results.Add([ordered]@{ name = $Name; exitCode = $code; expected = $Expected; elapsedMs = $watch.ElapsedMilliseconds })
        if ($code -ne $Expected) { throw "Portable check failed: $Name (exit $code); see $root/$Name.log" }
    }
    Invoke-PortableCheck 'cold' -Extra @('--theme','dark','--capture',(Join-Path $root 'dark.png'))
    $runtime = Get-ChildItem -LiteralPath (Join-Path $directory 'data/runtime') -Directory | Select-Object -First 1
    $before = (Get-Item -LiteralPath (Join-Path $runtime.FullName 'DanmaX.exe')).LastWriteTimeUtc
    $marker = Join-Path $directory 'data/user-marker.txt'
    Set-Content -LiteralPath $marker -Value 'Preserve portable user data.'
    Invoke-PortableCheck 'warm' -Extra @('--page','settings','--theme','light','--capture',(Join-Path $root 'light.png'))
    if ((Get-Item -LiteralPath (Join-Path $runtime.FullName 'DanmaX.exe')).LastWriteTimeUtc -ne $before) {
        throw 'Warm launch unexpectedly re-extracted the payload.'
    }
    $corrupt = Join-Path $runtime.FullName 'Qt6Core.dll'
    $expectedHash = (Get-FileHash -LiteralPath $corrupt).Hash
    [IO.File]::WriteAllText($corrupt, 'Deliberately damaged test runtime.')
    Invoke-PortableCheck 'repair'
    if ((Get-FileHash -LiteralPath $corrupt).Hash -ne $expectedHash -or -not (Test-Path -LiteralPath $marker)) {
        throw 'Runtime repair failed or touched portable user data.'
    }
    $destination = Join-Path $root '搬迁后 moved portable'
    foreach ($target in @($directory, $destination)) {
        $resolved = [IO.Path]::GetFullPath($target)
        if (-not $resolved.StartsWith($root + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
            throw 'Relocation must stay inside the new validation directory.'
        }
    }
    Move-Item -LiteralPath $directory -Destination $destination
    $directory = $destination
    Invoke-PortableCheck 'relocated' -Extra @('--file','tests/fixtures/sample.xml')
    if (-not (Test-Path -LiteralPath (Join-Path $directory 'data/user-marker.txt'))) { throw 'Relocation lost user data.' }
    Invoke-PortableCheck 'native-fluent' -Native -Extra @('--page','settings','--theme','dark',
        '--capture',(Join-Path $root 'native.png'), '--capture-overlay',(Join-Path $root 'overlay.png'))
    Invoke-PortableCheck 'image-backend' -ImageBackend
    # Reject extra executable/plugin files: repair must not carry them into the new runtime.
    $runtime = Get-ChildItem -LiteralPath (Join-Path $directory 'data/runtime') -Directory |
        Where-Object Name -Match '^[a-f0-9]{24}$' | Select-Object -First 1
    Set-Content -LiteralPath (Join-Path $runtime.FullName 'injected.dll') -Value 'Not a payload file.'
    Invoke-PortableCheck 'extra-file-repair'
    if (Test-Path -LiteralPath (Join-Path $runtime.FullName 'injected.dll')) { throw 'Unexpected runtime file survived repair.' }
    # Two cold launchers must publish one verified runtime without racing extraction.
    $parallelDirectory = Join-Path $root 'concurrent'
    New-Item -ItemType Directory -Force $parallelDirectory | Out-Null
    Copy-Item -LiteralPath $source -Destination (Join-Path $parallelDirectory 'DanmaX.exe')
    $parallel = @()
    for ($index = 0; $index -lt 2; ++$index) {
        $info = [Diagnostics.ProcessStartInfo]::new()
        $info.FileName = Join-Path $parallelDirectory 'DanmaX.exe'
        $info.UseShellExecute = $false
        $info.RedirectStandardError = $true
        $info.Environment['QT_QPA_PLATFORM'] = 'offscreen'
        $info.Environment['QT_QUICK_BACKEND'] = 'software'
        $info.Environment['PATH'] = Join-Path $root 'missing-sdk'
        $info.ArgumentList.Add('--smoke-test')
        # Concurrent application instances get separate writable settings/cache directories.
        $info.ArgumentList.Add('--data-dir')
        $info.ArgumentList.Add((Join-Path $parallelDirectory "data/instance-$index"))
        $info.ArgumentList.Add('--cache-dir')
        $info.ArgumentList.Add((Join-Path $parallelDirectory "data/cache-$index"))
        $process = [Diagnostics.Process]::Start($info)
        $parallel += @{ process = $process; stderr = $process.StandardError.ReadToEndAsync() }
    }
    foreach ($item in $parallel) {
        if (-not $item.process.WaitForExit(30000)) { $item.process.Kill($true); throw 'Concurrent launch timed out.' }
        if ($item.process.ExitCode -ne 0) { throw "Concurrent launch failed: $($item.stderr.Result)" }
        $item.process.Dispose()
    }
    $runtimeDirectories = @(Get-ChildItem -LiteralPath (Join-Path $parallelDirectory 'data/runtime') -Directory)
    if ($runtimeDirectories.Count -ne 1 -or $runtimeDirectories[0].Name -notmatch '^[a-f0-9]{24}$') {
        throw 'Concurrent extraction did not publish exactly one runtime.'
    }
    $results.Add([ordered]@{ name = 'concurrent-cold'; exitCode = 0; expected = 0 })
    # A regular file at data/ must fail cleanly, never overwrite it or fall back to AppData.
    $blockedDirectory = Join-Path $root 'blocked'
    New-Item -ItemType Directory -Force $blockedDirectory | Out-Null
    Copy-Item -LiteralPath $source -Destination (Join-Path $blockedDirectory 'DanmaX.exe')
    Set-Content -LiteralPath (Join-Path $blockedDirectory 'data') -Value 'Do not overwrite this file.'
    $directory = $blockedDirectory
    Invoke-PortableCheck 'blocked-data' -Expected 1
    if ((Get-Content -LiteralPath (Join-Path $blockedDirectory 'data') -Raw).Trim() -ne 'Do not overwrite this file.') {
        throw 'Blocked data path was overwritten.'
    }
    $results | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $root 'results.json') -Encoding utf8
    Write-Host "Portable checks passed: $root"
    $results | ForEach-Object { [PSCustomObject]$_ } | Format-Table -AutoSize
}
