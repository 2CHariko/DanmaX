#requires -Version 7.0
# Offline audit of the vendored sources and rendered icons. Does not download.
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$directory = Join-Path $root 'resources/fluent'
$manifest = Get-Content -LiteralPath (Join-Path $directory 'manifest.json') -Raw | ConvertFrom-Json
$files = @(@($manifest.assets) + @($manifest.derivedAssets) | ForEach-Object { $_.file })
if (@($files | Sort-Object -Unique).Count -ne $files.Count -or @($files | Where-Object { $_ -notmatch '^[a-z0-9-]+\.svg$' }).Count) {
    throw 'Fluent manifest must contain unique SVG filenames.'
}
$actual = @(Get-ChildItem -LiteralPath $directory -Filter '*.svg' -File | ForEach-Object { $_.Name })
if (Compare-Object ($files | Sort-Object) ($actual | Sort-Object)) { throw 'Fluent SVG files do not match the manifest.' }
foreach ($asset in @($manifest.assets) + @($manifest.derivedAssets)) {
    $path = [IO.Path]::GetFullPath((Join-Path $directory $asset.file))
    if (-not $path.StartsWith($directory + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Icon manifest path escaped resources/fluent.'
    }
    if (-not (Test-Path -LiteralPath $path) -or (Get-FileHash -LiteralPath $path).Hash.ToLowerInvariant() -ne $asset.sha256) {
        throw "Fluent icon hash mismatch: $($asset.file)"
    }
}
Write-Host "Verified $(@($manifest.assets).Count) official SVG sources and $(@($manifest.derivedAssets).Count) derived SVG resources ($($manifest.commit))."
