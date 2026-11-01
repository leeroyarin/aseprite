# ==============================================================================
# Smart Object Feature - E2E Test Suite Runner
# Aseprite 2.0 Automation Harness
# ==============================================================================

[CmdletBinding()]
param(
  [string]$AsepriteBin = "build\bin\aseprite.exe",
  [string]$TestScript = "tests\smart_object\test_smart_object_runner.lua"
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$asepritePath = Join-Path $repoRoot $AsepriteBin
$scriptPath = Join-Path $repoRoot $TestScript

Write-Host "======================================================" -ForegroundColor Cyan
Write-Host " Smart Object Feature E2E Test Suite Runner          " -ForegroundColor Cyan
Write-Host "======================================================" -ForegroundColor Cyan
Write-Host "Repo Root:     $repoRoot"
Write-Host "Executable:    $asepritePath"
Write-Host "Test Script:   $scriptPath"

if (-not (Test-Path $asepritePath)) {
  Write-Error "Aseprite executable not found at: $asepritePath. Please build target first via 'cmake --build build'."
  exit 1
}

if (-not (Test-Path $scriptPath)) {
  Write-Error "Test script not found at: $scriptPath."
  exit 1
}

$sw = [System.Diagnostics.Stopwatch]::StartNew()

Write-Host "`nLaunching headless Smart Object E2E verification..." -ForegroundColor Yellow
$procInfo = New-Object System.Diagnostics.ProcessStartInfo
$procInfo.FileName = $asepritePath
$procInfo.Arguments = "-batch -script `"$scriptPath`""
$procInfo.RedirectStandardOutput = $true
$procInfo.RedirectStandardError = $true
$procInfo.UseShellExecute = $false
$procInfo.WorkingDirectory = $repoRoot

$process = [System.Diagnostics.Process]::Start($procInfo)
$stdout = $process.StandardOutput.ReadToEnd()
$stderr = $process.StandardError.ReadToEnd()
$process.WaitForExit()

$sw.Stop()

if ($stdout) {
  Write-Host $stdout
}

if ($stderr) {
  Write-Host $stderr -ForegroundColor Red
}

$exitCode = $process.ExitCode
Write-Host "`nTest execution finished in $($sw.Elapsed.TotalSeconds.ToString('F2')) seconds with exit code: $exitCode"

if ($exitCode -eq 0) {
  Write-Host "SUCCESS: All Smart Object E2E tests passed." -ForegroundColor Green
  exit 0
} else {
  Write-Host "FAILURE: Smart Object E2E tests failed with exit code $exitCode." -ForegroundColor Red
  exit $exitCode
}
