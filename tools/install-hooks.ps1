#requires -Version 5.1
<#
.SYNOPSIS
    Install this repository's git hooks (blocks accidental direct pushes to main).

.DESCRIPTION
    Sets core.hooksPath = .githooks for THIS clone only.
    Run it once after cloning. Teammates must run it too (hooks are local config,
    they are not carried by the clone).

    Undo:  git config --unset core.hooksPath

    NOTE: this file is ASCII-only on purpose (Windows PowerShell 5.1 reads
    BOM-less .ps1 as ANSI; non-ASCII would break parsing).
#>
$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $repo

$hook = Join-Path $repo '.githooks\pre-push'
if (-not (Test-Path -LiteralPath $hook)) {
    throw ".githooks/pre-push not found in $repo"
}

git config core.hooksPath .githooks
$value = git config --get core.hooksPath

Write-Host "repo         : $repo"
Write-Host "core.hooksPath = $value"
Write-Host ''
Write-Host 'Done. Direct pushes to main are now blocked in this clone.' -ForegroundColor Green
Write-Host 'Test it:  git commit --allow-empty -m "test" ; git push   (should fail)' -ForegroundColor DarkGray
