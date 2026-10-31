# Builds, tests and packs dist\QuasarMonitor-<version>.zip (exe + skins + docs). No installer: unzip and run.
param([string]$Version = '0.1.0')
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
Get-Process QuasarMonitor -ErrorAction SilentlyContinue | Stop-Process -Force
& "$PSScriptRoot\build.ps1"
$stage = "dist\QuasarMonitor"
if (Test-Path dist) { Remove-Item dist -Recurse -Force }
New-Item -ItemType Directory -Force $stage | Out-Null
Copy-Item build\QuasarMonitor.exe $stage
Copy-Item skins "$stage\skins" -Recurse
Copy-Item docs "$stage\docs" -Recurse
Get-ChildItem "$stage\docs\img" -Exclude 'gallery.png' | Remove-Item -Force      # keep the zip small: only the gallery picture
Copy-Item README.md, LICENSE, CHANGELOG.md $stage
$zip = "dist\QuasarMonitor-$Version.zip"
Compress-Archive -Path "$stage\*" -DestinationPath $zip
$h = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLower()
"$h  QuasarMonitor-$Version.zip" | Set-Content "$zip.sha256"
Write-Host ("Built {0}  ({1} KB)`nSHA256 {2}" -f $zip, [int]((Get-Item $zip).Length / 1KB), $h)
