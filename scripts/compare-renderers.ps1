#requires -Version 7.0
param([int] $Seconds = 5)
. "$PSScriptRoot/common.ps1"
Invoke-ProjectEnvironment {
    $tools = Initialize-Toolchain
    $exe = Get-ProjectPath 'out/build/windows-release/bin/danmaku_app.exe'
    $output = Get-ProjectPath ('out/validation/renderers-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
    New-Item -ItemType Directory -Force $output | Out-Null
    $env:QT_QPA_PLATFORM = 'windows'
    Remove-Item Env:QT_QUICK_BACKEND -ErrorAction SilentlyContinue
    $env:QT_SCALE_FACTOR = '1'
    @{
        cpu = @(Get-CimInstance Win32_Processor | Select-Object Name,NumberOfLogicalProcessors)
        video = @(Get-CimInstance Win32_VideoController | Select-Object Name,DriverVersion)
        build = 'Release'; qt = $DependencyLock.qtVersion
        note = 'Short cold-start samples; callbacks are not GPU or optical frame times.'
    } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $output 'environment.json') -Encoding utf8
    foreach ($backend in @('text','image')) {
        $env:DANMAKU_RENDER_BACKEND = $backend
        foreach ($count in @(500,2000)) {
            $name = "$backend-$count"
            Invoke-Checked $exe @('--benchmark',"$count",'--seconds',"$Seconds",
                '--data-dir',(Join-Path $output "$name-data"),'--cache-dir',$env:QML_DISK_CACHE_PATH,
                '--report',(Join-Path $output "$name.json"))
        }
    }
    Write-Host "Renderer comparison: $output"
}
