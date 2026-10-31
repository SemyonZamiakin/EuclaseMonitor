# Starts the app, lets it run, and reports real resource use (so the "5 MB" claim is a measurement, not a promise).
param([string]$Exe = "$PSScriptRoot\..\build\QuasarMonitor.exe", [int]$Seconds = 40, [string]$Skin = 'glass')
Get-Process QuasarMonitor -ErrorAction SilentlyContinue | Stop-Process -Force
$p = Start-Process $Exe -ArgumentList '--skin', $Skin -PassThru
Start-Sleep 8
$p.Refresh()
$cpu0 = $p.TotalProcessorTime.TotalSeconds; $t0 = Get-Date
Start-Sleep $Seconds
$p.Refresh()
$cpuUsed = $p.TotalProcessorTime.TotalSeconds - $cpu0
$wall = ((Get-Date) - $t0).TotalSeconds
$cores = (Get-CimInstance Win32_ComputerSystem).NumberOfLogicalProcessors
$counter = Get-CimInstance Win32_PerfFormattedData_PerfProc_Process -Filter "IDProcess=$($p.Id)"
[pscustomobject]@{
    Skin                  = $Skin
    WorkingSetMB          = [math]::Round($p.WorkingSet64 / 1MB, 1)
    PrivateWorkingSetMB   = [math]::Round($counter.WorkingSetPrivate / 1MB, 1)
    PrivateBytesMB        = [math]::Round($p.PrivateMemorySize64 / 1MB, 1)
    CpuPercentOfOneCore   = [math]::Round(100 * $cpuUsed / $wall, 2)
    CpuPercentOfAllCores  = [math]::Round(100 * $cpuUsed / $wall / $cores, 3)
    Threads               = $p.Threads.Count
    Handles               = $p.HandleCount
} | Format-List
