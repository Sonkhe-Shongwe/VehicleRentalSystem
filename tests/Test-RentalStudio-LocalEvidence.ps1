# P8-D: safe log-only cleanup, synthetic Windows PowerShell 5.1 regression.
# No application launch or real user records. All paths are unique disposable test files.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$script = Join-Path $PSScriptRoot '..\tools\Review-RentalStudio-LocalEvidence.ps1'
$tmp = Join-Path ([IO.Path]::GetTempPath()) ('rental-studio-p8d-' + [guid]::NewGuid().ToString('N'))
$kit = Join-Path $tmp 'fake-qa-kit'
New-Item -ItemType Directory -Path $kit -Force | Out-Null
$source = 'a' * 40
$names = @('RentalStudioP2Review.exe','SOURCE_COMMIT.txt',
    'Check-Rental-Studio.cmd','Launch-Rental-Studio.cmd')
function Create-Report([string]$leaf) {
    $dir = Join-Path $kit $leaf
    New-Item -ItemType Directory -Path $dir -Force | Out-Null
    [IO.File]::WriteAllText((Join-Path $dir 'RESULTS.txt'),
        ('Rental Studio 2.0 - fictional functional verification' + [Environment]::NewLine +
         'Source commit: ' + $source + [Environment]::NewLine + 'FINAL: PASS - all eight checks exited 0'))
    foreach ($name in @('01-source.log','02-startup.log','03-journey-wide.log',
            '04-journey-compact.log','05-restart.log','06-stale-quote.log',
            '07-cancel.log','08-source-after.log')) {
        [IO.File]::WriteAllText((Join-Path $dir $name),'fictional diagnostic with local path')
    }
    return $dir
}
function Expect-Failure([string]$label, [scriptblock]$action) {
    $denied = $false
    try { & $action } catch { $denied = $true }
    if (-not $denied) { throw ('Unexpected cleanup allowed: ' + $label) }
    Write-Host ('PASS: P8-D refuses ' + $label)
}
try {
    foreach ($name in $names) {
        [IO.File]::WriteAllText((Join-Path $kit $name),
            $(if ($name -eq 'SOURCE_COMMIT.txt') { $source } else { 'FICTIONAL QA FILE' }))
    }
    $first = Create-Report 'functional-check-evidence-123-456'
    & $script -PackageRoot $kit | Out-Null
    if (-not (Test-Path -LiteralPath (Join-Path $first '01-source.log'))) {
        throw 'Default read-only review deleted a log.'
    }
    Write-Host 'PASS: default review preserves all logs'

    # Fake locally captured screenshot must survive explicit log cleanup.
    $image = Join-Path $first 'owner-screenshot.png'
    [IO.File]::WriteAllBytes($image,[byte[]](137,80,78,71))
    & $script -PackageRoot $kit -DeleteLogs -ReportName 'functional-check-evidence-123-456' -Confirm:$false | Out-Null
    if (-not (Test-Path -LiteralPath $image) -or
        (Test-Path -LiteralPath (Join-Path $first 'RESULTS.txt'))) {
        throw 'Log-only deletion altered screenshot or retained diagnostic logs.'
    }
    Write-Host 'PASS: cleans known logs, preserves screenshots and folder'

    $second = Create-Report 'functional-check-evidence-789-012'
    & $script -PackageRoot $kit -DeleteLogs -ReportName 'functional-check-evidence-789-012' -Confirm:$false | Out-Null
    if (Test-Path -LiteralPath $second) { throw 'Empty generated report folder was not removed.' }
    Write-Host 'PASS: removes precisely selected empty generated report'

    $third = Create-Report 'functional-check-evidence-333-444'
    $orig = Join-Path $third 'RESULTS.txt'
    [IO.File]::WriteAllText($orig,'Human note; untrusted evidence marker')
    Expect-Failure 'unidentified report marker' {
        & $script -PackageRoot $kit -DeleteLogs -ReportName 'functional-check-evidence-333-444' -Confirm:$false | Out-Null
    }
    if (-not (Test-Path -LiteralPath (Join-Path $third '01-source.log'))) {
        throw 'Rejected unknown marker incorrectly deleted logs.'
    }
    Expect-Failure 'folder path injection' {
        & $script -PackageRoot $kit -DeleteLogs -ReportName '../acceptance-evidence' -Confirm:$false | Out-Null
    }
    $protected = Join-Path $kit 'acceptance-evidence'
    New-Item -ItemType Directory -Path $protected | Out-Null
    [IO.File]::WriteAllText((Join-Path $protected 'owner-review.txt'),'KEEP OWNER REVIEW')
    Expect-Failure 'repeat cleanup after recognized report logs were removed' {
        & $script -PackageRoot $kit -DeleteLogs -ReportName 'functional-check-evidence-123-456' -Confirm:$false | Out-Null
    }
    & $script -PackageRoot $kit | Out-Null
    if (-not (Test-Path -LiteralPath (Join-Path $protected 'owner-review.txt'))) {
        throw 'Acceptance evidence must never be deleted.'
    }
    Write-Host 'PASS: owner acceptance evidence and other files never touched'
    Write-Host 'P8-D LOG-ONLY PRIVACY TESTS PASS'
}
finally {
    if (Test-Path -LiteralPath $tmp) { Remove-Item -LiteralPath $tmp -Force -Recurse }
}
