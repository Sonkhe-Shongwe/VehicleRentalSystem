# Host-independent synthetic CMakeCache fixture regression for P2-LK1.
# Runs on Windows PowerShell 5.1; deliberately DOES NOT compile or test a Qt app.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$script = Join-Path $repoRoot 'tools/Check-RentalStudio-LocalKit.ps1'
$relative = 'build/p2-lk1-synthetic-fixture'
$fixture = Join-Path $repoRoot $relative
if (Test-Path -LiteralPath $fixture) {
    throw 'Test fixture directory already exists. Refusing to replace local files.'
}
New-Item -Path $fixture -ItemType Directory -Force | Out-Null
function Write-Cache([string]$compiler,[string]$qt,[string]$testFlag,[string]$sourceRootValue) {
    @(
        "CMAKE_HOME_DIRECTORY:INTERNAL=$sourceRootValue",
        'CMAKE_GENERATOR:INTERNAL=Ninja',
        "CMAKE_CXX_COMPILER:FILEPATH=$compiler",
        "Qt6_DIR:PATH=$qt",
        'BUILD_QTQUICK_P2_SPIKE:BOOL=ON',
        'BUILD_P7_SQLITE_SANDBOX:BOOL=ON',
        "BUILD_TESTING:BOOL=$testFlag"
    ) | Set-Content -LiteralPath (Join-Path $fixture 'CMakeCache.txt') -Encoding ASCII
}
function Probe([bool]$shouldPass,[string]$scenario) {
    # PowerShell 5.1 promotes expected child-stderr rejections to
    # NativeCommandError when ErrorActionPreference=Stop. Capture the
    # exit code instead, without treating an intentionally invalid
    # cache as a harness failure.
    $previousPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $output = & powershell.exe -NoProfile -NonInteractive -File $script -BuildDirectory $relative 2>$null | Out-String
        $exit = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previousPreference
    }
    if ($shouldPass -and ($exit -ne 0 -or $output -notmatch 'BUILD/CTEST: NOT RUN')) {
        throw "Expected source/kit inspection pass for $scenario (code $exit)."
    }
    if (-not $shouldPass -and $exit -eq 0) {
        throw "Expected fail-closed configuration rejection for $scenario."
    }
    if ($shouldPass -and ($output.Contains($repoRoot) -or $output.Contains('C:/Qt/Tools/'))) {
        throw 'Configuration report leaked an absolute source/toolchain path.'
    }
    Write-Output "[P2-LK1 synthetic] $scenario : PASS"
}
try {
    Write-Cache 'C:/Qt/Tools/mingw1310_64/bin/g++.exe' 'C:/Qt/6.10.2/mingw_64/lib/cmake/Qt6' 'ON' $repoRoot
    Probe $true 'MinGW fictional-only cache and clean source'
    Write-Cache 'C:/Build/VisualStudio/cl.exe' 'C:/Qt/6.10.2/mingw_64/lib/cmake/Qt6' 'ON' $repoRoot
    Probe $false 'reject MSVC compiler in MinGW evidence'
    Write-Cache 'C:/Qt/Tools/mingw1310_64/bin/g++.exe' 'C:/Qt/6.10.2/msvc2022_64/lib/cmake/Qt6' 'ON' $repoRoot
    Probe $false 'reject Windows MSVC Qt package'
    Write-Cache 'C:/Qt/Tools/mingw1310_64/bin/g++.exe' 'C:/Qt/6.10.2/mingw_64/lib/cmake/Qt6' 'OFF' $repoRoot
    Probe $false 'reject disabled CTest flag'
    Write-Cache 'C:/Qt/Tools/mingw1310_64/bin/g++.exe' 'C:/Qt/6.10.2/mingw_64/lib/cmake/Qt6' 'ON' 'C:/different/source'
    Probe $false 'reject stale or foreign CMakeCache'
    Write-Output '[P2-LK1 synthetic] five source/kit parser scenarios PASS; no Qt build was claimed.'
} finally {
    Remove-Item -LiteralPath $fixture -Recurse -Force -ErrorAction SilentlyContinue
}
