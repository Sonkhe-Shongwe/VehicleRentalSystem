# P8-B: synthetic-only, PowerShell 5.1-compatible package SHA-256 manifest regression.
# These are tiny invented files, not the rental executable or customer data.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$scriptFile = Join-Path $PSScriptRoot '..\tools\Verify-RentalStudio-PackageHash.ps1'
$tempRoot = Join-Path ([IO.Path]::GetTempPath()) ('rental-studio-package-hash-' + [guid]::NewGuid().ToString('N'))
$kit = Join-Path $tempRoot 'fictional-qa-kit'
$allPaths = @(
    'RentalStudioP2Review.exe',
    'SOURCE_COMMIT.txt',
    'Launch-Rental-Studio.cmd',
    'Check-Rental-Studio.cmd',
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
function Invoke-Check {
    & $scriptFile -Check -PackageRoot $kit | Out-Null
}
function Expect-Denial([string]$scenario) {
    $rejected = $false
    try { Invoke-Check } catch { $rejected = $true }
    if (-not $rejected) { throw ("Verification incorrectly passed: " + $scenario) }
    Write-Host ("PASS: rejects " + $scenario)
}
New-Item -ItemType Directory -Path $kit -Force | Out-Null
try {
    foreach ($name in $allPaths) {
        $file = Join-Path $kit $name
        $folder = Split-Path -Parent $file
        New-Item -ItemType Directory -Path $folder -Force | Out-Null
        $data = if ($name -eq 'SOURCE_COMMIT.txt') { 'a' * 40 } else { 'FICTIONAL QA FILE: ' + $name }
        [IO.File]::WriteAllText($file, $data, [Text.UTF8Encoding]::new($false))
    }
    Copy-Item -LiteralPath $scriptFile -Destination (Join-Path $kit 'Verify-RentalStudio-PackageHash.ps1')
    & $scriptFile -Create -PackageRoot $kit | Out-Null
    Invoke-Check
    Write-Host 'PASS: generated manifest verifies untouched package'

    $vehicle = Join-Path $kit 'demo\vehicles.txt'
    $beforeVehicle = [IO.File]::ReadAllBytes($vehicle)
    [IO.File]::AppendAllText($vehicle, 'TAMPERED')
    Expect-Denial 'modified packaged file'
    [IO.File]::WriteAllBytes($vehicle, $beforeVehicle)
    Invoke-Check

    $removedFile = Join-Path $kit 'PHOTO_CREDITS\ORIGINAL_CC0_IMAGE_CREDITS.md'
    $outside = Join-Path $tempRoot 'removed-credit.md'
    Move-Item -LiteralPath $removedFile -Destination $outside
    Expect-Denial 'missing packaged file'
    Move-Item -LiteralPath $outside -Destination $removedFile
    Invoke-Check

    $unexpected = Join-Path $kit 'unknown.payload'
    [IO.File]::WriteAllText($unexpected, 'unexpected', [Text.Encoding]::ASCII)
    Expect-Denial 'unexpected packaged file'
    Remove-Item -LiteralPath $unexpected -Force
    Invoke-Check

    $marker = Join-Path $kit 'SOURCE_COMMIT.txt'
    [IO.File]::WriteAllText($marker, ('b' * 40), [Text.Encoding]::ASCII)
    Expect-Denial 'changed source SHA marker'
    [IO.File]::WriteAllText($marker, ('a' * 40), [Text.Encoding]::ASCII)
    Invoke-Check

    $manifest = Join-Path $kit 'PACKAGE_SHA256.json'
    $manifestBytes = [IO.File]::ReadAllBytes($manifest)
    [IO.File]::WriteAllText($manifest, '{"schema":"forged","files":[]}')
    Expect-Denial 'invalid or forged manifest'
    [IO.File]::WriteAllBytes($manifest, $manifestBytes)
    Invoke-Check
    Write-Host 'P8-B PACKAGE HASH TESTS PASS: original, five negative cases and restored checks'
}
finally {
    if (Test-Path -LiteralPath $tempRoot) {
        Remove-Item -LiteralPath $tempRoot -Force -Recurse
    }
}
