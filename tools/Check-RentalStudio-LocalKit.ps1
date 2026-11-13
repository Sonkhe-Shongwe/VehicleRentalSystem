# P2-LK1: read-only local Qt Creator/MinGW source and CMake kit provenance.
# Defaults to inspecting the ignored local build cache. Explicit -BuildAndTest
# runs the EXISTING normal build + native CTest, never the 31-item manual checklist.
# No elevation, network, OS scaling changes, GUI automation, or data import.
param(
    [string]$BuildDirectory = 'build/rental-studio-local',
    [switch]$BuildAndTest
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$allowedRoot = [System.IO.Path]::GetFullPath((Join-Path $repoRoot 'build'))
$buildRoot = if ([System.IO.Path]::IsPathRooted($BuildDirectory)) {
    [System.IO.Path]::GetFullPath($BuildDirectory)
} else {
    [System.IO.Path]::GetFullPath((Join-Path $repoRoot $BuildDirectory))
}
$rootWithSlash = $allowedRoot.TrimEnd([char]'\', [char]'/') + [System.IO.Path]::DirectorySeparatorChar
if (-not $buildRoot.StartsWith($rootWithSlash, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw 'Refusing a build directory outside the repository ignored build/ folder.'
}
$cachePath = Join-Path $buildRoot 'CMakeCache.txt'
if (-not (Test-Path -LiteralPath $cachePath -PathType Leaf)) {
    throw 'No local CMakeCache.txt. First configure the repository-root project with the Qt Creator MinGW kit.'
}

$cache = @{}
foreach ($line in (Get-Content -LiteralPath $cachePath)) {
    if ($line -match '^([^/#:][^:]*):[^=]+=(.*)$') {
        $cache[$matches[1]] = $matches[2]
    }
}
function Need([string]$name) {
    if (-not $cache.ContainsKey($name) -or
        [string]::IsNullOrWhiteSpace($cache[$name])) {
        throw "Required CMakeCache entry missing: $name"
    }
    return [string]$cache[$name]
}
$sourceDir = Need 'CMAKE_HOME_DIRECTORY'
if (-not [System.IO.Path]::GetFullPath($sourceDir).Equals(
        $repoRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw 'CMakeCache points to a different source repository. Use a clean ignored build directory.'
}
foreach ($flag in @('BUILD_QTQUICK_P2_SPIKE','BUILD_TESTING','BUILD_P7_SQLITE_SANDBOX')) {
    $value = Need $flag
    if ($value -notmatch '^(ON|TRUE|YES|1)$') {
        throw "Expected local fictional-only CMake option $flag=ON, found another setting."
    }
}
$generator = Need 'CMAKE_GENERATOR'
$compiler = Need 'CMAKE_CXX_COMPILER'
$qtPackage = Need 'Qt6_DIR'
if ($compiler -notmatch '(?i)(^|[\\/])([^\\/]*-)?g\+\+(\.exe)?$') {
    throw 'Selected C++ compiler is not a MinGW g++ kit. Hosted MSVC does not close the local MinGW gate.'
}
if ($qtPackage -notmatch '(?i)[\\/]mingw_64[\\/]') {
    throw 'Qt6_DIR does not identify a MinGW Qt 6 kit.'
}

$branch = (& git -C $repoRoot branch --show-current | Out-String).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Cannot identify local Git branch.' }
$revision = (& git -C $repoRoot rev-parse HEAD | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or $revision -notmatch '^[0-9a-f]{40}$') {
    throw 'Cannot verify local Git source commit.'
}
$changed = @(& git -C $repoRoot status --porcelain --untracked-files=no)
if ($LASTEXITCODE -ne 0) { throw 'Cannot check tracked source cleanliness.' }
if (@($changed | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }).Count -gt 0) {
    throw 'Tracked source changes exist. Commit or stash them before claiming exact-source evidence.'
}

$qtVersion = 'not inferred'
if ($qtPackage -match '[\\/](\d+\.\d+\.\d+)[\\/]') {
    $qtVersion = $matches[1] + ' (from CMakeCache Qt package path)'
}
Write-Output '[P2-LK1] Windows local Qt source and kit inspection: PASS'
Write-Output "[P2-LK1] Source commit: $revision"
Write-Output ("[P2-LK1] Branch: " + $(if ($branch) { $branch } else { '(detached HEAD)' }))
Write-Output '[P2-LK1] Tracked Git source: clean'
Write-Output "[P2-LK1] Qt package version: $qtVersion"
Write-Output '[P2-LK1] Qt platform: MinGW 64-bit indicated by configured Qt6_DIR'
Write-Output '[P2-LK1] C++ toolchain: g++ (MinGW-configured kit)'
Write-Output "[P2-LK1] CMake generator: $generator"
Write-Output '[P2-LK1] Fictional Qt Quick, testing, opt-in SQLite Lab: ON'
Write-Output '[P2-LK1] No local source paths, usernames or data files are included in this report.'

if (-not $BuildAndTest) {
    Write-Output '[P2-LK1] BUILD/CTEST: NOT RUN. This is configuration inspection, not local acceptance.'
    Write-Output '[P2-LK1] Narrator, physical Windows scaling and motion: NOT TESTED.'
    exit 0
}
# Building all targets, not just launching the IDE, establishes that current
# checked-out source compiled with the configured CMake MinGW kit.
$multiConfig = $cache.ContainsKey('CMAKE_CONFIGURATION_TYPES') -and
               -not [string]::IsNullOrWhiteSpace($cache['CMAKE_CONFIGURATION_TYPES'])
$buildArgs = @('--build', $buildRoot, '--parallel', '2')
$testArgs = @('--test-dir', $buildRoot, '--output-on-failure')
if ($multiConfig) {
    $buildArgs += @('--config','Debug')
    $testArgs += @('-C','Debug')
}
Write-Output '[P2-LK1] Building all current source targets using configured local Qt kit...'
& cmake @buildArgs
if ($LASTEXITCODE -ne 0) { throw 'Local MinGW build FAILED; no acceptance is granted.' }
Write-Output '[P2-LK1] Running existing complete native CTest suite (no manual operations)...'
& ctest @testArgs
if ($LASTEXITCODE -ne 0) { throw 'Local MinGW CTest FAILED; no acceptance is granted.' }
Write-Output '[P2-LK1] LOCAL MIN-GW BUILD + AUTOMATED CTEST: PASS on checked-out source.'
Write-Output '[P2-LK1] Qt Creator GUI version and real Narrator/OS scale still require separate owner observation.'
