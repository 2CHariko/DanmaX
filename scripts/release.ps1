#requires -Version 7.0
param(
    [int] $BuildNumber = 0
)
. "$PSScriptRoot/common.ps1"
. "$PSScriptRoot/portable.ps1"

Invoke-ProjectEnvironment {
    $cmakeFile = Get-ProjectPath 'CMakeLists.txt'
    $cmakeText = Get-Content -LiteralPath $cmakeFile -Raw
    if ($cmakeText -match '(?m)^project\s*\(\s*DanmaX\s+VERSION\s+([0-9.]+)') {
        $version = $Matches[1]
    } else {
        throw "Cannot determine DanmaX version from $cmakeFile"
    }

    $buildNumberFile = Get-ProjectPath '.local/build_number'
    if (-not (Test-Path -LiteralPath $buildNumberFile) -and (Test-Path -LiteralPath (Get-ProjectPath '.build_number'))) {
        $buildNumberFile = Get-ProjectPath '.build_number'
    }
    $parentDir = [IO.Path]::GetDirectoryName($buildNumberFile)
    if (-not (Test-Path -LiteralPath $parentDir)) { New-Item -ItemType Directory -Force $parentDir | Out-Null }
    if ($BuildNumber -le 0) {
        $currentBuild = 0
        if (Test-Path -LiteralPath $buildNumberFile) {
            $raw = (Get-Content -LiteralPath $buildNumberFile -Raw).Trim()
            if ($raw -match '^\d+$') {
                $currentBuild = [int]$raw
            }
        }
        if ($currentBuild -le 0) {
            try {
                $gitCount = (& git rev-list --count HEAD 2>$null)
                if ($gitCount -match '^\d+$') {
                    $currentBuild = [int]$gitCount
                }
            } catch {}
        }
        $BuildNumber = $currentBuild + 1
        Set-Content -LiteralPath $buildNumberFile -Value "$BuildNumber`n" -Encoding utf8
    }

    Write-Host "========================================="
    Write-Host "  Building and Packaging DanmaX v$version (Build $BuildNumber)"
    Write-Host "========================================="
    $releaseDir = Get-ProjectPath "out/release/v$version"
    New-Item -ItemType Directory -Force $releaseDir | Out-Null

    # 1. 编译纯静态单 EXE 便携版
    Write-Host "`n[1/3] 编译纯静态单 EXE 便携版 (windows-static-release)..."
    $staticTools = Initialize-Toolchain -StaticQt:$true
    $includePrefix = Get-MsvcIncludesPrefix
    Invoke-Checked $staticTools.CMake @('--preset', 'windows-static-release', "-DDANMAKU_MSVC_INCLUDES_PREFIX=$includePrefix", "-DDANMAX_BUILD_NUMBER=$BuildNumber")
    Invoke-Checked $staticTools.CMake @('--build', '--preset', 'windows-static-release', '--target', 'danmaku_app', '--parallel')

    $staticSrc = Get-ProjectPath "out/build/windows-static-release/bin/DanmaX.exe"
    if (-not (Test-Path -LiteralPath $staticSrc)) { throw "Static executable not found at $staticSrc" }

    $portableExeName = "DanmaX-v$version-windows-x64-portable.exe"
    $portableExePath = Join-Path $releaseDir $portableExeName
    Copy-Item -LiteralPath $staticSrc -Destination $portableExePath -Force

    # 校验依赖（保证真正静态无依赖）
    $dumpbin = Join-Path $env:VCToolsInstallDir 'bin/Hostx64/x64/dumpbin.exe'
    $imports = & $dumpbin /DEPENDENTS $portableExePath
    if (($imports -join "`n") -match '(?im)^\s+(Qt\d.*|MSVCP\d.*|VCRUNTIME\d.*|CONCRT\d.*|dxcompiler|dxil)\.dll\s*$') {
        throw "Static package imports disallowed runtime DLLs."
    }

    # 2. 生成带外壳文件夹的免安装便携 ZIP 压缩包
    Write-Host "`n[2/3] 打包带外壳文件夹的单文件便携 ZIP 压缩包..."
    $stageDir = Join-Path $releaseDir "DanmaX-v$version-windows-x64"
    if (Test-Path -LiteralPath $stageDir) { Remove-Item -LiteralPath $stageDir -Recurse -Force }
    New-Item -ItemType Directory -Force $stageDir | Out-Null

    Copy-Item -LiteralPath $staticSrc -Destination (Join-Path $stageDir 'DanmaX.exe')
    Copy-Item -LiteralPath (Get-ProjectPath 'LICENSE') -Destination $stageDir
    Copy-Item -LiteralPath (Get-ProjectPath 'README.md') -Destination $stageDir
    Copy-Item -LiteralPath (Get-ProjectPath 'docs/THIRD_PARTY.md') -Destination $stageDir
    Copy-Item -LiteralPath (Get-ProjectPath 'resources/fluent/LICENSE') -Destination (Join-Path $stageDir 'Fluent-System-Icons-LICENSE.txt')

    $zipName = "DanmaX-v$version-windows-x64.zip"
    $zipPath = Join-Path $releaseDir $zipName
    if (Test-Path -LiteralPath $zipPath) { Remove-Item -LiteralPath $zipPath -Force }

    # 将 $stageDir 自身作为根目录压缩（确保解压后包含 DanmaX-vX.Y.Z-windows-x64 外壳文件夹）
    Compress-Archive -Path $stageDir -DestinationPath $zipPath -CompressionLevel Optimal

    # 3. 生成 SHA-256 校验清单
    Write-Host "`n[3/3] 计算发布文件校验和 (SHA-256)..."
    $hashPortable = (Get-FileHash -LiteralPath $portableExePath -Algorithm SHA256).Hash.ToLowerInvariant()
    $hashZip = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLowerInvariant()

    $checksumContent = @"
$hashPortable  $portableExeName
$hashZip  $zipName
"@
    Set-Content -LiteralPath (Join-Path $releaseDir 'checksums.txt') -Value $checksumContent -Encoding utf8

    $displayVer = (Get-Item $portableExePath).VersionInfo.FileVersion
    Write-Host "`n========================================="
    Write-Host "发布归档已完成！版本: v$version (文件版本: $displayVer)"
    Write-Host "输出路径:"
    Write-Host "1. $portableExePath ($([Math]::Round((Get-Item $portableExePath).Length / 1MB, 2)) MB)"
    Write-Host "2. $zipPath ($([Math]::Round((Get-Item $zipPath).Length / 1MB, 2)) MB)"
    Write-Host "3. $(Join-Path $releaseDir 'checksums.txt')"
    Write-Host "========================================="
}
