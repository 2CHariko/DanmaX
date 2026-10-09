#requires -Version 7.0
# Offline audit of the vendored sources and rendered icons. Does not download.
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$directory = Join-Path $root 'resources/fluent'
$manifest = Get-Content -LiteralPath (Join-Path $directory 'manifest.json') -Raw | ConvertFrom-Json
foreach ($asset in @($manifest.assets) + @($manifest.derivedAssets)) {
    $path = [IO.Path]::GetFullPath((Join-Path $directory $asset.file))
    if (-not $path.StartsWith($directory + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Icon manifest path escaped resources/fluent.'
    }
    if (-not (Test-Path -LiteralPath $path) -or (Get-FileHash -LiteralPath $path).Hash.ToLowerInvariant() -ne $asset.sha256) {
        throw "Fluent icon hash mismatch: $($asset.file)"
    }
}
Write-Host "Verified $(@($manifest.assets).Count) SVG sources and $(@($manifest.derivedAssets).Count) PNG resources ($($manifest.commit))."
