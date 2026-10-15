#requires -Version 7.0
param([ValidateRange(1,5)][int]$Samples = 3)
. "$PSScriptRoot/common.ps1"
$rows = @()
for ($index=1; $index -le $Samples; ++$index) {
    foreach ($kind in @('qt-only','framework','self-contained')) {
        $label = "measure-$index"
        $arguments = @{Mode='Smoke';StaticQt=$true;Staged=$true;RunLabel=$label}
        if ($kind -eq 'qt-only') { $arguments.QtOnly=$true }
        if ($kind -eq 'self-contained') { $arguments.SelfContained=$true }
        & "$PSScriptRoot/probe-winui-island.ps1" @arguments
        $folder = if ($kind -eq 'qt-only') { 'qt-only-smoke' } else { "native-$kind-smoke" }
        $path = Get-ProjectPath "out/validation/winui-island/static-1-baseline/$folder/$label/events.jsonl"
        $events = @(Get-Content -LiteralPath $path | ForEach-Object { $_ | ConvertFrom-Json })
        if (!($events | Where-Object { $_.event -eq 'exit' -and $_.code -eq 0 })) { throw "Incomplete measurement: $path" }
        if ($events | Where-Object { $_.event -in @('qml-warning','winrt-error','error') }) { throw "Warnings/errors in measurement: $path" }
        $loaded = $events | Where-Object { $_.event -eq 'memory' -and $_.stage -in @('native-attached','qt-only-loaded') } | Select-Object -First 1
        $ready = $events | Where-Object { $_.event -eq 'memory' -and $_.stage -eq 'ready' } | Select-Object -First 1
        $rows += [ordered]@{kind=$kind;sample=$index;loadedMs=$loaded.elapsedMs;privateMiB=[math]::Round($ready.privateBytes/1MB,2);workingSetMiB=[math]::Round($ready.workingSetBytes/1MB,2)}
    }
}
$rows | ConvertTo-Json | Set-Content -LiteralPath (Get-ProjectPath 'out/validation/winui-island/measurements.json')
$rows | ForEach-Object { [pscustomobject]$_ } | Format-Table
