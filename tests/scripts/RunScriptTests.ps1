#requires -Version 7.0
# Isolated script regression checks; no compiler/Qt installation or Pester required.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$output = Join-Path $root ('out/validation/run-script-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
$fixture = Join-Path $output 'project space'
$fixtureScripts = Join-Path $fixture 'scripts'
New-Item -ItemType Directory -Force $fixtureScripts | Out-Null
Copy-Item -LiteralPath (Join-Path $root 'scripts/run.ps1') -Destination $fixtureScripts
foreach ($inputFile in @('CMakePresets.json', 'toolchain/dependencies.lock.json', 'toolchain/qt-static.lock.json',
    '.local/toolchain.local.json', 'LICENSE', 'docs/THIRD_PARTY.md', 'docs/PACKAGING.md', 'resources/fluent/LICENSE')) {
    $destination = Join-Path $fixture $inputFile
    New-Item -ItemType Directory -Force (Split-Path $destination) | Out-Null
    if ($inputFile -eq '.local/toolchain.local.json') { Set-Content $destination '{}' }
    else { Copy-Item -LiteralPath (Join-Path $root $inputFile) -Destination $destination }
}
$common = @'
. '__COMMON__'
$script:ProjectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
function Record-Call($Value) { Add-Content (Join-Path $ProjectRoot 'calls.jsonl') ($Value | ConvertTo-Json -Compress -Depth 5) }
function Initialize-Toolchain([switch] $StaticQt) {
    Record-Call @{ kind='init'; static=[bool]$StaticQt }
    return @{ CMake='test-cmake'; Qt=(Join-Path $ProjectRoot 'test-qt') }
}
function Get-MsvcIncludesPrefix { return 'test-prefix: ' }
function Invoke-Checked([string] $Executable, [string[]] $Arguments) {
    $kind = if ($Executable -ne 'test-cmake') { 'launch' } elseif ($Arguments[0] -eq '--preset') { 'configure' } else { 'build' }
    Record-Call @{ kind=$kind; exe=$Executable; arguments=$Arguments; path=$env:PATH; temp=$env:TEMP; qml=$env:QML_IMPORT_PATH }
    $failure = Join-Path $ProjectRoot 'failure.txt'
    if ((Get-Content $failure -Raw).Trim() -eq $kind) { throw "Injected $kind failure" }
    if ($kind -eq 'configure') {
        New-Item -ItemType Directory -Force $Arguments[3] | Out-Null
        $mode = if ($Arguments[1] -eq 'windows-debug') { 'Debug' } else { 'Release' }
        $static = if ($Arguments[1] -eq 'windows-static-release') { 'TRUE' } else { 'OFF' }
        Set-Content (Join-Path $Arguments[3] 'CMakeCache.txt') "CMAKE_BUILD_TYPE:STRING=$mode`nDANMAKU_STATIC_QT:BOOL=$static"
        Set-Content (Join-Path $Arguments[3] 'build.ninja') 'mock build graph'
    } elseif ($kind -eq 'build') {
        $destination = Join-Path $Arguments[1] 'bin/DanmaX.exe'
        New-Item -ItemType Directory -Force (Split-Path $destination) | Out-Null
        if (-not (Test-Path $destination)) { Set-Content $destination 'test executable placeholder' }
    } elseif ($kind -eq 'launch') {
        $directory = Split-Path $Executable
        if (-not (Test-Path (Join-Path $directory 'settings.ini'))) { Set-Content (Join-Path $directory 'settings.ini') 'preserve settings' }
        Add-Content (Join-Path $directory 'app.log') 'test launch'
    }
}
'@
Set-Content (Join-Path $fixtureScripts 'common.ps1') $common.Replace('__COMMON__', (Join-Path $root 'scripts/common.ps1').Replace("'", "''"))
$deployment = @'
. '__DEPLOYMENT__'
function Invoke-QtDeployment([string] $Directory, [hashtable] $Tools, [switch] $Debug, [switch] $Force) {
    Record-Call @{ kind='deploy'; debug=[bool]$Debug; force=[bool]$Force; directory=$Directory }
    if ((Get-Content (Join-Path $ProjectRoot 'failure.txt') -Raw).Trim() -eq 'deploy') { throw 'Injected deploy failure' }
    $suffix = if ($Debug) { 'd' } else { '' }
    $path = Join-Path $Directory "Qt6Core$suffix.dll"
    Set-Content $path 'test dependency'
    $path
}
'@
Set-Content (Join-Path $fixtureScripts 'deployment.ps1') $deployment.Replace('__DEPLOYMENT__', (Join-Path $root 'scripts/deployment.ps1').Replace("'", "''"))
$fixtureRun = Join-Path $fixtureScripts 'run.ps1'
$results = [Collections.Generic.List[string]]::new()
function Assert($Condition, [string] $Message) { if (-not $Condition) { throw $Message } }
function Reset-Calls {
    Set-Content (Join-Path $fixture 'calls.jsonl') ''
    Set-Content (Join-Path $fixture 'failure.txt') ''
}
function Get-Calls { @(Get-Content (Join-Path $fixture 'calls.jsonl') | Where-Object { $_ } | ConvertFrom-Json) }
function Invoke-Run([hashtable] $Parameters = @{}) { & $fixtureRun @Parameters *> (Join-Path $output 'last-output.txt') }
function Expect-Failure([hashtable] $Parameters, [string] $Pattern) {
    $failed = $false
    try { Invoke-Run $Parameters }
    catch { Assert ($_.Exception.Message -like $Pattern) "Unexpected error: $_"; $failed = $true }
    Assert $failed 'Expected failure did not occur'
    Assert (@(Get-Calls | Where-Object kind -eq 'launch').Count -eq 0) 'Launched after failure'
}
$debug = Join-Path $fixture 'out/run/windows-debug'
Reset-Calls
Invoke-Run @{ SmokeTest=$true }
$calls = @(Get-Calls)
Assert (($calls.kind -join ',') -eq 'init,configure,build,deploy,launch') 'First run did not build/deploy/launch in order'
Assert ($calls[-1].exe -eq (Join-Path $debug 'DanmaX.exe')) 'Launched build executable instead of deployed executable'
Assert ($calls[-1].arguments -contains '--smoke-test') 'SmokeTest argument lost'
Assert ($calls[-1].path -notlike '*test-qt*' -and -not $calls[-1].qml) 'SDK paths leaked into runtime'
Assert ($calls[-1].temp -eq (Join-Path $debug 'cache/tmp')) 'Runtime TEMP is shared'
$results.Add('first build, correct Qt mode, deployed launch and isolated environment')

