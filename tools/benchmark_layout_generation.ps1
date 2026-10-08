# Manual performance smoke for Grid2D layout generation (iteration 7 optional).
# Runs gtest filter for DungeonTopologyGeneratorIntegration.DungeonGenerator_* a few times and prints elapsed ms.
# Does not enforce a threshold in CI (machine-dependent); use for local regression after optimizations.
#
# Usage:
#   powershell -File tools/benchmark_layout_generation.ps1 -BuildDir _build -Config Debug
#   powershell -File tools/benchmark_layout_generation.ps1 -DungeonTopologyGeneratorTestsExe D:\path\to\dungeon_topology_generator_tests.exe

param(
    [string] $BuildDir = "_build",
    [string] $Config = "Debug",
    [string] $DungeonTopologyGeneratorTestsExe = ""
)

$ErrorActionPreference = "Stop"

if ($DungeonTopologyGeneratorTestsExe -eq "") {
    $candidate = Join-Path $BuildDir "bin\$Config\dungeon_topology_generator_tests.exe"
    if (-not (Test-Path $candidate)) {
        $candidate = Join-Path $BuildDir "bin\dungeon_topology_generator_tests.exe"
    }
    if (-not (Test-Path $candidate)) {
        Write-Error "dungeon_topology_generator_tests.exe not found. Build the project or pass -DungeonTopologyGeneratorTestsExe."
    }
    $DungeonTopologyGeneratorTestsExe = (Resolve-Path $candidate).Path
}

$filter = "DungeonTopologyGeneratorIntegration.DungeonGenerator_*"
$runs = 5
Write-Host "Exe: $DungeonTopologyGeneratorTestsExe"
Write-Host "Filter: $filter  ($runs runs)"

$times = @()
for ($i = 0; $i -lt $runs; $i++) {
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    & $DungeonTopologyGeneratorTestsExe "--gtest_filter=$filter" 2>$null | Out-Null
    $sw.Stop()
    $times += $sw.ElapsedMilliseconds
    Write-Host "  run $($i+1): $($sw.ElapsedMilliseconds) ms"
}

$sorted = $times | Sort-Object
$median = $sorted[[int]($runs / 2)]
Write-Host "Median: $median ms (not a CI gate; compare manually across commits)"
