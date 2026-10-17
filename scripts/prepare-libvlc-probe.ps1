#requires -Version 7.0
param([switch] $Offline)
. "$PSScriptRoot/common.ps1"
Invoke-ProjectEnvironment {
    $lockPath = Get-ProjectPath 'toolchain/libvlc.lock.json'
    $lock = Get-Content -LiteralPath $lockPath -Raw | ConvertFrom-Json
    $cache = Get-ProjectPath '.cache/downloads/libvlc-probe'
    $deps = Get-ProjectPath $lock.destination
    $archive = Join-Path $cache $lock.archive
    New-Item -ItemType Directory -Force $cache,$deps | Out-Null
    if (-not (Test-Path -LiteralPath $archive)) {
        if ($Offline) { throw "Missing cached archive: $archive" }
        $partial = "$archive.partial"
        Invoke-WebRequest -Uri $lock.url -OutFile $partial -TimeoutSec 300
        if ((Get-FileHash -LiteralPath $partial -Algorithm SHA256).Hash -ne $lock.sha256) { throw 'libVLC archive SHA-256 mismatch.' }
        Move-Item -LiteralPath $partial -Destination $archive -Force
    }
    if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $lock.sha256) { throw 'libVLC cached archive SHA-256 mismatch.' }
    $zip = [IO.Compression.ZipFile]::OpenRead($archive)
    try {
        foreach ($entry in $zip.Entries) {
            if (-not $entry.FullName.StartsWith($lock.sourceDirectory + '/', [StringComparison]::Ordinal)) { throw "Unexpected archive entry: $($entry.FullName)" }
            $target = [IO.Path]::GetFullPath((Join-Path $deps $entry.FullName))
            if (-not $target.StartsWith($deps + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw 'libVLC archive escapes dependency directory.' }
        }
    } finally { $zip.Dispose() }
    Expand-Archive -LiteralPath $archive -DestinationPath $deps -Force
    $source = Join-Path $cache $lock.sourceArchive
    if (-not (Test-Path -LiteralPath $source)) {
        if ($Offline) { throw "Missing cached source: $source" }
        Invoke-WebRequest -Uri $lock.sourceUrl -OutFile "$source.partial" -TimeoutSec 300
        if ((Get-FileHash -LiteralPath "$source.partial" -Algorithm SHA256).Hash -ne $lock.sourceSha256) { throw 'libVLC source SHA-256 mismatch.' }
        Move-Item -LiteralPath "$source.partial" -Destination $source -Force
    }
    if ((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ne $lock.sourceSha256) { throw 'libVLC cached source SHA-256 mismatch.' }
    $tar = Join-Path $env:SystemRoot 'System32/tar.exe'
    if (-not (Test-Path -LiteralPath $tar)) { throw 'Windows system tar.exe is required for source headers.' }
    Write-Host "System archive tool: $tar ($((Get-Item -LiteralPath $tar).VersionInfo.FileVersion))"
    $entries = & $tar -tf $source
    if ($LASTEXITCODE -ne 0) { throw 'Cannot list libVLC source.' }
    foreach ($entry in $entries) {
        $target = [IO.Path]::GetFullPath((Join-Path $deps $entry))
        if (-not $entry.StartsWith($lock.sourceDirectory + '/', [StringComparison]::Ordinal) -or
            -not $target.StartsWith($deps + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw "Unexpected source entry: $entry" }
    }
    Invoke-Checked $tar @('-xf',$source,'-C',$deps,"$($lock.sourceDirectory)/include/vlc","$($lock.sourceDirectory)/COPYING.LIB")
    Set-Content -LiteralPath (Join-Path $deps '.prepared-lock') -Value (Get-FileHash -LiteralPath $lockPath -Algorithm SHA256).Hash -Encoding ascii
    Write-Host "Prepared shared project-local libVLC $($lock.version). Probe and integrated development builds remain offline."
}
