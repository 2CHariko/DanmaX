#requires -Version 7.0
param([switch]$Offline)
. "$PSScriptRoot/common.ps1"
Invoke-ProjectEnvironment {
    $lock = Get-Content (Get-ProjectPath 'toolchain/qwindowkit.lock.json') -Raw | ConvertFrom-Json
    $cache = Get-ProjectPath '.cache/qwindowkit'
    $deps = Get-ProjectPath '.deps/qwindowkit'
    New-Item -ItemType Directory -Force $cache,$deps | Out-Null
    $file = Join-Path $cache $lock.archive
    if (!(Test-Path -LiteralPath $file)) {
        if ($Offline) { throw "Missing cached archive: $file" }
        Write-Host "Locked QWindowKit archive missing in cache."
    }
    if ((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -ne $lock.sha256) {
        throw "SHA-256 mismatch: $file"
    }
    Expand-Archive -LiteralPath $file -DestinationPath $deps -Force
    Write-Host 'Locked QWindowKit source prepared. Configure/build do not download.'
}
