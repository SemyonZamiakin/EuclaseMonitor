# Every metric named in docs/METRICS.md must exist in the live app (via the HTTP export). Keeps the reference honest.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot
$cfgPath = "$env:APPDATA\QuasarMonitor\settings.json"
$backup = if (Test-Path $cfgPath) { Get-Content $cfgPath -Raw } else { $null }
Get-Process QuasarMonitor -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep 1
Set-Content $cfgPath '{ "skin": "minimal", "visible": false, "export": { "enabled": true, "port": 18098, "lan": false } }' -Encoding UTF8
$p = Start-Process "$root\build\QuasarMonitor.exe" -PassThru
$missing = @(); $checked = 0
try {
    Start-Sleep 7   # the process sampler runs every 2 s and the disk sampler every 10 s: wait for both
    $j = (Invoke-WebRequest 'http://127.0.0.1:18098/metrics' -UseBasicParsing).Content | ConvertFrom-Json
    $have = $j.PSObject.Properties.Name
    $hasGpu = $have -contains 'gpu.count'
    $names = Select-String -Path "$root\docs\METRICS.md" -Pattern '`([a-z][a-z0-9_.<>]*)`' -AllMatches | % { $_.Matches } | % { $_.Groups[1].Value } | Sort-Object -Unique
    foreach ($n in $names) {
        if ($n -notmatch '\.') { continue }                                   # format names like `rate` are not metrics
        if ($n -match '\.(exe|json|md|png|jpg)$') { continue }                # file names in the prose
        if ($n -match '^gpu([1-9])\.' -and [int]$j.'gpu.count' -le [int]$Matches[1]) { continue }   # example for a 2nd GPU on a 1-GPU PC
        if ($n -match '^(gpu)' -and -not $hasGpu) { continue }               # no NVIDIA GPU here: GPU names cannot be checked
        $x = $n -replace '<N>', '0' -replace '<K>', '1' -replace '<X>', 'C'
        if ($x -match '^(cpu|gpu)\.(core|clock)?' -or $true) { $checked++ }
        if ($have -notcontains $x) { $missing += $x }
    }
} finally {
    $p | Stop-Process -Force -ErrorAction SilentlyContinue
    if ($null -ne $backup) { Set-Content $cfgPath $backup -Encoding UTF8 } else { Remove-Item $cfgPath -ErrorAction SilentlyContinue }
}
Write-Host "checked $checked documented metric names against the live app (GPU present: $hasGpu)"
if ($missing.Count) { Write-Host "MISSING: $($missing -join ', ')"; exit 1 } else { Write-Host 'metrics doc: all names exist' }
