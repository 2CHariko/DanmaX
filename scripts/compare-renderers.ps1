#requires -Version 7.0
param([ValidateRange(1,30)][int] $Seconds = 8,
      [ValidateSet('windows-release','windows-static-release')][string] $Preset = 'windows-static-release',
      [int[]] $Counts = @(600,2000), [ValidateRange(1,10)][int] $Rounds = 1,
      [string] $Xml = '')
. "$PSScriptRoot/common.ps1"
Invoke-ProjectEnvironment {
    $tools = Initialize-Toolchain -StaticQt:($Preset -eq 'windows-static-release')
    $exe = Get-ProjectPath "out/build/$Preset/bin/danmaku_app.exe"
    if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) { throw 'Build the selected Release preset first.' }
    foreach ($count in $Counts) {
        if ($count -lt 50 -or $count -gt 5000) { throw 'Counts must be between 50 and 5000.' }
    }
    $fixture = if ($Xml) { Get-ProjectPath $Xml } else { '' }
    if ($fixture -and -not (Test-Path -LiteralPath $fixture -PathType Leaf)) { throw 'XML fixture not found.' }
    $output = Get-ProjectPath ('out/validation/renderers-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
    New-Item -ItemType Directory -Force $output | Out-Null
    $env:QT_QPA_PLATFORM = 'windows'
    Remove-Item Env:QT_QUICK_BACKEND,Env:QT_LOGGING_RULES,Env:QSG_RENDERER_DEBUG -ErrorAction SilentlyContinue
    $env:QT_SCALE_FACTOR = '1'
    $env:QSG_RHI_BACKEND = 'd3d11'
    $env:QSG_RENDER_LOOP = 'threaded'
    @{
        cpu = @(Get-CimInstance Win32_Processor | Select-Object Name,NumberOfLogicalProcessors)
        video = @(Get-CimInstance Win32_VideoController | Select-Object Name,DriverVersion)
        build = $Preset; qt = $DependencyLock.qtVersion; seconds = $Seconds; rounds = $Rounds
        graphics = 'D3D11'; renderLoop = 'threaded'; scaleFactor = 1
        fixtureSha256 = if ($fixture) { (Get-FileHash -LiteralPath $fixture).Hash } else { '' }
        executableSha256 = (Get-FileHash -LiteralPath $exe).Hash
        note = 'Cold startup included in p95/p99; final rates use the last metrics interval. XML must contain the requested count at time zero. Fixed lifetime is 30 seconds; callbacks are not GPU or optical frame times.'
    } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $output 'environment.json') -Encoding utf8
    foreach ($backend in @('text','image')) {
        $env:DANMAKU_RENDER_BACKEND = $backend
        foreach ($count in $Counts) {
            for ($round = 1; $round -le $Rounds; ++$round) {
                $name = "$backend-$count-$round"
                $data = Join-Path $output "$name-data"
                New-Item -ItemType Directory -Force $data | Out-Null
                $settings = Get-Content (Get-ProjectPath 'docs/settings.example.ini') -Raw -Encoding utf8
                $settings = $settings.Replace('debug=false','debug=true').Replace('logToFile=true','logToFile=false').
                    Replace('fixedSeconds=5','fixedSeconds=30').Replace('maxTracks=18','maxTracks=60').
                    Replace('overlap=false','overlap=true').Replace('maxActive=500',"maxActive=$count")
                Set-Content -LiteralPath (Join-Path $data 'settings.ini') -Value $settings -Encoding utf8
                $arguments = @('--benchmark',"$count",'--seconds',"$Seconds",
                    '--data-dir',$data,'--cache-dir',(Join-Path $output 'cache'),
                    '--report',(Join-Path $output "$name.json"))
                if ($fixture) { $arguments += @('--file',$fixture) }
                Invoke-Checked $exe $arguments
                $report = Get-Content (Join-Path $output "$name.json") -Raw | ConvertFrom-Json
                Write-Host "$name active=$($report.active) updateHz=$($report.updatesPerSecond) presentHz=$($report.presentedPerSecond) CPU=$($report.cpuPercent) imageNodes=$($report.imageNodes)"
                if ($fixture -and $report.active -ne $count) { throw 'Fixture did not sustain the requested active count.' }
            }
        }
    }
    Write-Host "Renderer comparison: $output"
}
