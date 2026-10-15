#requires -Version 7.0
param([switch]$Offline)
. "$PSScriptRoot/common.ps1"
Invoke-ProjectEnvironment {
    $tools = Initialize-Toolchain
    $lockFile = Get-ProjectPath 'toolchain/winui-island.lock.json'
    $lock = Get-Content -LiteralPath $lockFile -Raw | ConvertFrom-Json
    $download = Get-ProjectPath $lock.paths.downloads
    $packages = Get-ProjectPath $lock.paths.packages
    New-Item -ItemType Directory -Force $download,$packages | Out-Null
    foreach ($package in $lock.packages) {
        $id = $package.id.ToLowerInvariant()
        $name = "$id.$($package.version)"
        $archive = Join-Path $download "$name.nupkg"
        if (!(Test-Path -LiteralPath $archive)) {
            if ($Offline) { throw "Missing archive: $archive" }
            Invoke-WebRequest "https://api.nuget.org/v3-flatcontainer/$id/$($package.version)/$name.nupkg" -OutFile $archive
        }
        if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $package.sha256) { throw "SHA-256 mismatch: $archive" }
        $destination = Join-Path $packages $id
        $marker = Join-Path $destination '.verified-sha256'
        if (!(Test-Path -LiteralPath $marker) -or (Get-Content -LiteralPath $marker -Raw).Trim() -ne $package.sha256) {
            New-Item -ItemType Directory -Force $destination | Out-Null
            [IO.Compression.ZipFile]::ExtractToDirectory($archive, $destination, $true)
            Set-Content -LiteralPath $marker -Value $package.sha256
        }
        Write-Host "Verified: $($package.id) $($package.version)"
    }
    $projection = Get-ProjectPath $lock.paths.projection
    New-Item -ItemType Directory -Force $projection | Out-Null
    $sdkMetadata = Join-Path $env:WindowsSdkDir "UnionMetadata/$($DependencyLock.windowsSdkVersion)/Windows.winmd"
    if (!(Test-Path -LiteralPath $sdkMetadata)) { throw "Missing explicit SDK metadata: $sdkMetadata" }
    $generator = Join-Path $packages 'microsoft.windows.cppwinrt/bin/cppwinrt.exe'
    $arguments = @('-output',$projection,'-input',$sdkMetadata)
    foreach ($id in @('microsoft.windowsappsdk.foundation','microsoft.windowsappsdk.interactiveexperiences','microsoft.windowsappsdk.winui','microsoft.web.webview2')) {
        $metadata = @(Get-ChildItem -LiteralPath (Join-Path $packages $id) -Recurse -Filter '*.winmd' | Where-Object { $_.FullName -notmatch '[\\/]win10-[^\\/]+[\\/]' })
        foreach ($file in $metadata) { $arguments += @('-input',$file.FullName) }
    }
    Invoke-Checked $generator $arguments
    [ordered]@{
        lockSha256 = (Get-FileHash -LiteralPath $lockFile -Algorithm SHA256).Hash
        projectRoot = $ProjectRoot
        windowsSdk = $sdkMetadata
        sdkMetadataSha256 = (Get-FileHash -LiteralPath $sdkMetadata -Algorithm SHA256).Hash
        generator = $generator
    } | ConvertTo-Json | Set-Content -LiteralPath (Get-ProjectPath '.deps/winui-island/prepared.json')
    Write-Host 'WinUI probe dependencies ready. Configure/build is offline; no system runtime was installed.'
}
