# Builds Euclase Monitor with MSYS2 mingw64 g++ (no other dependencies), runs the tests.
#   ./build.ps1            -> build\EuclaseMonitor.exe, build\render_skin.exe + run tests
#   ./build.ps1 -NoTests
param([switch]$NoTests)
$ErrorActionPreference = 'Stop'
$gpp = 'C:\msys64\mingw64\bin\g++.exe'
if (-not (Test-Path $gpp)) { $gpp = (Get-Command g++ -ErrorAction Stop).Source }
$env:PATH = (Split-Path $gpp) + ';' + $env:PATH
Set-Location $PSScriptRoot
New-Item -ItemType Directory -Force build | Out-Null

$core  = 'src\json.cpp', 'src\expr.cpp', 'src\util.cpp', 'src\samplers.cpp', 'src\skin.cpp', 'src\render.cpp', 'src\ring_export.cpp', 'build\corundum.o'
$libs  = '-lgdiplus', '-lole32', '-lshlwapi', '-lshell32', '-liphlpapi', '-lws2_32', '-lpowrprof', '-lpsapi', '-luuid', '-ladvapi32'
$warn  = '-Wall', '-Wextra'

function Build($name, $args2) {
    Write-Host "building $name ..."
    & $gpp @args2
    if ($LASTEXITCODE) { throw "build of $name failed" }
}

# Corundum (vendored, C11) is compiled as C, then linked like the C++ sources
$gcc = Join-Path (Split-Path $gpp) 'gcc.exe'
Write-Host 'building corundum.o ...'
& $gcc -std=c11 -O2 -Wall -Wextra -c third_party\corundum\corundum.c -o build\corundum.o
if ($LASTEXITCODE) { throw 'build of corundum.o failed' }

Build 'EuclaseMonitor.exe' (@('-std=c++17', '-Os') + $warn + @('-mwindows', '-municode', '-static', '-Wl,--gc-sections', '-s', 'src\main.cpp') + $core + @('-o', 'build\EuclaseMonitor.exe') + $libs)
Build 'bench_tick.exe'    (@('-std=c++17', '-O2') + $warn + @('tools\bench_tick.cpp') + $core + @('-o', 'build\bench_tick.exe') + $libs)
Build 'render_skin.exe'   (@('-std=c++17', '-O2') + $warn + @('tools\render_skin.cpp') + $core + @('-o', 'build\render_skin.exe') + $libs)

if (-not $NoTests) {
    Build 'test_core.exe'     (@('-std=c++17', '-O2') + $warn + @('tests\test_core.cpp', 'src\json.cpp', 'src\expr.cpp', '-o', 'build\test_core.exe'))
    Build 'test_ring.exe'     (@('-std=c++17', '-O2') + $warn + @('tests\test_ring.cpp', 'src\ring_export.cpp', 'src\util.cpp', 'build\corundum.o', '-o', 'build\test_ring.exe') + $libs)
    Build 'test_samplers.exe' (@('-std=c++17', '-O2') + $warn + @('tests\test_samplers.cpp', 'src\samplers.cpp', 'src\util.cpp', '-o', 'build\test_samplers.exe') + $libs)
    Build 'test_skin.exe'     (@('-std=c++17', '-O2') + $warn + @('tests\test_skin.cpp') + $core + @('-o', 'build\test_skin.exe') + $libs)
    Build 'test_docs.exe'     (@('-std=c++17', '-O2') + $warn + @('tests\test_docs.cpp') + $core + @('-o', 'build\test_docs.exe') + $libs)
    foreach ($t in 'test_core', 'test_samplers', 'test_skin', 'test_ring') {
        Write-Host "--- $t"
        & ".\build\$t.exe"
        if ($LASTEXITCODE) { throw "$t failed" }
    }
    foreach ($doc in 'docs\CUSTOMIZE.md', 'docs\CUSTOMIZE.ru.md') {
        Write-Host "--- test_docs ($doc)"
        & .\build\test_docs.exe $doc
        if ($LASTEXITCODE) { throw "test_docs failed for $doc" }
    }
}
Write-Host "OK: build\EuclaseMonitor.exe  ($([int]((Get-Item build\EuclaseMonitor.exe).Length/1KB)) KB)"
