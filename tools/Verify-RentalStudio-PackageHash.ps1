# Rental Studio 2.0: SHA-256 inventory for the unsigned fictional Windows QA kit.
# Windows PowerShell 5.1 compatible. Never reads customer data or network endpoints.
[CmdletBinding(DefaultParameterSetName = 'Check')]
param(
    [Parameter(Mandatory = $true, ParameterSetName = 'Create')]
    [switch]$Create,
    [Parameter(Mandatory = $true, ParameterSetName = 'Check')]
    [switch]$Check,
    [string]$PackageRoot = $PSScriptRoot
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$schema = 'rental-studio.qa.package.sha256.v1'
$manifestName = 'PACKAGE_SHA256.json'
$requiredFiles = @(
    'RentalStudioP2Review.exe',
    'SOURCE_COMMIT.txt',
    'Launch-Rental-Studio.cmd',
    'Check-Rental-Studio.cmd',
    'Verify-RentalStudio-PackageHash.ps1',
    'Review-RentalStudio-LocalEvidence.ps1',
    'START_HERE_FUNCTIONAL_DEMO.md',
    'Verify-P2-Windows.ps1',
    'Verify-P4-Windows.ps1',
    'demo/vehicles.txt',
    'demo/rentals.txt',
    'PHOTO_CREDITS/CATALOGUE_IMAGE_CREDITS.md',
    'PHOTO_CREDITS/ORIGINAL_CC0_IMAGE_CREDITS.md',
    'THIRD_PARTY_NOTICES/README.md',
    'THIRD_PARTY_NOTICES/DECLARED_ASSET_ATTRIBUTIONS.json',
    'THIRD_PARTY_NOTICES/LUCIDE_LICENSE.txt',
    'THIRD_PARTY_NOTICES/LUCIDE_SOURCE_README.md',
    'THIRD_PARTY_NOTICES/INTER_OFL.txt',
    'THIRD_PARTY_NOTICES/INTER_SOURCE.md'
)

$directory = Get-Item -LiteralPath $PackageRoot -Force
if (-not $directory.PSIsContainer -or
    ($directory.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
    throw 'Package root must be an ordinary directory, not a junction or link.'
}
$root = $directory.FullName.TrimEnd([char[]]@('\', '/'))
$manifestPath = Join-Path $root $manifestName

function Get-PackageEntries {
    $all = @(Get-ChildItem -LiteralPath $root -Force -Recurse -ErrorAction Stop)
    $records = New-Object 'System.Collections.Generic.List[object]'
    foreach ($entry in $all) {
        if ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) {
            throw 'Package may not contain links or junctions.'
        }
        if ($entry.PSIsContainer) { continue }
        if ($entry.FullName -ceq $manifestPath) { continue }
        $relative = $entry.FullName.Substring($root.Length + 1).Replace('\', '/')
        if ([string]::IsNullOrWhiteSpace($relative) -or
            $relative.Contains([char]10) -or $relative.Contains([char]13)) {
            throw 'Invalid package relative file name.'
        }
        $records.Add([pscustomobject]@{
            RelativePath = $relative
            FullName = $entry.FullName
        })
    }
    $ordered = @($records.ToArray() | Sort-Object -Property RelativePath)
    $names = @($ordered | ForEach-Object { $_.RelativePath })
    foreach ($need in $requiredFiles) {
        if ($names -cnotcontains $need) {
            throw ("Required fictional QA package file missing: " + $need)
        }
    }
    # Emit individual file records; the caller collects them with @(...).
    # A unary comma here incorrectly wrapped the list as one item.
    return $ordered
}

$sourceFile = Join-Path $root 'SOURCE_COMMIT.txt'
if (-not (Test-Path -LiteralPath $sourceFile -PathType Leaf)) {
    throw 'Missing SOURCE_COMMIT.txt'
}
$source = ([IO.File]::ReadAllText($sourceFile, [Text.Encoding]::UTF8)).Trim()
if ($source -cnotmatch '^[0-9a-fA-F]{40}$') {
    throw 'SOURCE_COMMIT.txt must contain a complete 40-character Git SHA.'
}
$source = $source.ToLowerInvariant()

$files = @(Get-PackageEntries)
$inventory = New-Object 'System.Collections.Generic.List[object]'
foreach ($file in $files) {
    $digest = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    $inventory.Add([ordered]@{ path = $file.RelativePath; sha256 = $digest })
}
if ($Create) {
    if (Test-Path -LiteralPath $manifestPath) {
        throw 'Manifest already exists. Refusing to overwrite an existing package inventory.'
    }
    $document = [ordered]@{
        schema = $schema
        sourceCommit = $source
        files = @($inventory.ToArray())
    }
    $json = ConvertTo-Json -InputObject $document -Depth 6
    [IO.File]::WriteAllText($manifestPath,
        $json + [Environment]::NewLine, [Text.UTF8Encoding]::new($false))
    Write-Host ("PASS: created SHA-256 manifest for {0} files; source {1}" -f $inventory.Count, $source)
    return
}

if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
    throw 'PACKAGE_SHA256.json missing.'
}
$manifest = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 |
    ConvertFrom-Json -ErrorAction Stop
if ($null -eq $manifest -or $manifest.schema -cne $schema -or
    $manifest.sourceCommit -cne $source -or $manifest.files -isnot [array]) {
    throw 'Unsupported manifest schema or source SHA mismatch.'
}
if ($manifest.files.Count -ne $inventory.Count) {
    throw ("Package file count changed: manifest {0}, actual {1}" -f
        $manifest.files.Count, $inventory.Count)
}
for ($i = 0; $i -lt $inventory.Count; ++$i) {
    $expectedPath = [string]$manifest.files[$i].path
    $expectedHash = [string]$manifest.files[$i].sha256
    $actual = $inventory[$i]
    if ($expectedPath -cne $actual.path -or
        $expectedHash -cnotmatch '^[0-9a-f]{64}$' -or
        $expectedHash -cne $actual.sha256) {
        throw ("Package file name or SHA-256 mismatch at inventory entry {0}: {1}" -f
            ($i + 1), $actual.path)
    }
}
Write-Host ("PASS: verified {0} packaged files and source {1}" -f $inventory.Count, $source)
