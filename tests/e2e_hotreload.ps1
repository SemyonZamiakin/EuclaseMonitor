# End-to-end test with the real exe: hot reload, rejected broken skin keeps the old one, recovery, no leaks.
# Edits a COPY of a skin inside %APPDATA%\QuasarMonitor\skins\e2e-test and removes it afterwards.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot
$exe = "$root\build\QuasarMonitor.exe"
$log = "$env:APPDATA\QuasarMonitor\monitor.log"
$dir = "$env:APPDATA\QuasarMonitor\skins\e2e-test"
$fail = 0
function Check($name, $ok) { if ($ok) { Write-Host "  ok   $name" } else { Write-Host "  FAIL $name"; $script:fail++ } }
function LogSince($mark) { if (Test-Path $log) { (Get-Content $log -Raw) } else { '' } }

Get-Process QuasarMonitor -ErrorAction SilentlyContinue | Stop-Process -Force
Remove-Item $dir -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $dir | Out-Null
Copy-Item "$root\skins\minimal\skin.json" "$dir\skin.json"
$orig = Get-Content "$dir\skin.json" -Raw
$startLen = if (Test-Path $log) { (Get-Item $log).Length } else { 0 }

$p = Start-Process $exe -ArgumentList '--skin', 'e2e-test' -PassThru
Start-Sleep 4
function NewLog { $t = Get-Content $log -Raw; if ($t.Length -gt $startLen) { $t.Substring([int][math]::Min($startLen, $t.Length)) } else { '' } }

Check 'app is running' (-not $p.HasExited)
Check 'skin loaded at start' ((NewLog) -match "skin 'e2e-test' loaded: 1 widgets")

# 1. valid edit -> reloaded by the watcher within a second or two
$mark = (NewLog).Length
Set-Content "$dir\skin.json" ($orig -replace 'CPU \{cpu.load:0\}%', 'PROCESSOR {cpu.load:0}%') -Encoding UTF8
Start-Sleep 2
Check 'valid edit is reloaded' ((NewLog).Substring($mark) -match "skin 'e2e-test' loaded")

# 2. broken JSON -> rejected with a readable error, app keeps running with the previous skin
$mark = (NewLog).Length
Set-Content "$dir\skin.json" '{ "widgets": [ { "type": "text", "text": "x" ' -Encoding UTF8
Start-Sleep 2
$added = (NewLog).Substring($mark)
Check 'broken JSON is rejected' ($added -match "rejected: skin.json: line")
Check 'app survives a broken skin' (-not $p.HasExited)

# 3. a skin with a bad expression loads with warnings instead of failing
$mark = (NewLog).Length
Set-Content "$dir\skin.json" '{ "widgets": [ { "type": "bar", "w": 50, "h": 8, "value": "cpu.load +" }, { "type": "text", "text": "still here" } ] }' -Encoding UTF8
Start-Sleep 2
$added = (NewLog).Substring($mark)
Check 'bad expression -> warning, other widgets kept' (($added -match 'bad "value" expression') -and ($added -match 'loaded: 1 widgets, 1 warnings'))

# 4. recovery
$mark = (NewLog).Length
Set-Content "$dir\skin.json" $orig -Encoding UTF8
Start-Sleep 2
Check 'recovers after the fix' ((NewLog).Substring($mark) -match 'loaded: 1 widgets, 0 warnings')

# 5. rapid saves (editors write twice) do not pile up reloads
$mark = (NewLog).Length
1..5 | % { Set-Content "$dir\skin.json" $orig -Encoding UTF8; Start-Sleep -Milliseconds 40 }
Start-Sleep 2
$loads = ([regex]::Matches((NewLog).Substring($mark), "loaded:")).Count
Check "debounce: $loads reload(s) for 5 quick saves" ($loads -ge 1 -and $loads -le 2)

$p.Refresh()
Check "memory stays small after all that ($([math]::Round($p.WorkingSet64/1MB,1)) MB)" ($p.WorkingSet64 -lt 40MB)
$p | Stop-Process -Force
Remove-Item $dir -Recurse -Force
if ($fail) { Write-Host "$fail check(s) FAILED"; exit 1 } else { Write-Host 'e2e: all passed' }
