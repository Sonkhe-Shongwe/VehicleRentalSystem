# P8-M Windows PowerShell 5.1 synthetic source/manifest/VC floor refusal tests.
# Creates and destroys a fake package beneath the OS temp directory only.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$script = Join-Path $PSScriptRoot '..\tools\Inspect-RentalStudio-CleanWindowsPrerequisites.ps1'
$root = Join-Path ([IO.Path]::GetTempPath()) ('rental-studio-p8m-' + [guid]::NewGuid().ToString('N'))
$source = ('a' * 40)
$floor = '14.44.35229.0'
$paths = @(
    'RentalStudioP2Review.exe',
    'SOURCE_COMMIT.txt',
    'Launch-Rental-Studio.cmd',
    'Check-Rental-Studio.cmd',
    'Verify-RentalStudio-PackageHash.ps1',
    'START_HERE_FUNCTIONAL_DEMO.md',
    'demo/vehicles.txt',
    'demo/rentals.txt',
    'Qt6Core.dll',
    'platforms/qwindows.dll',
    'sqldrivers/qsqlite.dll',
    'THIRD_PARTY_NOTICES/README.md'
)
function Write-FakePackage {
    foreach ($name in $paths) {
        $path = Join-Path $root ($name.Replace('/', '\'))
        [IO.Directory]::CreateDirectory((Split-Path -Parent $path)) | Out-Null
        $payload = if ($name -eq 'SOURCE_COMMIT.txt') { $source } else { 'ONLY_FAKE_BYTES:' + $name }
        [IO.File]::WriteAllText($path, $payload, [Text.UTF8Encoding]::new($false))
    }
    $inventory = @(
        foreach ($name in ($paths | Sort-Object)) {
            $path = Join-Path $root ($name.Replace('/', '\'))
            [ordered]@{
                path = $name
                sha256 = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
            }
        }
    )
    $doc = [ordered]@{
        schema = 'rental-studio.qa.package.sha256.v1'
        sourceCommit = $source
        files = @($inventory)
    }
    $json = ConvertTo-Json -InputObject $doc -Depth 8
    [IO.File]::WriteAllText((Join-Path $root 'PACKAGE_SHA256.json'), $json,
        [Text.UTF8Encoding]::new($false))
}
function Invoke-Audit([string]$Expected = $source, [string]$Min = $floor) {
    # PS5.1 native stderr on deliberately rejected cases must not become a
    # terminating PS7 NativeCommandError in the *test harness* itself.
    $oldPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $output = & powershell.exe -NoProfile -NonInteractive -File $script -PackageRoot $root -ExpectedSourceCommit $Expected -MinimumVCRuntimeVersion $Min 2>&1 | Out-String
        $exitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $oldPreference
    }
    return @{ ExitCode = $exitCode; Text = $output }
}
function Must-Refuse([string]$label, [string]$expected, [string]$min) {
    $result = Invoke-Audit $expected $min
    if ($result.ExitCode -eq 0 -or $result.Text -match 'P8-M PREFLIGHT PASS') {
        throw ('P8-M incorrectly accepted ' + $label)
    }
    Write-Host ('PASS: refuse ' + $label)
}
try {
    [IO.Directory]::CreateDirectory($root) | Out-Null
    Write-FakePackage
    $before = (Get-FileHash -LiteralPath (Join-Path $root 'PACKAGE_SHA256.json') -Algorithm SHA256).Hash
    $passed = Invoke-Audit
    if ($passed.ExitCode -ne 0 -or $passed.Text -notmatch 'P8-M PREFLIGHT PASS') {
        throw ('P8-M synthetic package / real runner VC registry test failed: ' + $passed.Text)
    }
    if ((Get-FileHash -LiteralPath (Join-Path $root 'PACKAGE_SHA256.json') -Algorithm SHA256).Hash -cne $before) {
        throw 'P8-M mutated synthetic package manifest'
    }
    Write-Host 'PASS: valid synthetic package, observed hosted VC floor, no file mutation'
    Must-Refuse 'source commitment mismatch' ('b' * 40) $floor
    Must-Refuse 'invalid expected Git SHA' 'not-hex' $floor
    Must-Refuse 'unknown build floor too high' $source '14.999.999.999'
    Must-Refuse 'non-v14 floor' $source '15.0.0.0'
    $demo = Join-Path $root 'demo\vehicles.txt'
    [IO.File]::AppendAllText($demo, 'tampered')
    Must-Refuse 'tampered fictional demo fixture' $source $floor
    Write-FakePackage
    $unknown = Join-Path $root 'extra.txt'
    [IO.File]::WriteAllText($unknown, 'unregistered')
    Must-Refuse 'unexpected extra file' $source $floor
    Remove-Item -LiteralPath $unknown -Force
    Remove-Item -LiteralPath (Join-Path $root 'sqldrivers\qsqlite.dll') -Force
    Must-Refuse 'missing protected Qt SQLite driver' $source $floor
    Write-FakePackage
    $manifest = Join-Path $root 'PACKAGE_SHA256.json'
    $bad = [IO.File]::ReadAllText($manifest).Replace('rental-studio.qa.package.sha256.v1', 'fake.manifest')
    [IO.File]::WriteAllText($manifest, $bad)
    Must-Refuse 'modified manifest schema' $source $floor
    Write-Host 'P8-M CLEAN WINDOWS PREFLIGHT SYNTHETIC GUARDS PASS'
} finally {
    if (Test-Path -LiteralPath $root) {
        Remove-Item -LiteralPath $root -Recurse -Force
    }
}
