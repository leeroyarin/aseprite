# Start Aseprite 2.0 with MCP Bridge
$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot

# Check if MCP bridge already listening on port 8765
$port = 8765
$tcpConn = Get-NetTCPConnection -LocalPort $port -State Listen -ErrorAction SilentlyContinue

if (-not $tcpConn) {
    Write-Host "[Aseprite 2.0] Starting MCP server background process..." -ForegroundColor Cyan
    Start-Process node -ArgumentList "$repoRoot\tools\aseprite2-mcp\dist\index.js" -WorkingDirectory $repoRoot -WindowStyle Hidden
    Start-Sleep -Milliseconds 600
} else {
    Write-Host "[Aseprite 2.0] MCP server is already listening on port $port." -ForegroundColor DarkCyan
}

$exePath = "$repoRoot\build\bin\aseprite.exe"
if (-not (Test-Path $exePath)) {
    Write-Host "[Aseprite 2.0] Error: $exePath not found. Build first." -ForegroundColor Red
    exit 1
}

Write-Host "[Aseprite 2.0] Launching Aseprite 2.0..." -ForegroundColor Green
Start-Process $exePath $args
