#requires -Version 7.0
param([switch] $Offline)
. "$PSScriptRoot/common.ps1"
Invoke-ProjectEnvironment {
    $directory = Get-ProjectPath 'resources/fluent'
    $manifest = Get-Content -LiteralPath (Join-Path $directory 'manifest.json') -Raw | ConvertFrom-Json
    foreach ($asset in $manifest.assets) {
        $source = Get-ProjectPath "resources/fluent/$($asset.file)"
        if (-not (Test-Path -LiteralPath $source)) {
            if ($Offline) { throw "Missing vendored icon: $($asset.file)" }
            $download = Get-ProjectPath ".cache/downloads/fluent/$($manifest.commit)/$($asset.file)"
            New-Item -ItemType Directory -Force (Split-Path $download) | Out-Null
            Invoke-WebRequest -Uri $asset.url -OutFile $download
            if ((Get-FileHash -LiteralPath $download).Hash.ToLowerInvariant() -ne $asset.sha256) {
                throw "Fluent source hash mismatch: $($asset.file)"
            }
            Copy-Item -LiteralPath $download -Destination $source
        }
        if ((Get-FileHash -LiteralPath $source).Hash.ToLowerInvariant() -ne $asset.sha256) {
            throw "Fluent source hash mismatch: $($asset.file)"
        }
    }
    foreach ($asset in $manifest.derivedAssets) {
        $source = Get-ProjectPath "resources/fluent/$($asset.source)"
        $destination = Get-ProjectPath "resources/fluent/$($asset.file)"
        $svg = [IO.File]::ReadAllText($source)
        if (-not $svg.Contains('#212121')) { throw "Unexpected SVG palette: $($asset.source)" }
        $dark = $svg.Replace('#212121', '#DEDEDE')
        $bytes = [Text.Encoding]::UTF8.GetBytes($dark)
        $hash = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes)).ToLowerInvariant()
        if ($hash -ne $asset.sha256) { throw "Derived SVG hash mismatch: $($asset.file)" }
        [IO.File]::WriteAllBytes($destination, $bytes)
    }
    & "$PSScriptRoot/verify-fluent-assets.ps1"
}
