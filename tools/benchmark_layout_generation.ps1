# Manual performance smoke for Grid2D layout generation (iteration 7 optional).
# Runs gtest filter for EdgarIntegration.DungeonGenerator_* a few times and prints elapsed ms.
# Does not enforce a threshold in CI (machine-dependent); use for local regression after optimizations.
#
# Usage:
#   powershell -File tools/benchmark_layout_generation.ps1 -BuildDir _build -Config Debug
#   powershell -File tools/benchmark_layout_generation.ps1 -EdgarTestsExe D:\path\to\edgar_tests.exe

param(
    [string] $BuildDir = "_build",
    [string] $Config = "Debug",
    [string] $EdgarTestsExe = ""
)

$ErrorActionPreference = "Stop"

if ($EdgarTestsExe -eq "") {
    $candidate = Join-Path $BuildDir "bin\$Config\edgar_tests.exe"
    if (-not (Test-Path $candidate)) {
        $candidate = Join-Path $BuildDir "bin\edgar_tests.exe"
    }
    if (-not (Test-Path $candidate)) {
        Write-Error "edgar_tests.exe not found. Build the project or pass -EdgarTestsExe."
    }
    $EdgarTestsExe = (Resolve-Path $candidate).Path
}

$filter = "EdgarIntegration.DungeonGenerator_*"
$runs = 5
Write-Host "Exe: $EdgarTestsExe"
Write-Host "Filter: $filter  ($runs runs)"

$times = @()
for ($i = 0; $i -lt $runs; $i++) {
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    & $EdgarTestsExe "--gtest_filter=$filter" 2>$null | Out-Null
    $sw.Stop()
    $times += $sw.ElapsedMilliseconds
    Write-Host "  run $($i+1): $($sw.ElapsedMilliseconds) ms"
}

$sorted = $times | Sort-Object
$median = $sorted[[int]($runs / 2)]
Write-Host "Median: $median ms (not a CI gate; compare manually across commits)"
