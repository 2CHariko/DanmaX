# Called inside Invoke-ProjectEnvironment with an initialized toolchain.
function Get-FluentDeployQtPaths($Tools) {
    $extra = Get-ProjectPath '.deps/fluentui/qt-extra'
    $view = Get-ProjectPath '.cache/fluentui/deploy-sdk-view'
    New-Item -ItemType Directory -Force "$view/bin" | Out-Null
    foreach ($directory in @("$($Tools.Qt)/bin", "$extra/bin")) {
        Get-ChildItem -LiteralPath $directory -File | Where-Object Name -ne 'qt.conf' | ForEach-Object {
            $link = Join-Path "$view/bin" $_.Name
            if (!(Test-Path -LiteralPath $link)) { New-Item -ItemType HardLink -Path $link -Target $_.FullName | Out-Null }
        }
    }
    @('[Paths]',"Prefix=$($Tools.Qt.Replace('\','/'))","Binaries=$($view.Replace('\','/'))/bin") | Set-Content "$view/bin/qt.conf"
    return "$view/bin/qtpaths.exe"
}
function Copy-FluentDistributionMaterials([string]$Destination) {
    $lock = Get-Content (Get-ProjectPath 'toolchain/fluentui.lock.json') -Raw | ConvertFrom-Json
    $library = $lock.libraries[0]
    $archive = Get-ProjectPath ".cache/fluentui/$($library.archive)"
    if ((Get-FileHash -LiteralPath $archive).Hash -ne $library.sha256) { throw 'FluentUI source archive hash mismatch.' }
    # Copy only the compiled subset and its unmodified originals. In particular,
    # never redistribute the upstream Microsoft font or unrelated GPL sources.
    $source = Get-ProjectPath ".deps/fluentui/FluentUI-$($library.commit)"
    $subset = Join-Path $Destination 'FluentUI-source'
    $cmake = Get-Content (Get-ProjectPath 'cmake/FluentUI.cmake') -Raw
    $native = [regex]::Match($cmake,'foreach\(name (FluAccentColor[^)]+)\)').Groups[1].Value -split '\s+'
    $controls = [regex]::Match($cmake,'set\(controls ([^)]+)\)').Groups[1].Value -split '\s+'
    $files = @('License','src/Def.h','src/FluentIconDef.h','src/stdafx.h','src/singleton.h')
    foreach($name in $native) { $files += "src/$name.cpp","src/$name.h" }
    foreach($name in $controls) { $files += "src/Qt6/imports/FluentUI/Controls/$name.qml" }
    $manifest = foreach($relative in $files) {
        $target = Join-Path $subset $relative
        New-Item -ItemType Directory -Force (Split-Path $target) | Out-Null
        Copy-Item -LiteralPath (Join-Path $source $relative) -Destination $target
        [pscustomobject]@{file=$relative; sha256=(Get-FileHash -LiteralPath $target).Hash}
    }
    $manifest | ConvertTo-Json | Set-Content (Join-Path $Destination 'FluentUI-source-manifest.json')
    Copy-Item -LiteralPath (Get-ProjectPath 'toolchain/fluentui.lock.json') -Destination $Destination
    Copy-Item -LiteralPath (Get-ProjectPath ".deps/fluentui/FluentUI-$($library.commit)/License") -Destination (Join-Path $Destination 'FluentUI-LICENSE.txt')
    Copy-Item -LiteralPath (Get-ProjectPath 'cmake/FluentUI.cmake') -Destination (Join-Path $Destination 'FluentUI-subset.cmake')
}
