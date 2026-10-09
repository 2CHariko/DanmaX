#requires -Version 7.0
param([switch]$Offline)
. "$PSScriptRoot/common.ps1"
Invoke-ProjectEnvironment {
    $lock = Get-Content (Get-ProjectPath 'tools/ui-library-probe/dependencies.lock.json') -Raw | ConvertFrom-Json
    $cache = Get-ProjectPath '.cache/ui-library-probe'
    $extra = Get-ProjectPath '.deps/ui-library-probe/qt-extra'
    New-Item -ItemType Directory -Force $cache,$extra | Out-Null
    function Get-ProbeArchive($url,$file,$hash) {
        if (!(Test-Path -LiteralPath $file)) {
            if ($Offline) { throw "Missing cached archive: $file" }
            Invoke-WebRequest $url -OutFile $file
        }
        if ((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -ne $hash) { throw "SHA-256 mismatch: $file" }
    }
    foreach ($library in $lock.libraries) {
        $repo = $library.repository.Replace('https://github.com/','')
        $file = Join-Path $cache $library.archive
        Get-ProbeArchive "https://codeload.github.com/$repo/zip/$($library.commit)" $file $library.sha256
        Expand-Archive -LiteralPath $file -DestinationPath $cache -Force
    }
    foreach ($asset in $lock.qtExtras) {
        $file = Join-Path $cache "$($asset.name).7z"
        Get-ProbeArchive $asset.url $file $asset.sha256
        Invoke-Checked (Join-Path $env:SystemRoot 'System32/tar.exe') @('-xf',$file,'-C',$extra)
    }
    Write-Host 'Evaluation dependencies verified and extracted. Production SDK and dependency lock unchanged.'
}
