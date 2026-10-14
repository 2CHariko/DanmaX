#requires -Version 7.0
param([ValidateSet('windows-debug','windows-release','windows-static-release')][string] $Preset = 'windows-static-release',
      [ValidateRange(15,120)][int] $Seconds = 15,
      [int[]] $Limits = @(30,60,120,47,0),
      [ValidateRange(50,5000)][int] $StressCount = 2000,
      [ValidateRange(15,120)][int] $StressSeconds = 45,
      [ValidateSet('image','text')][string] $StressBackend = 'image',
      [ValidateRange(1,3)][double] $ScaleFactor = 1,
      [switch] $SkipStress)
. "$PSScriptRoot/common.ps1"
Invoke-ProjectEnvironment {
    $tools = Initialize-Toolchain -StaticQt:($Preset -eq 'windows-static-release')
    $exe = Get-ProjectPath "out/build/$Preset/bin/DanmaX.exe"
    if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) { throw 'Build the selected preset first.' }
    foreach ($limit in $Limits) {
        if ($limit -ne 0 -and ($limit -lt 15 -or $limit -gt 1000)) { throw 'Limits must be 0 or 15..1000.' }
    }
    $output = Get-ProjectPath ('out/validation/frame-rates-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
    New-Item -ItemType Directory -Force $output | Out-Null
    $env:QT_QPA_PLATFORM = 'windows'
    Remove-Item Env:QT_QUICK_BACKEND,Env:QT_LOGGING_RULES,Env:QSG_RENDERER_DEBUG -ErrorAction SilentlyContinue
    $env:QT_SCALE_FACTOR = $ScaleFactor.ToString([Globalization.CultureInfo]::InvariantCulture)
    $env:QSG_RHI_BACKEND = 'd3d11'
    $env:QSG_RENDER_LOOP = 'threaded'
    @{
        cpu = @(Get-CimInstance Win32_Processor | Select-Object Name,NumberOfLogicalProcessors)
        video = @(Get-CimInstance Win32_VideoController | Select-Object Name,DriverVersion)
        build = $Preset; qt = $DependencyLock.qtVersion; scaleFactor = $ScaleFactor
        executableSha256 = (Get-FileHash -LiteralPath $exe).Hash
        note = 'Native D3D11/threaded. Discard positions below 3s; use complete metrics intervals. Callbacks are not GPU or optical FPS. Synthetic text does not represent real video/background or all hardware.'
    } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $output 'environment.json') -Encoding utf8
    $template = Get-Content (Get-ProjectPath 'docs/settings.example.ini') -Raw -Encoding utf8
    $low = Join-Path $output 'low.xml'
    $text = [Text.StringBuilder]::new('<i>')
    for ($i=0; $i -lt 50; ++$i) { [void]$text.Append("<d p=`"0,5,25,16777215`">弹幕帧率测试 $i · Fluent Qt Quick</d>") }
    [void]$text.Append('</i>'); [IO.File]::WriteAllText($low, $text.ToString(), [Text.UTF8Encoding]::new($false))
    $cases = [Collections.Generic.List[object]]::new()
    foreach ($limit in $Limits) { $cases.Add(@{name="limit-$limit"; limit=$limit; adaptive=$false; count=50; seconds=$Seconds; file=$low; backend='image'}) }
    if (-not $SkipStress) {
        $stress = Join-Path $output 'stress.xml'
        $text = [Text.StringBuilder]::new('<i>')
        for ($i=0; $i -lt ($StressCount + 200 * $StressSeconds); ++$i) {
            $position = if ($i -lt $StressCount) { 0 } else { ($i - $StressCount + 1) / 200.0 }
            $mode = @(1,5,4)[$i % 3]
            $at = $position.ToString('F3', [Globalization.CultureInfo]::InvariantCulture)
            [void]$text.Append("<d p=`"$at,$mode,25,16777215`">压力弹幕 $i · 中文 English 文字描边与图片缓存持续入场，观察目标帧率和自然退场</d>")
        }
        [void]$text.Append('</i>'); [IO.File]::WriteAllText($stress, $text.ToString(), [Text.UTF8Encoding]::new($false))
        foreach ($adaptive in @($false,$true)) { $cases.Add(@{name="stress-$StressBackend-$adaptive"; limit=1000; adaptive=$adaptive; count=$StressCount; seconds=$StressSeconds; file=$stress; backend=$StressBackend}) }
    }
    $summary = [Collections.Generic.List[object]]::new()
    $failed = $false
    foreach ($case in $cases) {
        $data = Join-Path $output ($case.name + '-data')
        New-Item -ItemType Directory -Force $data | Out-Null
        $settings = $template.Replace('logToFile=true','logToFile=false').Replace('overlap=false','overlap=true').
            Replace('maxTracks=18','maxTracks=60').Replace('maxActive=500',"maxActive=$($case.count)").
            Replace('frameRateLimit=60',"frameRateLimit=$($case.limit)")
        if ($case.count -eq 50) { $settings = $settings.Replace('fixedSeconds=5','fixedSeconds=30') }
        else { $settings = $settings.Replace('fontSize=24','fontSize=48').Replace('strokeWidth=1','strokeWidth=4').Replace('targetFrameRate=60','targetFrameRate=1000') }
        if ($case.adaptive) { $settings = $settings.Replace('adaptiveFrameRateEnabled=false','adaptiveFrameRateEnabled=true') }
        Set-Content -LiteralPath (Join-Path $data 'settings.ini') -Value $settings -Encoding utf8
        $env:DANMAKU_RENDER_BACKEND = $case.backend
        $reportPath = Join-Path $output ($case.name + '.json')
        # Bound native render/worker teardown as well as data collection. A JSON
        # report alone does not prove that the benchmark exited successfully.
        $start = [Diagnostics.ProcessStartInfo]::new($exe)
        $start.UseShellExecute = $false
        $start.CreateNoWindow = $true
        $start.RedirectStandardOutput = $true
        $start.RedirectStandardError = $true
        foreach ($argument in @('--benchmark',"$($case.count)",'--seconds',"$($case.seconds)",'--file',$case.file,
            '--data-dir',$data,'--cache-dir',(Join-Path $output 'cache'),'--report',$reportPath)) {
            $start.ArgumentList.Add($argument)
        }
        $process = [Diagnostics.Process]::Start($start)
        try {
            $stdout = $process.StandardOutput.ReadToEndAsync()
            $stderr = $process.StandardError.ReadToEndAsync()
            $exited = $process.WaitForExit(($case.seconds + 30) * 1000)
            if (-not $exited) {
                $process.Kill()
                $process.WaitForExit()
            }
            $stdout.GetAwaiter().GetResult() | Set-Content -LiteralPath (Join-Path $output ($case.name + '-stdout.txt'))
            $stderr.GetAwaiter().GetResult() | Set-Content -LiteralPath (Join-Path $output ($case.name + '-stderr.txt'))
            if (-not $exited) { throw "Benchmark did not exit within its deadline: $($case.name); evidence: $output" }
            if ($process.ExitCode -ne 0) { throw "Benchmark exited with $($process.ExitCode): $($case.name); evidence: $output" }
        } finally { $process.Dispose() }
        $report = Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
        $samples = @($report.frameRateSamples | Where-Object { $_.position - $_.intervalSeconds -ge 3 -and $_.intervalSeconds -ge .75 })
        $duration = ($samples | Measure-Object intervalSeconds -Sum).Sum
        $frames = 0.0
        foreach ($sample in $samples) { $frames += $sample.presentedPerSecond * $sample.intervalSeconds }
        $hz = if ($duration -gt 0) { $frames / $duration } else { 0 }
        $tail = @($samples | Select-Object -Last 5)
        $tailHz = if ($tail.Count) { ($tail | Measure-Object presentedPerSecond -Average).Average } else { 0 }
        $passed = $duration -ge 10 -and $hz -gt 0
        if ($case.count -eq 50) { $passed = $passed -and [Math]::Abs($hz / $report.effectiveFrameRateLimit - 1) -le .05 }
        if (-not $passed) { $failed = $true }
        $summary.Add(@{name=$case.name; duration=$duration; measuredHz=$hz; passed=$passed; effectiveLimit=$report.effectiveFrameRateLimit;
            effectiveTarget=$report.effectiveTargetFrameRate; finalActive=$report.active; finalQuota=$report.admissionLimit;
            tailHz=$tailHz; targetMet=($tailHz -ge .90 * $report.effectiveTargetFrameRate);
            floorReached=($report.admissionLimit -eq $report.admissionFloor);
            skipped=$report.adaptiveSkipped; state=$report.adaptiveFrameRateState; width=$report.width; height=$report.height; dpr=$report.dpr; refreshRate=$report.refreshRate})
        Write-Host "$($case.name): $([Math]::Round($hz,2)) Hz over $([Math]::Round($duration,2))s; quota=$($report.admissionLimit) skipped=$($report.adaptiveSkipped) state=$($report.adaptiveFrameRateState) passed=$passed"
    }
    $summary | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $output 'summary.json') -Encoding utf8
    Write-Host "Frame-rate evidence: $output"
    if ($failed) { throw 'Frame-rate validation failed; inspect the saved reports.' }
}
