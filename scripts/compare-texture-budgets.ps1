#requires -Version 7.0
param([Parameter(Mandatory)][string] $Xml, [ValidateRange(50,5000)][int] $Count = 600,
      [ValidateRange(1,30)][int] $Seconds = 8, [ValidateRange(1,10)][int] $Rounds = 2,
      [double[]] $Scales = @(1,1.5,2))
. "$PSScriptRoot/common.ps1"
Invoke-ProjectEnvironment {
    $tools = Initialize-Toolchain -StaticQt
    $exe = Get-ProjectPath 'out/build/windows-static-release/bin/danmaku_app.exe'
    $fixture = Get-ProjectPath $Xml
    if (-not (Test-Path -LiteralPath $fixture -PathType Leaf)) { throw 'XML fixture not found.' }
    $output = Get-ProjectPath ('out/validation/texture-budgets-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
    New-Item -ItemType Directory -Force $output | Out-Null
    $env:QT_QPA_PLATFORM='windows'
    $env:QSG_RHI_BACKEND='d3d11'
    $env:QSG_RENDER_LOOP='threaded'
    Remove-Item Env:QT_QUICK_BACKEND,Env:QT_LOGGING_RULES,Env:QSG_RENDERER_DEBUG,Env:DANMAKU_RENDER_BACKEND -ErrorAction SilentlyContinue
    @{
        cpu=@(Get-CimInstance Win32_Processor | Select-Object Name,NumberOfLogicalProcessors)
        video=@(Get-CimInstance Win32_VideoController | Select-Object Name,DriverVersion)
        build='Static Release';qt=$DependencyLock.qtVersion;graphics='D3D11';renderLoop='threaded'
        fixtureSha256=(Get-FileHash -LiteralPath $fixture).Hash
        executableSha256=(Get-FileHash -LiteralPath $exe).Hash
        count=$Count;seconds=$Seconds;rounds=$Rounds;scales=$Scales
        note='Same time-zero XML; fixed lifetime 30s and scroll speed 30 to sustain count at every scale. P95/P99 include cold startup. Final rates/CPU use the final sampling interval, not GPU/optical frame times. Higher scale reduces logical screen area.'
    } | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $output 'environment.json') -Encoding utf8
    foreach ($scale in $Scales) {
        if ($scale -le 0 -or $scale -gt 4) { throw 'Scales must be positive and at most 4.' }
        $scaleText=$scale.ToString([Globalization.CultureInfo]::InvariantCulture)
        $env:QT_SCALE_FACTOR=$scaleText
        foreach ($mode in @('manual64','auto512')) {
            for ($round=1;$round -le $Rounds;++$round) {
                $name="$mode-dpr$scaleText-$round"
                $data=Join-Path $output "$name-data"
                New-Item -ItemType Directory -Force $data | Out-Null
                $settings=Get-Content (Get-ProjectPath 'docs/settings.example.ini') -Raw -Encoding utf8
                $settings=$settings.Replace('debug=false','debug=true').Replace('logToFile=true','logToFile=false').
                    Replace('speed=180','speed=30').
                    Replace('fixedSeconds=5','fixedSeconds=30').Replace('maxTracks=18','maxTracks=60').
                    Replace('overlap=false','overlap=true').Replace('maxActive=500',"maxActive=$Count")
                if ($mode -eq 'manual64') {
                    $settings=$settings.Replace('textureBudgetAuto=true','textureBudgetAuto=false').Replace('textureBudgetMiB=512','textureBudgetMiB=64')
                }
                Set-Content (Join-Path $data 'settings.ini') $settings -Encoding utf8
                Invoke-Checked $exe @('--file',$fixture,'--benchmark',"$Count",'--seconds',"$Seconds",
                    '--data-dir',$data,'--cache-dir',(Join-Path $output 'cache'),'--report',(Join-Path $output "$name.json"))
                $report=Get-Content (Join-Path $output "$name.json") -Raw | ConvertFrom-Json
                Write-Host "$name active=$($report.active) images=$($report.imageNodes) budget=$($report.textureBudgetBytes) presentHz=$($report.presentedPerSecond) CPU=$($report.cpuPercent)"
                if ($report.active -ne $Count) { throw 'Fixture failed to sustain the requested active count.' }
            }
        }
    }
    Write-Host "Texture budget comparison: $output"
}
