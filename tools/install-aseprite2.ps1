# tools/install-aseprite2.ps1
# Installs Aseprite 2.0 to %LOCALAPPDATA%\Programs\Aseprite 2.0
$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$binDir = Join-Path $repoRoot "build\bin"
$installDir = Join-Path $env:LOCALAPPDATA "Programs\Aseprite 2.0"

Write-Host "==> Verifying build outputs..." -ForegroundColor Cyan
$sourceExe = Join-Path $binDir "aseprite.exe"
if (-not (Test-Path $sourceExe)) {
    throw "aseprite.exe not found in $binDir. Run tools\aseprite2-build.ps1 first."
}

Write-Host "==> Creating install directory: $installDir" -ForegroundColor Cyan
if (-not (Test-Path $installDir)) {
    New-Item -ItemType Directory -Path $installDir -Force | Out-Null
}

Write-Host "==> Copying binaries..." -ForegroundColor Cyan
$targetExe = Join-Path $installDir "aseprite.exe"
$oldExe = Join-Path $installDir "aseprite.old.exe"
if (Test-Path $oldExe) {
    Remove-Item $oldExe -Force -ErrorAction SilentlyContinue
}
try {
    Copy-Item $sourceExe -Destination $targetExe -Force -ErrorAction Stop
} catch {
    Write-Host "    (Active Aseprite 2.0 session detected -- performing live hot-swap...)" -ForegroundColor Yellow
    if (Test-Path $targetExe) {
        Move-Item $targetExe $oldExe -Force
    }
    Copy-Item $sourceExe -Destination $targetExe -Force
}

foreach ($dep in @("icudtl.dat", "zlib.dll")) {
    $srcDep = Join-Path $binDir $dep
    if (Test-Path $srcDep) {
        try {
            Copy-Item $srcDep -Destination $installDir -Force -ErrorAction Stop
        } catch {
            # Already loaded in memory by active instance; safe to keep existing copy
        }
    }
}

Write-Host "==> Copying application assets (data/)..." -ForegroundColor Cyan
$destData = Join-Path $installDir "data"
& robocopy (Join-Path $binDir "data") $destData /E /NP /NFL /NDL /NJH /NJS | Out-Null
if ($LASTEXITCODE -gt 7) {
    throw "Robocopy failed with code $LASTEXITCODE"
}

Write-Host "==> Setting up MCP launcher and scripts..." -ForegroundColor Cyan
Copy-Item (Join-Path $repoRoot "start-aseprite2.cmd") -Destination $installDir -Force
$destTools = Join-Path $installDir "tools"
if (-not (Test-Path $destTools)) {
    New-Item -ItemType Directory -Path $destTools -Force | Out-Null
}
Copy-Item (Join-Path $repoRoot "tools\start-aseprite2.ps1") -Destination $destTools -Force

# Create Shortcuts
$wsh = New-Object -ComObject WScript.Shell

# Desktop shortcut
$desktopDir = [Environment]::GetFolderPath('Desktop')
if (Test-Path $desktopDir) {
    $desktopShortcut = $wsh.CreateShortcut((Join-Path $desktopDir "Aseprite 2.0.lnk"))
    $desktopShortcut.TargetPath = (Join-Path $installDir "aseprite.exe")
    $desktopShortcut.WorkingDirectory = $installDir
    $desktopShortcut.Description = "Aseprite 2.0 - Animated sprite editor & pixel art tool"
    $desktopShortcut.IconLocation = "$installDir\aseprite.exe,0"
    $desktopShortcut.Save()
    Write-Host "==> Desktop shortcut created: $desktopDir\Aseprite 2.0.lnk" -ForegroundColor Green
}

# Start Menu shortcut
$startMenuDir = Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs"
if (Test-Path $startMenuDir) {
    # Remove old vague shortcut if present
    $oldShortcut = Join-Path $startMenuDir "aseprite (2).lnk"
    if (Test-Path $oldShortcut) {
        Remove-Item $oldShortcut -Force -ErrorAction SilentlyContinue
    }

    $startShortcut = $wsh.CreateShortcut((Join-Path $startMenuDir "Aseprite 2.0.lnk"))
    $startShortcut.TargetPath = (Join-Path $installDir "aseprite.exe")
    $startShortcut.WorkingDirectory = $installDir
    $startShortcut.Description = "Aseprite 2.0 - Animated sprite editor & pixel art tool"
    $startShortcut.IconLocation = "$installDir\aseprite.exe,0"
    $startShortcut.Save()
    Write-Host "==> Start Menu shortcut created: $startMenuDir\Aseprite 2.0.lnk" -ForegroundColor Green
}

# Add to user PATH
$userPath = [Environment]::GetEnvironmentVariable("Path", "User")
$paths = if ($userPath) { $userPath -split ';' } else { @() }
if ($paths -notcontains $installDir) {
    $newUserPath = if ([string]::IsNullOrWhiteSpace($userPath)) { $installDir } else { "$userPath;$installDir" }
    [Environment]::SetEnvironmentVariable("Path", $newUserPath, "User")
    Write-Host "==> Added $installDir to user PATH" -ForegroundColor Green
} else {
    Write-Host "==> $installDir already in user PATH" -ForegroundColor DarkCyan
}

# Set file associations (.aseprite and .ase)
$regBase = "HKCU:\Software\Classes"
$progId = "Aseprite2.File"

$progKey = "$regBase\$progId"
if (-not (Test-Path $progKey)) { New-Item -Path $progKey -Force | Out-Null }
Set-ItemProperty -Path $progKey -Name "(Default)" -Value "Aseprite 2.0 Sprite"

$openCmdKey = "$progKey\shell\open\command"
if (-not (Test-Path $openCmdKey)) { New-Item -Path $openCmdKey -Force | Out-Null }
Set-ItemProperty -Path $openCmdKey -Name "(Default)" -Value "`"$installDir\aseprite.exe`" `"%1`""

$iconKey = "$progKey\DefaultIcon"
if (-not (Test-Path $iconKey)) { New-Item -Path $iconKey -Force | Out-Null }
Set-ItemProperty -Path $iconKey -Name "(Default)" -Value "`"$installDir\aseprite.exe`",0"

foreach ($ext in @(".aseprite", ".ase")) {
    $extKey = "$regBase\$ext"
    if (-not (Test-Path $extKey)) { New-Item -Path $extKey -Force | Out-Null }
    Set-ItemProperty -Path $extKey -Name "(Default)" -Value $progId
}

$legacyAutoKey = "$regBase\aseprite_auto_file\shell\open\command"
if (Test-Path $legacyAutoKey) {
    Set-ItemProperty -Path $legacyAutoKey -Name "(Default)" -Value "`"$installDir\aseprite.exe`" `"%1`""
}

Write-Host "==> File associations updated for .aseprite and .ase" -ForegroundColor Green
Write-Host "==> Aseprite 2.0 installation successfully completed!" -ForegroundColor Green
