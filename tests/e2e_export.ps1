# End-to-end test of the metrics export with the real exe. Temporarily enables "export" in settings.json and restores it.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot
$exe = "$root\build\EuclaseMonitor.exe"
$cfgPath = "$env:APPDATA\EuclaseMonitor\settings.json"
$backup = if (Test-Path $cfgPath) { Get-Content $cfgPath -Raw } else { $null }
$fail = 0
function Check($name, $ok) { if ($ok) { Write-Host "  ok   $name" } else { Write-Host "  FAIL $name"; $script:fail++ } }

if (Get-Process EuclaseMonitor -ErrorAction SilentlyContinue) { throw "Close Euclase Monitor first: this test starts its own instance and never stops yours" }
Start-Sleep 1
Set-Content $cfgPath '{ "skin": "minimal", "export": { "enabled": true, "port": 18099, "lan": false } }' -Encoding UTF8
$p = Start-Process $exe -PassThru
try {
    Start-Sleep 4
    $h = (Invoke-WebRequest 'http://127.0.0.1:18099/health' -UseBasicParsing).Content
    Check '/health answers ok' ($h -eq 'ok')
    $r = Invoke-WebRequest 'http://127.0.0.1:18099/metrics' -UseBasicParsing
    $j = $r.Content | ConvertFrom-Json
    Check 'content type is JSON' ($r.Headers['Content-Type'] -match 'application/json')
    Check 'cpu.load is a number 0..100' ($j.'cpu.load' -ge 0 -and $j.'cpu.load' -le 100)
    Check 'ram.total_gb present' ($j.'ram.total_gb' -gt 1)
    Check 'string metric cpu.name present' ($j.'cpu.name' -match 'Intel|AMD|CPU')
    Check 'unavailable metric is null (cpu.temp)' ($null -eq $j.'cpu.temp')
    $f = (Invoke-WebRequest 'http://127.0.0.1:18099/metrics?keys=cpu.load,ram.used_pct' -UseBasicParsing).Content | ConvertFrom-Json
    Check 'keys= filter returns only the asked metrics' (($f.PSObject.Properties.Name.Count -eq 2) -and ($null -ne $f.'cpu.load'))
    $code = 0; try { Invoke-WebRequest 'http://127.0.0.1:18099/nope' -UseBasicParsing | Out-Null } catch { $code = [int]$_.Exception.Response.StatusCode }
    Check '404 for unknown path' ($code -eq 404)
    $code = 0; try { Invoke-WebRequest 'http://127.0.0.1:18099/metrics' -Method Post -UseBasicParsing | Out-Null } catch { $code = [int]$_.Exception.Response.StatusCode }
    Check '405 for POST' ($code -eq 405)
    # garbage and slow clients must not wedge the server
    $c = New-Object Net.Sockets.TcpClient('127.0.0.1', 18099); $c.GetStream().Write([byte[]](1..200), 0, 200); $c.Close()
    $slow = New-Object Net.Sockets.TcpClient('127.0.0.1', 18099)   # connects, sends nothing
    $t0 = Get-Date; (Invoke-WebRequest 'http://127.0.0.1:18099/health' -UseBasicParsing -TimeoutSec 8).Content | Out-Null; $dt = ((Get-Date) - $t0).TotalSeconds
    $slow.Close()
    Check "server recovers after garbage / a silent client (${dt}s)" ($dt -lt 6)
    Check 'app still alive' (-not $p.HasExited)
    $socks = @(Get-NetTCPConnection -LocalPort 18099 -ErrorAction SilentlyContinue | ? { $_.OwningProcess -eq $p.Id })
    Check 'listening, and only on 127.0.0.1' (($socks.Count -ge 1) -and -not ($socks | ? { $_.LocalAddress -ne '127.0.0.1' }))
} finally {
    $p | Stop-Process -Force -ErrorAction SilentlyContinue
    if ($null -ne $backup) { Set-Content $cfgPath $backup -Encoding UTF8 } else { Remove-Item $cfgPath -ErrorAction SilentlyContinue }
}
if ($fail) { Write-Host "$fail check(s) FAILED"; exit 1 } else { Write-Host 'export e2e: all passed' }
