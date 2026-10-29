# Builds KeepWeapons.asi (32-bit Windows DLL) on Windows with a user-local Zig compiler.
#
#   .\build.ps1                       build using .\tools\zig-windows-x86_64-0.13.0\zig.exe (or $env:ZIG, or zig on PATH)
#   .\build.ps1 -FetchZig             download zig 0.13.0 from ziglang.org into .\downloads\zig, verify its
#                                     sha256, extract it into .\tools, then build
#   .\build.ps1 -Test                 also build and run the unit test of the pattern scanner
#   .\build.ps1 -Install "<GTAIV dir>" copy KeepWeapons.asi/.ini into <GTAIV dir>\plugins after building
param(
    [switch]$FetchZig,
    [switch]$Test,
    [string]$Install
)
$ErrorActionPreference = 'Stop'

$Dir = $PSScriptRoot
$ZigVer = '0.13.0'
$ZigZip = "zig-windows-x86_64-$ZigVer.zip"
$ZigUrl = "https://ziglang.org/download/$ZigVer/$ZigZip"
$ZigSha256 = 'd859994725ef9402381e557c60bb57497215682e355204d754ee3df75ee3c158'
$ZigHome = Join-Path $Dir "tools\zig-windows-x86_64-$ZigVer"

if ($FetchZig -and -not (Test-Path "$ZigHome\zig.exe")) {
    $dl = Join-Path $Dir 'downloads\zig'
    New-Item -ItemType Directory -Force $dl | Out-Null
    $zip = Join-Path $dl $ZigZip
    if (-not (Test-Path $zip)) { Invoke-WebRequest -UseBasicParsing -Uri $ZigUrl -OutFile $zip }
    $hash = (Get-FileHash -Algorithm SHA256 $zip).Hash.ToLower()
    if ($hash -ne $ZigSha256) { throw "sha256 mismatch for ${zip}: $hash" }
    # Expand-Archive trips over zig's long lib paths (MAX_PATH); the bundled bsdtar does not.
    New-Item -ItemType Directory -Force (Join-Path $Dir 'tools') | Out-Null
    tar.exe -xf $zip -C (Join-Path $Dir 'tools')
    if ($LASTEXITCODE) { throw 'zig extraction failed' }
}

if ($env:ZIG) { $Zig = $env:ZIG }
elseif (Test-Path "$ZigHome\zig.exe") { $Zig = "$ZigHome\zig.exe" }
elseif ((Get-Command zig -ErrorAction SilentlyContinue) -and ((& zig version) -eq $ZigVer)) { $Zig = (Get-Command zig).Source }
else { throw "zig $ZigVer not found. Run: .\build.ps1 -FetchZig" }
Write-Host "using zig: $Zig ($(& $Zig version))"

$CFlags = @('-target', 'x86-windows-gnu', '-std=gnu11', '-O2', '-Wall', '-Wextra', '-fno-strict-aliasing')

if ($Test) {
    # The scanner test is plain C, so build it for the host with zig cc instead of a system cc.
    & $Zig cc -std=gnu11 -O1 -Wall -Wextra -I "$Dir\src" -o "$Dir\tests\test_patscan.exe" "$Dir\tests\test_patscan.c"
    if ($LASTEXITCODE) { throw 'test build failed' }
    & "$Dir\tests\test_patscan.exe"
    if ($LASTEXITCODE) { throw 'tests failed' }
}

& $Zig cc @CFlags -shared -I "$Dir\src" -o "$Dir\KeepWeapons.asi" "$Dir\src\KeepWeapons.c" -lkernel32 -luser32
if ($LASTEXITCODE) { throw 'build failed' }

# zig also writes an import library and a PDB next to the output; the plugin does not need them.
Remove-Item -Force -ErrorAction SilentlyContinue "$Dir\KeepWeapons.lib", "$Dir\KeepWeapons.pdb"
Get-Item "$Dir\KeepWeapons.asi" | Format-Table Name, Length

if ($Install) {
    $plugins = Join-Path $Install 'plugins'
    New-Item -ItemType Directory -Force $plugins | Out-Null
    Copy-Item "$Dir\KeepWeapons.asi" $plugins -Force
    if (-not (Test-Path "$plugins\KeepWeapons.ini")) { Copy-Item "$Dir\KeepWeapons.ini" $plugins }
    Write-Host "installed to $plugins"
}
