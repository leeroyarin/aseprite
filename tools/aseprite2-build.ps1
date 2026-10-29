# Aseprite 2.0 build helper (Windows).
# Usage (from any PowerShell):  powershell -ExecutionPolicy Bypass -File tools\aseprite2-build.ps1 [-Target aseprite]
# Loads the MSVC x64 dev environment (cl.exe, ninja) and builds the existing build/ directory.
param(
  [string]$Target = "aseprite"
)
$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot

if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
  $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
  $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
  if (-not $vs) { throw "Visual Studio with C++ tools not found" }
  Import-Module "$vs\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
  Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments "-arch=x64 -host_arch=x64" | Out-Null
}

Set-Location $repo
cmake --build build --target $Target
exit $LASTEXITCODE
