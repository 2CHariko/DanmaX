#requires -Version 7.0
param([switch] $Offline)
. "$PSScriptRoot/common.ps1"
Invoke-ProjectEnvironment {
    $lockPath = Get-ProjectPath 'toolchain/anitomy.lock.json'
    $lock = Get-Content -LiteralPath $lockPath -Raw | ConvertFrom-Json
    $cache = Get-ProjectPath '.cache/anitomy'
    $deps = Get-ProjectPath '.deps/anitomy'
    $archive = Get-ProjectPath ".cache/anitomy/$($lock.archive)"
    New-Item -ItemType Directory -Force $cache,$deps | Out-Null
    if (!(Test-Path -LiteralPath $archive)) {
        if ($Offline) { throw "Missing cached archive: $archive" }
        Invoke-WebRequest -Uri $lock.url -OutFile $archive -TimeoutSec 60
    }
    if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $lock.sha256) {
        throw "SHA-256 mismatch: $archive"
    }
    # Validate every archive entry before extraction, including its top-level folder.
    $zip = [IO.Compression.ZipFile]::OpenRead($archive)
    try {
        foreach ($entry in $zip.Entries) {
            if (!$entry.FullName.StartsWith($lock.sourceDirectory + '/', [StringComparison]::Ordinal)) {
                throw "Unexpected archive entry: $($entry.FullName)"
            }
            $target = [IO.Path]::GetFullPath((Join-Path $deps $entry.FullName))
            if (!$target.StartsWith($deps + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
                throw "Archive entry escapes dependency directory: $($entry.FullName)"
            }
        }
    } finally { $zip.Dispose() }
    Expand-Archive -LiteralPath $archive -DestinationPath $deps -Force
    Set-Content -LiteralPath (Join-Path $deps '.prepared-lock') -Value (Get-FileHash -LiteralPath $lockPath -Algorithm SHA256).Hash -Encoding ascii
    Write-Host "Prepared Anitomy $($lock.commit) (MPL-2.0). Configure/build remain offline."
}
