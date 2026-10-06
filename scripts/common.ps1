#requires -Version 7.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$script:ProjectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$script:DependencyLock = Get-Content (Join-Path $ProjectRoot 'toolchain/dependencies.lock.json') -Raw | ConvertFrom-Json

function Get-ProjectPath([string] $Relative) {
    $path = [IO.Path]::GetFullPath((Join-Path $ProjectRoot $Relative))
    if (-not $path.StartsWith($ProjectRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Path must remain within the project: $Relative"
    }
    return $path
}

function Invoke-Checked([string] $Executable, [string[]] $Arguments) {
    # A pipeline makes PowerShell wait for Windows GUI executables as well.
    & $Executable @Arguments | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "$Executable failed with exit code $LASTEXITCODE" }
}

function Invoke-ProjectEnvironment([scriptblock] $Action) {
    $before = @{}
    Get-ChildItem Env: | ForEach-Object { $before[$_.Name] = $_.Value }
    $oldLocation = Get-Location
    try {
        $env:TEMP = Get-ProjectPath '.cache/tmp'
        $env:TMP = $env:TEMP
        $env:QML_DISK_CACHE_PATH = Get-ProjectPath ".cache/qml/$($DependencyLock.qtVersion)"
        New-Item -ItemType Directory -Force $env:TEMP,$env:QML_DISK_CACHE_PATH | Out-Null
        # Never inherit another Qt installation or an unrelated QML import path.
        foreach ($name in @('QTDIR','QT_PLUGIN_PATH','QT_QPA_PLATFORM_PLUGIN_PATH','QML_IMPORT_PATH','QML2_IMPORT_PATH')) {
            [Environment]::SetEnvironmentVariable($name, $null, 'Process')
        }
        Set-Location $ProjectRoot
        & $Action
    } finally {
        foreach ($entry in @(Get-ChildItem Env:)) {
            if (-not $before.ContainsKey($entry.Name)) { [Environment]::SetEnvironmentVariable($entry.Name, $null, 'Process') }
        }
        foreach ($entry in $before.GetEnumerator()) { [Environment]::SetEnvironmentVariable($entry.Key, $entry.Value, 'Process') }
        Set-Location $oldLocation
    }
}

function Get-ToolPaths {
    return @{
        CMake = Get-ProjectPath ".tools/cmake/$($DependencyLock.cmakeVersion)/bin/cmake.exe"
        CTest = Get-ProjectPath ".tools/cmake/$($DependencyLock.cmakeVersion)/bin/ctest.exe"
        Ninja = Get-ProjectPath ".tools/ninja/$($DependencyLock.ninjaVersion)/ninja.exe"
        Qt = Get-ProjectPath ".tools/qt/$($DependencyLock.qtVersion)/msvc2022_64"
    }
}

function Initialize-Toolchain([switch] $CoreOnly) {
    $tools = Get-ToolPaths
    foreach ($path in @($tools.CMake, $tools.CTest, $tools.Ninja)) {
        if (-not (Test-Path -LiteralPath $path)) { throw "Missing $path. Run scripts/bootstrap.ps1 first." }
    }
    if (-not $CoreOnly -and -not (Test-Path -LiteralPath (Join-Path $tools.Qt 'bin/Qt6Core.dll'))) {
        throw 'Project-local Qt is missing. Run scripts/bootstrap.ps1 first.'
    }
    $localFile = Get-ProjectPath '.local/toolchain.local.json'
    if (-not (Test-Path -LiteralPath $localFile)) {
        throw 'Configure the system MSVC/SDK exception in .local/toolchain.local.json; see toolchain/local.example.json.'
    }
    $local = Get-Content -LiteralPath $localFile -Raw | ConvertFrom-Json
    if (-not $local.allowExternalSystemToolchain) { throw 'The external system toolchain exception is not enabled.' }
    $devcmd = Join-Path $local.visualStudioPath 'Common7/Tools/VsDevCmd.bat'
    if (-not (Test-Path -LiteralPath $devcmd)) { throw "VsDevCmd.bat missing: $devcmd" }
    Write-Host "System toolchain exception: $($local.visualStudioPath)"
    # The batch file only establishes this process's compiler environment.
    $command = 'call "{0}" -arch=x64 -host_arch=x64 -vcvars_ver={1} -winsdk={2} >nul && set' -f $devcmd,$DependencyLock.msvcToolsetVersion,$DependencyLock.windowsSdkVersion
    $lines = & $env:ComSpec /d /s /c $command
    if ($LASTEXITCODE -ne 0) { throw 'MSVC environment initialization failed.' }
    foreach ($line in $lines) {
        if ($line -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process') }
    }
    if ($env:VCToolsVersion.TrimEnd('\') -ne $DependencyLock.msvcToolsetVersion) { throw 'MSVC toolset does not match the lock.' }
    if ($env:WindowsSDKVersion.TrimEnd('\') -ne $DependencyLock.windowsSdkVersion) { throw 'Windows SDK does not match the lock.' }
    $env:VSLANG = '1033'
    $env:PATH = "$(Split-Path $tools.CMake);$(Split-Path $tools.Ninja);$($tools.Qt)/bin;$env:PATH"
    $env:QT_PLUGIN_PATH = Join-Path $tools.Qt 'plugins'
    $env:QML_IMPORT_PATH = Join-Path $tools.Qt 'qml'
    Write-Host "CMake: $($tools.CMake)"
    Write-Host "Qt: $($tools.Qt)"
    Write-Host "MSVC: $env:VCToolsInstallDir"
    Write-Host "Windows SDK: $env:WindowsSdkDir ($env:WindowsSDKVersion)"
    return $tools
}

function Get-MsvcIncludesPrefix {
    $probe = Get-ProjectPath '.cache/tmp/include-probe.cpp'
    Set-Content -LiteralPath $probe -Value '#include <stddef.h>' -Encoding ascii
    $info = [Diagnostics.ProcessStartInfo]::new()
    $info.FileName = Join-Path $env:VCToolsInstallDir 'bin/Hostx64/x64/cl.exe'
    $info.UseShellExecute = $false
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $encoding = [Text.Encoding]::GetEncoding([Globalization.CultureInfo]::CurrentCulture.TextInfo.ANSICodePage)
    $info.StandardOutputEncoding = $encoding
    $info.StandardErrorEncoding = $encoding
    foreach ($arg in @('/nologo','/EP','/showIncludes',$probe)) { $info.ArgumentList.Add($arg) }
    $process = [Diagnostics.Process]::Start($info)
    $stdout = $process.StandardOutput.ReadToEndAsync()
    $stderr = $process.StandardError.ReadToEndAsync()
    $process.WaitForExit()
    if ($process.ExitCode -ne 0) { throw 'MSVC include-prefix probe failed.' }
    $output = $stdout.Result + "`n" + $stderr.Result
    $process.Dispose()
    if ($output -match '(?m)^([^\r\n]*: +)[A-Za-z]:[\\/]') { return $Matches[1] }
    throw 'Could not detect compiler header dependency prefix.'
}
