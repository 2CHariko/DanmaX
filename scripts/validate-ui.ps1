#requires -Version 7.0
param([Parameter(Mandatory)][string] $Executable, [switch] $ScrollPopup)
. "$PSScriptRoot/common.ps1"
Invoke-ProjectEnvironment {
    $exe = Get-ProjectPath $Executable
    $output = Get-ProjectPath $(if ($ScrollPopup) { 'out/validation/scroll-popup/deployed' } else { 'out/validation/ui-refresh/deployed' })
    New-Item -ItemType Directory -Force $output | Out-Null
    $results = @()
    foreach ($theme in @('light','dark')) {
        foreach ($width in @(520,800,1080)) {
            foreach ($page in $(if ($ScrollPopup) { @('settings-middle','font-popup') } else { @('player','settings','logs') })) {
                $name = "$page-$theme-$width"
                $info = [Diagnostics.ProcessStartInfo]::new($exe)
                $info.UseShellExecute = $false
                $info.CreateNoWindow = $true
                $info.RedirectStandardError = $true
                foreach ($arg in @('--smoke-test','--seconds','2','--page',$page,'--theme',$theme,'--window-width',"$width",'--capture',"$output/$name.png",'--data-dir',"$output/$name-data",'--cache-dir',"$output/cache")) { $info.ArgumentList.Add($arg) }
                if ($ScrollPopup) { $info.ArgumentList.Add('--capture-state'); $info.ArgumentList.Add($page) }
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
