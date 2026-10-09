#requires -Version 7.0
param([Parameter(Mandatory)][string] $Executable, [switch] $ScrollPopup, [Alias("ExpandedGroups")][switch] $SettingsGroups)
. "$PSScriptRoot/common.ps1"
Invoke-ProjectEnvironment {
    $exe = Get-ProjectPath $Executable
    $output = Get-ProjectPath $(if ($SettingsGroups) { 'out/validation/fluentwinui3/groups' } elseif ($ScrollPopup) { 'out/validation/fluentwinui3/scroll-popup' } else { 'out/validation/fluentwinui3/pages' })
    New-Item -ItemType Directory -Force $output | Out-Null
    $results = @()
    foreach ($theme in @('light','dark')) {
        foreach ($width in @(520,800,1080)) {
            foreach ($page in $(if ($SettingsGroups) { @('diagnostics-group','cache-group') } elseif ($ScrollPopup) { @('settings-middle','font-popup') } else { @('player','settings','logs') })) {
                $name = "$page-$theme-$width"
                # QQuickWindow::grabWindow captures only Qt pixels, not the DWM
                # backdrop. Use a deliberate opaque fixture for layout captures;
                # validate Mica separately with desktop-composited screenshots.
                $fixture = Join-Path $output "$name-data"
                New-Item -ItemType Directory -Force $fixture | Out-Null
                "[Meta]`nformatVersion=1`n[Appearance]`ntheme=$theme`nmicaEnabled=false`n" | Set-Content -LiteralPath (Join-Path $fixture 'settings.ini') -Encoding utf8
                $info = [Diagnostics.ProcessStartInfo]::new($exe)
                $info.UseShellExecute = $false
                $info.CreateNoWindow = $true
                $info.RedirectStandardError = $true
                foreach ($arg in @('--smoke-test','--seconds','2','--page',$page,'--theme',$theme,'--window-width',"$width",'--capture',"$output/$name.png",'--data-dir',"$output/$name-data",'--cache-dir',"$output/cache")) { $info.ArgumentList.Add($arg) }
                if ($ScrollPopup -or $SettingsGroups) { $info.ArgumentList.Add('--capture-state'); $info.ArgumentList.Add($page) }
                $process = [Diagnostics.Process]::Start($info)
                $errorText = $process.StandardError.ReadToEnd()
                $process.WaitForExit()
                $results += [pscustomobject]@{Name=$name;ExitCode=$process.ExitCode;Stderr=$errorText}
                $process.Dispose()
            }
        }
    }
    $results | ConvertTo-Json -Depth 4 | Set-Content "$output/results.json" -Encoding utf8
    $results | Format-Table Name,ExitCode
    if (@($results | Where-Object { $_.ExitCode -ne 0 -or $_.Stderr.Length -gt 0 }).Count) { throw 'UI deployment validation failed; inspect results.json.' }
}
