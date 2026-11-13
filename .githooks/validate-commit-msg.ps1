#requires -Version 5.1
param(
    [Parameter(Mandatory)]
    [string] $MessageFile
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $MessageFile)) {
    [Console]::Error.WriteLine("[commit-msg] 找不到提交信息文件: $MessageFile")
    exit 1
}

$rawLines = Get-Content -LiteralPath $MessageFile -Encoding utf8
$cleanLines = New-Object System.Collections.Generic.List[string]
foreach ($line in $rawLines) {
    if ($line -match '^# ------------------------ >8 ------------------------$') { break }
    if ($line -match '^\s*#') { continue }
    $cleanLines.Add($line)
}

$header = ($cleanLines | Where-Object { $_.Trim().Length -gt 0 } | Select-Object -First 1)
if ([string]::IsNullOrWhiteSpace($header)) {
    [Console]::Error.WriteLine('[commit-msg] 提交信息不能为空。请遵循 docs/engineering/git-commit-convention.md。')
    exit 1
}

$header = $header.Trim()

if ($header -match '^(Merge |Revert "|fixup! |squash! )') {
    exit 0
}

$pattern = '^(feat|fix|docs|style|refactor|perf|test|build|ci|chore|revert)(\([a-z0-9-]+\))?:\s+(.+)$'
$match = [regex]::Match($header, $pattern)
if (-not $match.Success) {
    [Console]::Error.WriteLine("[commit-msg] 提交标题不符合 Conventional Commits 规范: `"$header`"")
    [Console]::Error.WriteLine('正确格式: <type>(<scope>): <简体中文主题>')
    [Console]::Error.WriteLine('合法 type: feat, fix, docs, style, refactor, perf, test, build, ci, chore, revert')
    [Console]::Error.WriteLine('示例: feat(player): 支持弹幕匹配失败时暂停播放器并弹出手动匹配窗')
    [Console]::Error.WriteLine('详见: docs/engineering/git-commit-convention.md')
    exit 1
}

$subject = $match.Groups[3].Value.Trim()
if ($subject.Length -gt 72) {
    [Console]::Error.WriteLine("[commit-msg] 提交主题过长 ($($subject.Length) > 72 字符): `"$subject`"")
    exit 1
}

if ($subject -match '[。.!！]$') {
    [Console]::Error.WriteLine("[commit-msg] 提交主题末尾不要加标点句号或感叹号: `"$subject`"")
    exit 1
}

if ($subject -notmatch '[\u4e00-\u9fff]') {
    [Console]::Error.WriteLine("[commit-msg] 提交主题必须使用简体中文描述（可包含英文专有名词）: `"$subject`"")
    exit 1
}

exit 0
