# ==============================================================================
# Easymetric 6-Surface Live Studio - Adversarial Stress Suite Runner
# ==============================================================================
param(
  [string]$ExePath = "$PSScriptRoot\..\build\bin\aseprite.exe",
  [string]$TestScript = "$PSScriptRoot\easymetric_adversarial_stress.lua"
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path "$PSScriptRoot\..").Path
$resolvedExe = (Resolve-Path $ExePath -ErrorAction SilentlyContinue)
$resolvedScript = (Resolve-Path $TestScript -ErrorAction SilentlyContinue)

if (-not $resolvedExe -or -not (Test-Path $resolvedExe)) {
  Write-Error "Aseprite executable not found at: $ExePath. Run build first."
  exit 1
}

if (-not $resolvedScript -or -not (Test-Path $resolvedScript)) {
  Write-Error "Adversarial test script not found at: $TestScript."
  exit 1
}

Write-Host "======================================================" -ForegroundColor Cyan
Write-Host " Easymetric Adversarial Stress Test Suite Runner     " -ForegroundColor Cyan
Write-Host "======================================================" -ForegroundColor Cyan
Write-Host "Repo Root:     $repoRoot"
Write-Host "Executable:    $resolvedExe"
Write-Host "Test Script:   $resolvedScript"
Write-Host ""
Write-Host "Launching adversarial stress verification..." -ForegroundColor Yellow

$sw = [System.Diagnostics.Stopwatch]::StartNew()
$output = & $resolvedExe -batch -script $resolvedScript *>&1 | Out-String
$exitCode = $LASTEXITCODE
$sw.Stop()

Write-Host $output

Write-Host ("Test execution finished in {0:N2} seconds with exit code: {1}" -f $sw.Elapsed.TotalSeconds, $exitCode)

if ($exitCode -eq 0) {
  Write-Host "SUCCESS: All Adversarial Stress tests passed." -ForegroundColor Green
  exit 0
} else {
  Write-Host "FAILURE: Adversarial Stress test suite reported errors." -ForegroundColor Red
  exit $exitCode
}
