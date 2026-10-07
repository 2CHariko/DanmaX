# Packaging helpers; sourced by package.ps1 after common.ps1.
function New-PortableExecutable([string] $Stage, [hashtable] $Tools, [string] $Timestamp) {
    $work = Get-ProjectPath "out/packages/work-$Timestamp"
    $output = Get-ProjectPath "out/packages/LocalDanmaku-$Timestamp"
    New-Item -ItemType Directory -Force $work,$output | Out-Null
    $makecab = Join-Path ([Environment]::GetFolderPath('System')) 'makecab.exe'
    if (-not (Test-Path -LiteralPath $makecab)) { throw 'Windows system makecab.exe is required for single-exe packaging.' }
    $files = @(Get-ChildItem -LiteralPath $Stage -File -Recurse | Sort-Object FullName)
    $manifest = [Collections.Generic.List[string]]::new()
    $directives = [Collections.Generic.List[string]]::new()
    foreach ($line in @('.OPTION EXPLICIT', '.Set Cabinet=on', '.Set Compress=on',
        '.Set CompressionType=LZX', '.Set CompressionMemory=21', '.Set CabinetNameTemplate=payload.cab',
        '.Set MaxDiskSize=0', '.Set MaxCabinetSize=0',
        ".Set DiskDirectoryTemplate=../../packages/work-$Timestamp", '.Set RptFileName=NUL', '.Set InfFileName=NUL')) {
        $directives.Add($line)
    }
    foreach ($file in $files) {
        $relative = [IO.Path]::GetRelativePath($Stage, $file.FullName)
        # CAB member names are ASCII; project and working directory paths may be Unicode.
        if ($relative -match '[^\x20-\x7e]|["\t\r\n]') { throw "Unsupported CAB member name: $relative" }
        $manifest.Add("$((Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant())`t$($file.Length)`t$relative")
        $directives.Add(('"{0}" "{0}"' -f $relative))
    }
    $ddf = Join-Path $work 'payload.ddf'
    $manifestFile = Join-Path $work 'payload.tsv'
    [IO.File]::WriteAllLines($ddf, $directives, [Text.Encoding]::ASCII)
    [IO.File]::WriteAllText($manifestFile, ($manifest -join "`n") + "`n", [Text.UTF8Encoding]::new($false))
    $previousLocation = Get-Location
    try {
        Set-Location $Stage
        $cabOutput = & $makecab /V0 /F $ddf 2>&1
        $cabExit = $LASTEXITCODE
        $cabOutput | Set-Content -LiteralPath (Join-Path $work 'makecab.log') -Encoding utf8
        if ($cabExit -ne 0) { throw "makecab failed; see $work/makecab.log" }
    } finally { Set-Location $previousLocation }
    $cabinet = Join-Path $work 'payload.cab'
    if (-not (Test-Path -LiteralPath $cabinet)) { throw 'makecab did not produce the expected single cabinet.' }
    $build = Get-ProjectPath "out/build/portable-$Timestamp"
    $includePrefix = Get-MsvcIncludesPrefix
    Invoke-Checked $Tools.CMake @('-S',$ProjectRoot,'-B',$build,'-G','Ninja',
        "-DCMAKE_MAKE_PROGRAM=$($Tools.Ninja)",'-DCMAKE_BUILD_TYPE=Release', '-DDANMAKU_BUILD_APP=OFF', '-DBUILD_TESTING=OFF',
        "-DDANMAKU_MSVC_INCLUDES_PREFIX=$includePrefix",
        "-DPORTABLE_CABINET=$($cabinet.Replace('\','/'))", "-DPORTABLE_MANIFEST=$($manifestFile.Replace('\','/'))")
    Invoke-Checked $Tools.CMake @('--build',$build,'--target','danmaku_portable','--parallel')
    $exe = Join-Path $output 'LocalDanmaku.exe'
    Copy-Item -LiteralPath (Join-Path $build 'bin/LocalDanmaku.exe') -Destination $exe
    $report = [ordered]@{
        format = 'single-exe'; qtVersion = $DependencyLock.qtVersion
        executable = [IO.Path]::GetRelativePath($ProjectRoot, $exe)
        executableBytes = (Get-Item -LiteralPath $exe).Length
        executableSha256 = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant()
        payloadFiles = $files.Count; payloadBytes = ($files | Measure-Object Length -Sum).Sum
        makecab = @{ path = $makecab; version = (Get-Item -LiteralPath $makecab).VersionInfo.FileVersion
            sha256 = (Get-FileHash -LiteralPath $makecab -Algorithm SHA256).Hash.ToLowerInvariant()
            source = 'Windows system component; not redistributed' }
        extraction = 'data/runtime/<first-24-hex-of-cabinet-sha256>'; verification = 'SHA-256 for every runtime file on every launch'
        runtimePrerequisite = 'Official Microsoft VC++ x64 runtime; not embedded'
    }
    $report | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $work 'package-report.json') -Encoding utf8
    Write-Host "Single-exe development package: $exe"
    Write-Host "Compressed EXE: $([Math]::Round($report.executableBytes / 1MB, 1)) MiB; runtime files: $($files.Count)"
    Write-Host 'First launch extracts to adjacent data/runtime; settings and caches remain in adjacent data.'
}
