#requires -Version 7.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))

Push-Location $root
try {
    & git config --local core.hooksPath .githooks
    if ($LASTEXITCODE -ne 0) { throw '设置 core.hooksPath 失败。' }

    & git config --local commit.template .gitmessage
    if ($LASTEXITCODE -ne 0) { throw '设置 commit.template 失败。' }

    Write-Host '已为当前仓库启用 Git Commit 规范校验与提交模板：'
    Write-Host '  core.hooksPath  = .githooks'
    Write-Host '  commit.template = .gitmessage'
}
finally {
    Pop-Location
}
