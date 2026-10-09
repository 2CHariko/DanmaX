#requires -Version 7.0
param([switch] $Benchmark, [string] $SessionId = '')
. "$PSScriptRoot/common.ps1"
Invoke-ProjectEnvironment {
    $tools = Initialize-Toolchain
    $exe = Get-ProjectPath 'out/build/windows-release/bin/DanmaX.exe'
    if (-not (Test-Path -LiteralPath $exe)) { throw 'Build Release first.' }
    $output = Get-ProjectPath ('out/validation/' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
    New-Item -ItemType Directory -Force $output | Out-Null
    $env:QT_QPA_PLATFORM = 'windows'
    Remove-Item Env:QT_QUICK_BACKEND -ErrorAction SilentlyContinue
    $env:QT_SCALE_FACTOR = '1'
    $hardware = @{
        cpu = @(Get-CimInstance Win32_Processor | Select-Object Name,NumberOfLogicalProcessors)
        video = @(Get-CimInstance Win32_VideoController | Select-Object Name,DriverVersion)
        build = 'Release'; qt = $DependencyLock.qtVersion; date = (Get-Date).ToString('o')
    }
    $hardware | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $output 'environment.json') -Encoding utf8
    foreach ($case in @(@('player','light','1080','1'),@('settings','dark','1080','1'),@('settings','light','520','1'),@('logs','light','800','1'),@('settings','dark','800','1.5'),@('player','light','800','2'))) {
        $name = $case -join '-'
        $env:QT_SCALE_FACTOR = $case[3]
        Invoke-Checked $exe @('--smoke-test','--page',$case[0],'--theme',$case[1],'--window-width',$case[2],
            '--data-dir',(Join-Path $output "data-$name"),'--cache-dir',$env:QML_DISK_CACHE_PATH,
            '--capture',(Join-Path $output "$name.png"),'--report',(Join-Path $output "$name.json"))
    }
    $env:QT_SCALE_FACTOR = '1'
    Invoke-Checked $exe @('--smoke-test','--file',(Get-ProjectPath 'tests/fixtures/sample.xml'),
        '--data-dir',(Join-Path $output 'xml-data'),'--cache-dir',$env:QML_DISK_CACHE_PATH,
        '--report',(Join-Path $output 'xml.json'),'--capture-overlay',(Join-Path $output 'xml-overlay.png'))
    if ($SessionId) {
        Invoke-Checked $exe @('--benchmark','50','--seconds','5','--media-session',$SessionId,'--file',(Get-ProjectPath 'tests/fixtures/sample.xml'),
            '--data-dir',(Join-Path $output 'session-data'),'--cache-dir',$env:QML_DISK_CACHE_PATH,'--report',(Join-Path $output 'session.json'))
    }
    if ($Benchmark) {
        foreach ($count in @(500,2000)) {
            Invoke-Checked $exe @('--benchmark',"$count",'--seconds','4',
                '--data-dir',(Join-Path $output "bench-$count-data"),'--cache-dir',$env:QML_DISK_CACHE_PATH,
                '--report',(Join-Path $output "bench-$count.json"))
        }
    }
    Write-Host "Validation artifacts: $output"
    Write-Host 'Inspect screenshots. Four-second workloads are short samples, not sustained or GPU performance certification.'
}
