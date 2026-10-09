#requires -Version 7.0
param([switch] $Offline)
. "$PSScriptRoot/common.ps1"

Invoke-ProjectEnvironment {
    $downloadDir = Get-ProjectPath '.cache/downloads'
    New-Item -ItemType Directory -Force $downloadDir | Out-Null
    $tar = Join-Path $env:SystemRoot 'System32/tar.exe'
    if (-not (Test-Path -LiteralPath $tar)) { throw 'Windows tar.exe is required to extract the verified SDK archives.' }
    $lockHash = (Get-FileHash (Get-ProjectPath 'toolchain/dependencies.lock.json') -Algorithm SHA256).Hash
    foreach ($group in ($DependencyLock.assets | Group-Object destination)) {
        $destination = Get-ProjectPath $group.Name
        $marker = Join-Path $destination '.installed-lock'
        if ((Test-Path -LiteralPath $marker) -and ((Get-Content $marker -Raw).Trim() -eq $lockHash)) {
            Write-Host "Ready: $($group.Name)"
            continue
        }
        if (Test-Path -LiteralPath $destination) { throw "Unverified or outdated tool directory: $destination. Preserve or remove it explicitly before bootstrap." }
        $stage = Get-ProjectPath ('.cache/tmp/bootstrap-' + [guid]::NewGuid().ToString('N'))
        New-Item -ItemType Directory -Force $stage | Out-Null
        foreach ($asset in $group.Group) {
            $archive = Join-Path $downloadDir $asset.name
            if (-not (Test-Path -LiteralPath $archive)) {
                if ($Offline) { throw "Offline archive missing: $($asset.name)" }
                Write-Host "Downloading $($asset.name)"
                Invoke-WebRequest -Uri $asset.url -OutFile "$archive.part"
                if ((Get-FileHash "$archive.part" -Algorithm SHA256).Hash.ToLowerInvariant() -ne $asset.sha256) { throw "Checksum mismatch: $($asset.name)" }
                Move-Item -LiteralPath "$archive.part" -Destination $archive
            }
            if ((Get-FileHash $archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $asset.sha256) { throw "Checksum mismatch: $($asset.name)" }
            Write-Host "Extracting $($asset.name)"
            Invoke-Checked $tar @('-xf', $archive, '-C', $stage)
        }
        $source = $stage
        if ($group.Group[0].PSObject.Properties.Name -contains 'stripPrefix') {
            $source = Join-Path $stage $group.Group[0].stripPrefix
        }
        New-Item -ItemType Directory -Force (Split-Path $destination) | Out-Null
        # Validate final paths immediately before the directory move.
        if (-not $source.StartsWith($ProjectRoot + '\') -or -not $destination.StartsWith($ProjectRoot + '\')) { throw 'Tool staging path escaped project.' }
        Move-Item -LiteralPath $source -Destination $destination
        Set-Content -LiteralPath $marker -Value $lockHash -Encoding utf8
    }
    $qt = (Get-ToolPaths).Qt
    @('[Paths]', 'Prefix=..') | Set-Content -LiteralPath (Join-Path $qt 'bin/qt.conf') -Encoding ascii
    Write-Host 'Project-local Qt, CMake and Ninja are ready. System MSVC/SDK are configured separately.'
}
