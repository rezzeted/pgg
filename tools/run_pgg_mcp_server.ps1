#!/usr/bin/env pwsh
# Optional terminal wrapper for the PGG MCP server (the Cursor entry is
# python3 -m tools.pgg_mcp.launch). Finds Python, then execs launch.

$ErrorActionPreference = 'Stop'

chcp 65001 > $null
[Console]::OutputEncoding = [Console]::InputEncoding = [System.Text.UTF8Encoding]::new($false)
$OutputEncoding = [System.Text.UTF8Encoding]::new($false)

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
Set-Location $RepoRoot

$env:PYTHONIOENCODING = 'utf-8'
$env:PYTHONUTF8 = '1'
$env:PYTHONUNBUFFERED = '1'
$env:PGG_REPO_ROOT = $RepoRoot

function Find-Python {
    $candidates = @(
        { (Get-Command python -ErrorAction SilentlyContinue).Source },
        { (Get-Command python3 -ErrorAction SilentlyContinue).Source },
        { & py -3 -c 'import sys; print(sys.executable)' 2>$null }
    )
    foreach ($candidate in $candidates) {
        try {
            $path = & $candidate
            if ($path -and (Test-Path -LiteralPath $path)) {
                return $path
            }
        } catch {
            continue
        }
    }
    throw 'Python not found. Install Python 3.10+ and ensure `python` or `py` is on PATH.'
}

$python = Find-Python
& $python -m tools.pgg_mcp.launch
exit $LASTEXITCODE