$settings = (Get-FileHash (Join-Path $debug 'settings.ini')).Hash
Set-Content (Join-Path $debug 'cache/keep.txt') 'preserve cache'
Reset-Calls
Invoke-Run
Assert (((Get-Calls).kind -join ',') -eq 'init,build,launch') 'Unchanged configuration/deployment was repeated'
Assert ((Get-FileHash (Join-Path $debug 'settings.ini')).Hash -eq $settings) 'Settings overwritten'
Assert ((Get-Content (Join-Path $debug 'cache/keep.txt') -Raw).Trim() -eq 'preserve cache') 'Cache overwritten'
$results.Add('unchanged configure/deployment reused and user data preserved')

foreach ($file in @('CMakePresets.json', 'out/build/windows-debug/CMakeCache.txt')) {
    Reset-Calls
    Add-Content (Join-Path $fixture $file) ' '
    Invoke-Run
    Assert ((Get-Calls).kind -contains 'configure') 'Changed configuration not detected'
}
$results.Add('preset and CMake cache changes trigger configure')

Reset-Calls
$outside = Split-Path $root
Push-Location $outside
try { Invoke-Run @{ BuildDirectory='out/build/custom space' }; Assert ((Get-Location).Path -eq $outside) 'Caller location changed' }
finally { Pop-Location }
$custom = Join-Path $fixture 'out/run/custom/windows-debug/custom space/DanmaX.exe'
Assert ((Get-Calls)[-1].exe -eq $custom) 'Custom build did not receive an isolated runtime'
$results.Add('custom directory with spaces and caller location restoration')

Reset-Calls
Invoke-Run @{ Preset='windows-release' }
$release = Join-Path $fixture 'out/run/windows-release'
Assert ((Get-Calls)[-1].exe -eq (Join-Path $release 'DanmaX.exe')) 'Release shares Debug runtime'
Assert (-not (@(Get-Calls | Where-Object kind -eq 'deploy')[0].debug)) 'Release deployed Debug libraries'
Assert (Test-Path (Join-Path $debug 'Qt6Cored.dll')) 'Release changed Debug dependencies'
$results.Add('Debug and Release executables, dependencies and data are independent')

