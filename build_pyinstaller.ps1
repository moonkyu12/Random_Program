$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$pythonExe = Join-Path $projectRoot "venv\Scripts\python.exe"

if (-not (Test-Path $pythonExe)) {
    throw "venv Python not found: $pythonExe"
}

Set-Location $projectRoot

& $pythonExe -m pip install -r requirements-build.txt
& $pythonExe -m PyInstaller --noconfirm "School Random Program.spec"
& $pythonExe -m PyInstaller --noconfirm "School Random Program Onedir.spec"

Write-Host ""
Write-Host "Onefile: dist\School Random Program.exe"
Write-Host "Onedir:  dist\School Random Program Onedir\School Random Program.exe"
