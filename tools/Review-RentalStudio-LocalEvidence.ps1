# Rental Studio 2.0 P8-D local privacy safeguard for fictional QA diagnostics.
# Default is read-only. -DeleteLogs requires one exact, named report and confirmation.
# Never deletes screenshots, accessibility evidence, unknown files or application data.
[CmdletBinding(SupportsShouldProcess=$true, ConfirmImpact='High', DefaultParameterSetName='Review')]
param(
    [Parameter(Mandatory=$true,ParameterSetName='Delete')][switch]$DeleteLogs,
    [Parameter(Mandatory=$true,ParameterSetName='Delete')]
    [ValidatePattern('^functional-check-evidence-[0-9]+-[0-9]+$')]
    [string]$ReportName,
    [string]$PackageRoot = $PSScriptRoot
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$rootItem = Get-Item -LiteralPath $PackageRoot -Force -ErrorAction Stop
if (-not $rootItem.PSIsContainer -or
    ($rootItem.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
    throw 'Package root must be an ordinary directory, not a link or junction.'
}
$root = $rootItem.FullName
foreach ($required in @('RentalStudioP2Review.exe','SOURCE_COMMIT.txt',
                       'Check-Rental-Studio.cmd','Launch-Rental-Studio.cmd')) {
    $f = Join-Path $root $required
    if (-not (Test-Path -LiteralPath $f -PathType Leaf)) {
        throw ('Not a complete fictional QA kit: missing ' + $required)
    }
    $info = Get-Item -LiteralPath $f -Force
    if ($info.Attributes -band [IO.FileAttributes]::ReparsePoint) {
        throw ('Unexpected link in package: ' + $required)
    }
}
$source = ([IO.File]::ReadAllText(
    (Join-Path $root 'SOURCE_COMMIT.txt'), [Text.Encoding]::UTF8)).Trim()
if ($source -cnotmatch '^[a-fA-F0-9]{40}$') { throw 'Invalid fictional kit source marker.' }

$allowedLogs = @('RESULTS.txt','01-source.log','02-startup.log',
    '03-journey-wide.log','04-journey-compact.log','05-restart.log',
    '06-stale-quote.log','07-cancel.log','08-source-after.log')
$reports = @(Get-ChildItem -LiteralPath $root -Directory -Force -Filter 'functional-check-evidence-*')
if ($DeleteLogs) {
    $reports = @($reports | Where-Object { $_.Name -ceq $ReportName })
    if ($reports.Count -ne 1) {
        throw 'The exact selected fictional functional report was not found.'
    }
}
if ($reports.Count -eq 0) {
    Write-Host 'No matching fictional functional check reports to review.'
    return
}
foreach ($directory in $reports) {
    if ($directory.Name -cnotmatch '^functional-check-evidence-[0-9]+-[0-9]+$' -or
        ($directory.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw 'Refusing unknown or linked evidence folder.'
    }
    $entries = @(Get-ChildItem -LiteralPath $directory.FullName -Force)
    foreach ($entry in $entries) {
        if ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) {
            throw 'Evidence folder contains a symlink or junction. No cleanup permitted.'
        }
    }
    $result = Join-Path $directory.FullName 'RESULTS.txt'
    if (-not (Test-Path -LiteralPath $result -PathType Leaf)) {
        if ($DeleteLogs) { throw 'Unidentified or incomplete functional report: RESULTS.txt missing.' }
        Write-Host ('REVIEW ONLY (unidentified): ' + $directory.Name)
        continue
    }
    $summary = [IO.File]::ReadAllText($result)
    if (-not $summary.StartsWith('Rental Studio 2.0 - fictional functional verification') -or
        -not $summary.Contains('Source commit: ' + $source)) {
        if ($DeleteLogs) { throw 'Report is not identified as belonging to the exact fictional kit.' }
        Write-Host ('REVIEW ONLY (unverified marker): ' + $directory.Name)
        continue
    }
    $logs = @($entries | Where-Object {
        -not $_.PSIsContainer -and $allowedLogs -ccontains $_.Name
    })
    if ($logs.Count -eq 0) {
        Write-Host ('No known diagnostic logs in ' + $directory.Name)
        continue
    }
    $kept = @($entries | Where-Object {
        $_.PSIsContainer -or $allowedLogs -cnotcontains $_.Name
    })
    Write-Host ('REPORT: {0}; known logs: {1}; other files/folders preserved: {2}' -f
        $directory.Name, $logs.Count, $kept.Count)
    if (-not $DeleteLogs) {
        Write-Host 'READ-ONLY: no files deleted. Use -DeleteLogs -ReportName <exact folder> to request deletion.'
        continue
    }
    if (-not $PSCmdlet.ShouldProcess($directory.FullName,
           ('Delete {0} recognized fictional QA diagnostic logs ONLY' -f $logs.Count))) {
        continue
    }
    foreach ($entry in $logs) {
        $now = Get-Item -LiteralPath $entry.FullName -Force
        if ($now.PSIsContainer -or ($now.Attributes -band [IO.FileAttributes]::ReparsePoint) -or
            $allowedLogs -cnotcontains $now.Name) {
            throw 'Report changed or contains an unsafe entry. Cleanup stopped.'
        }
        Remove-Item -LiteralPath $now.FullName -Force -ErrorAction Stop
    }
    # Only remove this EXACT empty generated directory. Never recurse.
    $remaining = @(Get-ChildItem -LiteralPath $directory.FullName -Force)
    if ($remaining.Count -eq 0) {
        $again = Get-Item -LiteralPath $directory.FullName -Force
        if ($again.Attributes -band [IO.FileAttributes]::ReparsePoint) {
            throw 'Report directory became a junction. Refusing removal.'
        }
        Remove-Item -LiteralPath $directory.FullName -Force
        Write-Host ('PASS: deleted only recognized logs and empty report folder ' + $directory.Name)
    } else {
        Write-Host ('PASS: deleted recognized logs; preserved {0} additional entries in {1}' -f
            $remaining.Count, $directory.Name)
    }
}
Write-Host 'P8-D: acceptance-evidence, screenshots, persistent fictional sessions and unknown files are never removed.'
