#requires -Version 7.0
param(
    [Parameter(Mandatory)][string] $LibraryPath,
    [switch] $NoBuild
)
. "$PSScriptRoot/common.ps1"
# A caller-relative media path must be resolved before entering the project environment.
$libraryRoot = (Resolve-Path -LiteralPath $LibraryPath -ErrorAction Stop).ProviderPath
if (!(Test-Path -LiteralPath $libraryRoot -PathType Container)) { throw 'LibraryPath must be a directory.' }
if (!$NoBuild) { & "$PSScriptRoot/test-anitomy-probe.ps1" -Configuration Release }
Invoke-ProjectEnvironment {
    $tools = Initialize-Toolchain
    $reportRoot = Get-ProjectPath 'out/validation/anitomy-library'
    New-Item -ItemType Directory -Force $reportRoot | Out-Null
    $extensions = @('.mkv','.mp4','.avi','.webm','.mov','.m4v','.ts','.m2ts','.mts','.flv','.wmv',
        '.mpg','.mpeg','.divx','.ogm','.ogv','.rm','.rmvb','.3gp','.3g2','.vob','.asf')
    $scanErrors = @()
    $topDirectories = @(Get-ChildItem -LiteralPath $libraryRoot -Directory -Force -ErrorAction SilentlyContinue -ErrorVariable +scanErrors)
    # Enumeration only: never open media, rename files, follow directory links or query a remote API.
    $allFiles = @(Get-ChildItem -LiteralPath $libraryRoot -Recurse -File -Force -ErrorAction SilentlyContinue -ErrorVariable +scanErrors)
    $videos = @($allFiles | Where-Object { $_.Extension.ToLowerInvariant() -in $extensions } | Sort-Object FullName)
    $inventoryPath = Join-Path $reportRoot 'inventory.json'
    [ordered]@{
        root = $libraryRoot
        scannedAtUtc = [DateTime]::UtcNow.ToString('O')
        allFileCount = $allFiles.Count
        topDirectories = @($topDirectories | Select-Object -ExpandProperty Name)
        videoExtensions = $extensions
        scanErrors = @($scanErrors | ForEach-Object { $_.ToString() })
        files = @($videos | ForEach-Object { [ordered]@{
            relativePath = [IO.Path]::GetRelativePath($libraryRoot, $_.FullName)
            filename = $_.Name
            parent = [IO.Path]::GetRelativePath($libraryRoot, $_.DirectoryName)
        } })
    } | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $inventoryPath -Encoding utf8
    if ($scanErrors.Count) { Write-Warning "Enumeration had $($scanErrors.Count) errors; inventory coverage is incomplete." }
    $executable = Get-ProjectPath 'out/build/anitomy-evaluation-release/bin/anitomy_probe.exe'
    if (!(Test-Path -LiteralPath $executable)) { throw 'Build the Release Anitomy probe first.' }
    Invoke-Checked $executable @('--inventory', $inventoryPath, '--audit-report', (Join-Path $reportRoot 'audit.json'))
    $audit = Get-Content -LiteralPath (Join-Path $reportRoot 'audit.json') -Raw | ConvertFrom-Json
    function Join-AnitomyField($Object, [string] $Name) {
        $property = $Object.PSObject.Properties[$Name]
        if ($null -ne $property) { return @($property.Value) -join ' | ' }
        return ''
    }
    $audit.rows | ForEach-Object { [pscustomobject]@{
        RelativePath = $_.relativePath
        CurrentTitle = $_.current.title
        CurrentEpisode = $_.current.episode
        CurrentValid = $_.current.valid
        AnitomyTitle = Join-AnitomyField $_.anitomy 'anime_title'
        AnitomyEpisode = Join-AnitomyField $_.anitomy 'episode_number'
        AnitomySeason = Join-AnitomyField $_.anitomy 'anime_season'
        AnitomySingleFields = $_.anitomySingleFields
        Issues = @($_.issues) -join ', '
    } } | Export-Csv -LiteralPath (Join-Path $reportRoot 'audit.csv') -NoTypeInformation -Encoding utf8BOM
    $groups = @($audit.rows | Group-Object {
        $parts = $_.relativePath -split '[\\/]'
        if ($parts.Count -gt 1) { $parts[0] } else { '[root video]' }
    } | ForEach-Object {
        $files = @($_.Group)
        [pscustomobject]@{
            Folder = $_.Name
            Videos = $files.Count
            AnitomySingleFields = @($files | Where-Object anitomySingleFields).Count
            CurrentFields = @($files | Where-Object { $_.current.valid }).Count
            EpisodeConflicts = @($files | Where-Object episodeConflict).Count
            AnitomyTitles = @($files | ForEach-Object { Join-AnitomyField $_.anitomy 'anime_title' } | Sort-Object -Unique) -join ' / '
        }
    })
    $groups | Export-Csv -LiteralPath (Join-Path $reportRoot 'groups.csv') -NoTypeInformation -Encoding utf8BOM
    $summary = [Collections.Generic.List[string]]::new()
    $summary.Add('# 本地媒体库文件名解析报告')
    $summary.Add('')
    $summary.Add("扫描目录：$libraryRoot")
    $summary.Add('')
    $summary.Add("视频 $($audit.videoFiles) 个；文件总数 $($allFiles.Count)；扫描错误 $($scanErrors.Count) 个。只读取文件名和目录元数据，没有读取视频内容、修改媒体文件或调用在线服务。")
    $summary.Add('')
    $summary.Add('| 指标 | 数量 |')
    $summary.Add('|---|---:|')
    $summary.Add("| Anitomy 可提取单集字段 | $($audit.anitomySingleFields) |")
    $summary.Add("| 当前解析器返回有效字段 | $($audit.currentFields) |")
    $summary.Add("| 两者均有字段 | $($audit.bothFields) |")
    $summary.Add("| 两者集数不同 | $($audit.episodeConflicts) |")
    $summary.Add("| 两者规范化番名或季数不同 | $($audit.titleDifferences) |")
    $summary.Add('')
    $summary.Add('以上是字段可提取数量，不是正确率或在线匹配成功率。SP、PV、OP/ED 和菜单也可能产生集数；未使用父目录推断番名，也未对无编号电影强制设置第 1 集。')
    $summary.Add('')
    $summary.Add('| 顶层视频目录／根目录视频 | 视频数 | Anitomy 单集字段 | 当前字段 |')
    $summary.Add('|---|---:|---:|---:|')
    foreach ($group in $groups) {
        $escapedName = $group.Folder.Replace('|','&#124;')
        $summary.Add("| $escapedName | $($group.Videos) | $($group.AnitomySingleFields) | $($group.CurrentFields) |")
    }
    $videoTopFolders = @($groups.Folder)
    $emptyVideoFolders = @($topDirectories | Where-Object { $_.Name -notin $videoTopFolders })
    if ($emptyVideoFolders.Count) {
        $summary.Add('')
        $summary.Add('扫描到但没有散装视频文件的顶层目录（不检查压缩包内部）：')
        $summary.Add('')
        foreach ($folder in $emptyVideoFolders) { $summary.Add('- ' + $folder.Name) }
    }
    $summary.Add('')
    $summary.Add('逐文件结果见 [audit.csv](audit.csv)，原始解析字段和风险标记见 [audit.json](audit.json)，目录汇总见 [groups.csv](groups.csv)。')
    $summary | Set-Content -LiteralPath (Join-Path $reportRoot 'README.md') -Encoding utf8
    Write-Host "Scanned $($videos.Count) videos out of $($allFiles.Count) files. Report: $reportRoot"
}
