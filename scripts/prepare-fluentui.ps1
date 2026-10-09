#requires -Version 7.0
param([switch]$Offline, [switch]$SourceOnly)
. "$PSScriptRoot/common.ps1"
Invoke-ProjectEnvironment {
    $lock = Get-Content (Get-ProjectPath 'toolchain/fluentui.lock.json') -Raw | ConvertFrom-Json
    $cache = Get-ProjectPath '.cache/fluentui'
    $extra = Get-ProjectPath '.deps/fluentui/qt-extra'
    New-Item -ItemType Directory -Force $cache,$extra | Out-Null
    function Get-FluentArchive($url,$file,$hash) {
        if (!(Test-Path -LiteralPath $file)) {
            if ($Offline) { throw "Missing cached archive: $file" }
            Invoke-WebRequest $url -OutFile $file
        }
        if ((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -ne $hash) { throw "SHA-256 mismatch: $file" }
    }
    foreach ($library in $lock.libraries) {
        $repo = $library.repository.Replace('https://github.com/','')
        $file = Join-Path $cache $library.archive
        Get-FluentArchive "https://codeload.github.com/$repo/zip/$($library.commit)" $file $library.sha256
        Expand-Archive -LiteralPath $file -DestinationPath (Get-ProjectPath '.deps/fluentui') -Force
    }
    if (!$SourceOnly) { foreach ($asset in $lock.qtExtras) {
        $file = Join-Path $cache "$($asset.name).7z"
        Get-FluentArchive $asset.url $file $asset.sha256
        Invoke-Checked (Join-Path $env:SystemRoot 'System32/tar.exe') @('-xf',$file,'-C',$extra)
    }
    }
    Write-Host 'Locked FluentUI source and matching Qt extras prepared. Configure/build do not download.'
}