Reset-Calls
Invoke-Run @{ NoBuild=$true }
Assert (((Get-Calls).kind -join ',') -eq 'init,launch') 'NoBuild compiled or unnecessarily deployed'
$results.Add('NoBuild reuses matching build and runtime')
Reset-Calls
Expect-Failure @{ NoBuild=$true; BuildDirectory='out/build/missing' } '*No executable found*'
Reset-Calls
Expect-Failure @{ NoBuild=$true; Preset='windows-release'; BuildDirectory='out/build/windows-debug' } '*does not match*'
Assert (@(Get-Calls).Count -eq 0) 'Invalid NoBuild reached toolchain or process shutdown'
$results.Add('missing/mismatched NoBuild fails before shutdown')

foreach ($stage in @('configure', 'build', 'deploy')) {
    Reset-Calls
    Set-Content (Join-Path $fixture 'failure.txt') $stage
    if ($stage -eq 'configure') { Add-Content (Join-Path $fixture 'CMakePresets.json') ' ' }
    if ($stage -eq 'deploy') { Add-Content (Join-Path $fixture 'out/build/windows-debug/bin/DanmaX.exe') 'new build' }
    Expect-Failure @{} "*Injected $stage failure*"
    $results.Add("$stage failure blocks launch of stale runtime")
}
Reset-Calls
Invoke-Run
$dependency = Join-Path $debug 'Qt6Cored.dll'
Remove-Item -LiteralPath $dependency
Reset-Calls
Invoke-Run @{ NoBuild=$true }
Assert (Test-Path $dependency) 'Missing dependency was not repaired'
Assert (@(Get-Calls | Where-Object kind -eq 'deploy')[0].directory -like '*out\stage\runtime-repair-*') 'Repair did not use a fresh deployment stage'
Assert ((Get-FileHash (Join-Path $debug 'settings.ini')).Hash -eq $settings) 'Repair overwrote user settings'
$results.Add('missing dependency repaired without overwriting user settings')
Set-Content -LiteralPath $dependency 'corrupted dependency'
Reset-Calls
Invoke-Run @{ NoBuild=$true }
Assert ((Get-Content -LiteralPath $dependency -Raw).Trim() -eq 'test dependency') 'Modified dependency was not restored'
Assert ((Get-FileHash (Join-Path $debug 'settings.ini')).Hash -eq $settings) 'Modified dependency repair changed settings'
$results.Add('modified dependency restored through a disposable stage')

Reset-Calls
Invoke-Run @{ Preset='windows-static-release' }
Assert (@(Get-Calls | Where-Object kind -eq 'deploy').Count -eq 0) 'Static runtime deployed dynamic Qt'
Assert ((Get-Calls)[-1].exe -eq (Join-Path $fixture 'out/run/windows-static-release/DanmaX.exe')) 'Static runtime was not isolated'
Reset-Calls
Invoke-Run @{ Preset='windows-static-release'; NoBuild=$true }
Assert (((Get-Calls).kind -join ',') -eq 'init,launch') 'Static NoBuild did not accept a CMake TRUE boolean'
$results.Add('static runtime and NoBuild use independent directory without dynamic deployment')

Reset-Calls
Expect-Failure @{ BuildDirectory='out/not-build' } '*under project out/build*'
. (Join-Path $root 'scripts/common.ps1')
Assert ((Get-ProjectPath (Join-Path $root 'out/run/windows-debug')) -eq (Join-Path $root 'out/run/windows-debug')) 'Absolute project path did not resolve correctly'
$rejected = $false
try { [void](Get-ProjectPath (Join-Path (Split-Path $root) 'outside')) } catch { $rejected = $true }
Assert $rejected 'Absolute path outside project accepted'
$results.Add('relative/absolute project paths bounded and invalid build directories rejected')
$script:ProjectRoot = $fixture
. (Join-Path $root 'scripts/deployment.ps1')
$junctionTarget = Join-Path $fixture 'out/validation/junction-target'
$junction = Join-Path $debug 'blocked-link'
New-Item -ItemType Directory -Force $junctionTarget | Out-Null
Set-Content (Join-Path $junctionTarget 'keep.txt') 'preserve'
New-Item -ItemType Junction -Path $junction -Target $junctionTarget | Out-Null
try {
    $rejected = $false
    try { Assert-DeploymentDirectory $debug } catch { $rejected = $_.Exception.Message -like '*reparse point*' }
    Assert $rejected 'Deployment containing a junction was accepted'
    Assert ((Get-Content (Join-Path $junctionTarget 'keep.txt') -Raw).Trim() -eq 'preserve') 'Junction target modified'
} finally { [IO.Directory]::Delete($junction) }
$results.Add('deployment rejects junctions and preserves their targets')
$results | Set-Content (Join-Path $output 'results.txt')
$results | ForEach-Object { Write-Host "PASS: $_" }
Write-Host "$($results.Count) checks passed; reports: $output"
