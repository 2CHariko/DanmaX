#requires -Version 7.0
param([switch] $Offline)
. "$PSScriptRoot/common.ps1"
Invoke-ProjectEnvironment {
    $lock = Get-Content (Get-ProjectPath 'toolchain/qt-static.lock.json') -Raw | ConvertFrom-Json
    $downloads = Get-ProjectPath '.cache/downloads'
    New-Item -ItemType Directory -Force $downloads | Out-Null
    $tar = Join-Path ([Environment]::GetFolderPath('System')) 'tar.exe'
    if (-not (Test-Path -LiteralPath $tar)) { throw 'Windows system tar.exe is required.' }
    foreach ($asset in $lock.assets) {
        $archive = Join-Path $downloads $asset.name
        if (-not (Test-Path -LiteralPath $archive)) {
            if ($Offline) { throw "Offline archive missing: $($asset.name)" }
            Write-Host "Downloading $($asset.name)"
            Invoke-WebRequest -Uri $asset.url -OutFile "$archive.part"
            if ((Get-FileHash -LiteralPath "$archive.part").Hash.ToLowerInvariant() -ne $asset.sha256) {
                throw "Checksum mismatch: $($asset.name)"
            }
            # File paths are derived from the project-local download directory.
            Move-Item -LiteralPath "$archive.part" -Destination $archive
        }
        if ((Get-FileHash -LiteralPath $archive).Hash.ToLowerInvariant() -ne $asset.sha256) {
            throw "Checksum mismatch: $($asset.name)"
        }
        $source = Get-ProjectPath "$($lock.sourceRoot)/$($asset.module)"
        $marker = Join-Path $source '.prepared-sha256'
        if (Test-Path -LiteralPath $source) {
            if (-not (Test-Path -LiteralPath $marker) -or (Get-Content $marker -Raw).Trim() -ne $asset.sha256) {
                throw "Unverified source directory: $source"
            }
        } else {
            New-Item -ItemType Directory -Force $source | Out-Null
            Invoke-Checked $tar @('-xf',$archive,'--strip-components','1','-C',$source)
            Set-Content -LiteralPath $marker -Value $asset.sha256 -Encoding ascii
        }
        Write-Host "Verified static Qt source: $($asset.module)"
    }
}
