# P8-M: external, read-only Windows PowerShell 5.1 preflight for fictional QA.
# Run on an owner-controlled Windows PC with a freshly extracted, full kit.
# Does not launch the app, install VC, access network, write files or registry,
# assert clean Windows target support, or certify a signed installer.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$PackageRoot,
    [Parameter(Mandatory = $true)][string]$ExpectedSourceCommit,
    [Parameter(Mandatory = $true)][string]$MinimumVCRuntimeVersion
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Convert-V14Version([string]$Value) {
    if ($Value -cnotmatch '^14\.[0-9]+\.[0-9]+\.[0-9]+$') {
        throw 'P8-M: invalid exact four-field v14 runtime version'
    }
    try { return [System.Version]::Parse($Value) }
    catch { throw 'P8-M: invalid v14 numeric version' }
}
if ($ExpectedSourceCommit -cnotmatch '^[0-9a-f]{40}$') {
    throw 'P8-M: expected source must be a lowercase full 40-hex Git commit'
}
$minimum = Convert-V14Version $MinimumVCRuntimeVersion
if (-not [Environment]::Is64BitOperatingSystem) {
    throw 'P8-M: this QA preview requires a 64-bit Windows host'
}
$package = Get-Item -LiteralPath $PackageRoot -Force -ErrorAction Stop
if (-not $package.PSIsContainer -or
    ($package.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
    throw 'P8-M: package root must be a real directory, not a link or junction'
}
$root = $package.FullName.TrimEnd([char[]]@('\', '/'))
$marker = Join-Path $root 'SOURCE_COMMIT.txt'
if (-not (Test-Path -LiteralPath $marker -PathType Leaf)) {
    throw 'P8-M: original source marker missing'
}
$source = ([IO.File]::ReadAllText($marker, [Text.Encoding]::UTF8)).Trim()
if ($source -cne $ExpectedSourceCommit) {
    throw 'P8-M: exact-source marker mismatch. Never substitute a different run.'
}
$manifestFile = Join-Path $root 'PACKAGE_SHA256.json'
if (-not (Test-Path -LiteralPath $manifestFile -PathType Leaf)) {
    throw 'P8-M: original P8-B package manifest missing'
}
$manifest = Get-Content -LiteralPath $manifestFile -Raw -Encoding UTF8 |
    ConvertFrom-Json -ErrorAction Stop
if ($null -eq $manifest -or
    $manifest.schema -cne 'rental-studio.qa.package.sha256.v1' -or
    $manifest.sourceCommit -cne $ExpectedSourceCommit -or
    $manifest.files -isnot [array] -or $manifest.files.Count -eq 0) {
    throw 'P8-M: manifest schema, source or inventory missing'
}
$all = @(Get-ChildItem -LiteralPath $root -Force -Recurse -ErrorAction Stop)
$entries = New-Object 'System.Collections.Generic.List[object]'
foreach ($item in $all) {
    if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
        throw 'P8-M: package contains a link or junction'
    }
    if ($item.PSIsContainer) { continue }
    if ($item.FullName -ceq $manifestFile) { continue }
    $relative = $item.FullName.Substring($root.Length + 1).Replace('\', '/')
    if ([string]::IsNullOrWhiteSpace($relative) -or
        $relative.Contains([char]10) -or $relative.Contains([char]13)) {
        throw 'P8-M: invalid relative file path'
    }
    $entries.Add([pscustomobject]@{ RelativePath = $relative; FullName = $item.FullName })
}
$ordered = @($entries.ToArray() | Sort-Object -Property RelativePath)
if ($ordered.Count -ne $manifest.files.Count) {
    throw 'P8-M: package file count does not match original manifest'
}
$required = @(
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
$names = @($ordered | ForEach-Object { $_.RelativePath })
foreach ($need in $required) {
    if ($names -cnotcontains $need) {
        throw ('P8-M: required QA file missing: ' + $need)
    }
}
for ($i = 0; $i -lt $ordered.Count; ++$i) {
    $record = $manifest.files[$i]
    $actual = $ordered[$i]
    if ($record.path -cne $actual.RelativePath -or
        $record.sha256 -cnotmatch '^[0-9a-f]{64}$') {
        throw 'P8-M: manifest path/hash shape or sequence mismatch'
    }
    $digest = (Get-FileHash -LiteralPath $actual.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($digest -cne $record.sha256) {
        throw 'P8-M: package checksum mismatch; do not execute the kit'
    }
}
$view = [Microsoft.Win32.RegistryView]::Registry64
$base = [Microsoft.Win32.RegistryKey]::OpenBaseKey(
    [Microsoft.Win32.RegistryHive]::LocalMachine, $view)
try {
    $key = $base.OpenSubKey('SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64', $false)
    if ($null -eq $key) {
        throw 'P8-M: official x64 v14 VC runtime registration was not established'
    }
    try {
        $installed = $key.GetValue('Installed', $null)
        $runtimeText = $key.GetValue('Version', $null)
        if ($installed -isnot [int] -or $installed -ne 1 -or
            $runtimeText -isnot [string]) {
            throw 'P8-M: x64 VC runtime installed/version registry state is incomplete'
        }
        $runtimeText = $runtimeText.TrimStart([char[]]@('v', 'V'))
        $runtime = Convert-V14Version $runtimeText
        if ($runtime -lt $minimum) {
            throw 'P8-M: installed VC runtime older than supplied source build floor'
        }
    } finally {
        $key.Dispose()
    }
} finally {
    $base.Dispose()
}
Write-Output ('P8-M PREFLIGHT PASS - package hashes: {0}; exact source: {1}' -f
    $ordered.Count, $ExpectedSourceCommit)
Write-Output ('P8-M HOST VC X64 OBSERVED - runtime: {0}; supplied build floor: {1}' -f
    $runtime.ToString(), $minimum.ToString())
Write-Output 'P8-M NO LAUNCH OR NETWORK TEST PERFORMED. Not proof of clean Windows, offline behaviour, licensing, publisher identity, installer readiness or owner acceptance.'
