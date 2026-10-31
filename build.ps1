# Builds Quasar Monitor with MSYS2 mingw64 g++ (no other dependencies), runs the tests.
#   ./build.ps1            -> build\QuasarMonitor.exe, build\render_skin.exe + run tests
#   ./build.ps1 -NoTests
param([switch]$NoTests)
$ErrorActionPreference = 'Stop'
$gpp = 'C:\msys64\mingw64\bin\g++.exe'
if (-not (Test-Path $gpp)) { $gpp = (Get-Command g++ -ErrorAction Stop).Source }
$env:PATH = (Split-Path $gpp) + ';' + $env:PATH
Set-Location $PSScriptRoot
New-Item -ItemType Directory -Force build | Out-Null

$core  = 'src\json.cpp', 'src\expr.cpp', 'src\util.cpp', 'src\samplers.cpp', 'src\skin.cpp', 'src\render.cpp'
$libs  = '-lgdiplus', '-lole32', '-lshlwapi', '-lshell32', '-liphlpapi', '-lws2_32', '-lpowrprof', '-lpsapi', '-luuid', '-ladvapi32'
$warn  = '-Wall', '-Wextra'

function Build($name, $args2) {
    Write-Host "building $name ..."
    & $gpp @args2
    if ($LASTEXITCODE) { throw "build of $name failed" }
}

Build 'QuasarMonitor.exe' (@('-std=c++17', '-Os') + $warn + @('-mwindows', '-municode', '-static', '-Wl,--gc-sections', '-s', 'src\main.cpp') + $core + @('-o', 'build\QuasarMonitor.exe') + $libs)
Build 'render_skin.exe'   (@('-std=c++17', '-O2') + $warn + @('tools\render_skin.cpp') + $core + @('-o', 'build\render_skin.exe') + $libs)

if (-not $NoTests) {
    Build 'test_core.exe'     (@('-std=c++17', '-O2') + $warn + @('tests\test_core.cpp', 'src\json.cpp', 'src\expr.cpp', '-o', 'build\test_core.exe'))
    Build 'test_samplers.exe' (@('-std=c++17', '-O2') + $warn + @('tests\test_samplers.cpp', 'src\samplers.cpp', 'src\util.cpp', '-o', 'build\test_samplers.exe') + $libs)
    Build 'test_skin.exe'     (@('-std=c++17', '-O2') + $warn + @('tests\test_skin.cpp') + $core + @('-o', 'build\test_skin.exe') + $libs)
    Build 'test_docs.exe'     (@('-std=c++17', '-O2') + $warn + @('tests\test_docs.cpp') + $core + @('-o', 'build\test_docs.exe') + $libs)
    foreach ($t in 'test_core', 'test_samplers', 'test_skin') {
        Write-Host "--- $t"
        & ".\build\$t.exe"
        if ($LASTEXITCODE) { throw "$t failed" }
    }
    Write-Host "--- test_docs (examples in docs/CUSTOMIZE.md)"
    & .\build\test_docs.exe docs\CUSTOMIZE.md
    if ($LASTEXITCODE) { throw 'test_docs failed' }
}
Write-Host "OK: build\QuasarMonitor.exe  ($([int]((Get-Item build\QuasarMonitor.exe).Length/1KB)) KB)"
