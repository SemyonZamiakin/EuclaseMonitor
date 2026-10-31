# The global hotkey really toggles the overlay (synthesises Ctrl+Alt+F10; only runs if the app registered it).
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot
$log = "$env:APPDATA\QuasarMonitor\monitor.log"
Add-Type @"
using System; using System.Text; using System.Runtime.InteropServices;
public class HK {
 public delegate bool CB(IntPtr h, IntPtr l);
 [DllImport("user32.dll")] static extern bool EnumWindows(CB cb, IntPtr l);
 [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
 [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetClassNameW(IntPtr h, StringBuilder sb, int n);
 [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
 public static bool OverlayVisible(uint pid) { bool v = false; EnumWindows((h, x) => { uint p; GetWindowThreadProcessId(h, out p); if (p == pid) { var sb = new StringBuilder(64); GetClassNameW(h, sb, 64); if (sb.ToString() == "QuasarMonitor.Overlay") v = IsWindowVisible(h); } return true; }, IntPtr.Zero); return v; }
 public static void Chord() { keybd_event(0x11,0,0,UIntPtr.Zero); keybd_event(0x12,0,0,UIntPtr.Zero); keybd_event(0x79,0,0,UIntPtr.Zero); System.Threading.Thread.Sleep(80); keybd_event(0x79,0,2,UIntPtr.Zero); keybd_event(0x12,0,2,UIntPtr.Zero); keybd_event(0x11,0,2,UIntPtr.Zero); }
}
"@
$cfgPath = "$env:APPDATA\QuasarMonitor\settings.json"
$backup = if (Test-Path $cfgPath) { Get-Content $cfgPath -Raw } else { $null }
Get-Process QuasarMonitor -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep 1
Set-Content $cfgPath '{ "skin": "minimal", "visible": true, "hotkey": "ctrl+alt+f10" }' -Encoding UTF8
$before = (Get-Item $log -ErrorAction SilentlyContinue).Length
$p = Start-Process "$root\build\QuasarMonitor.exe" -PassThru
$fail = 0
try {
    Start-Sleep 4
    $newLog = if (Test-Path $log) { (Get-Content $log -Raw).Substring([int][math]::Min($before, (Get-Content $log -Raw).Length)) } else { '' }
    if ($newLog -match 'hotkey') { Write-Host "SKIP: hotkey could not be registered here ($($newLog.Trim()))"; exit 0 }
    $v0 = [HK]::OverlayVisible([uint32]$p.Id); [HK]::Chord(); Start-Sleep 1; $v1 = [HK]::OverlayVisible([uint32]$p.Id); [HK]::Chord(); Start-Sleep 1; $v2 = [HK]::OverlayVisible([uint32]$p.Id)
    Write-Host "visible: start=$v0  after 1st press=$v1  after 2nd press=$v2"
    if (-not ($v0 -and -not $v1 -and $v2)) { $fail = 1 }
} finally {
    $p | Stop-Process -Force -ErrorAction SilentlyContinue
    if ($null -ne $backup) { Set-Content $cfgPath $backup -Encoding UTF8 } else { Remove-Item $cfgPath -ErrorAction SilentlyContinue }
}
if ($fail) { Write-Host 'hotkey e2e: FAILED'; exit 1 } else { Write-Host 'hotkey e2e: passed' }
