# P8-C: source-bound ZIP metadata regression, Windows PowerShell 5.1.
# All values synthetic, no ZIP, app executable, customer records or network.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$helper = Join-Path $PSScriptRoot '..\tools\Write-RentalStudio-PackageProvenance.ps1'
$root = Join-Path ([IO.Path]::GetTempPath()) ('rental-studio-p8c-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $root -Force | Out-Null
$summary = Join-Path $root 'GITHUB_STEP_SUMMARY.md'
$valid = @{
    WorkflowRunId = '456789'
    SourceCommit = ('a' * 40)
    ArtifactId = '123456'
    ArtifactUrl = 'https://github.com/Sonkhe-Shongwe/vehicle-rental-system/actions/runs/456789/artifacts/123456'
    ArtifactDigest = ('b' * 64)
    SummaryPath = $summary
}
function Invoke-Case([hashtable]$changes) {
    $argsCopy = @{}
    foreach ($name in $valid.Keys) { $argsCopy[$name] = $valid[$name] }
    foreach ($name in $changes.Keys) { $argsCopy[$name] = $changes[$name] }
    & $helper @argsCopy | Out-Null
}
function Expect-Denial([string]$scenario, [hashtable]$changes) {
    $before = [IO.File]::ReadAllText($summary)
    $denied = $false
    try { Invoke-Case $changes } catch { $denied = $true }
    if (-not $denied) { throw ('P8-C unexpectedly accepted ' + $scenario) }
    if ([IO.File]::ReadAllText($summary) -cne $before) {
        throw ('Rejected metadata modified the summary: ' + $scenario)
    }
    Write-Host ('PASS: reject ' + $scenario)
}
try {
    Invoke-Case @{}
    $body = [IO.File]::ReadAllText($summary)
    if (-not $body.Contains('b' * 64) -or
        -not $body.Contains('a' * 40) -or
        -not $body.Contains($valid.ArtifactUrl) -or
        -not $body.Contains('Get-FileHash') -or
        -not $body.Contains('not an independently signed attestation')) {
        throw 'P8-C valid GitHub Summary lacks essential provenance or limitation.'
    }
    Write-Host 'PASS: valid uploaded ZIP metadata publishes outside package'
    Expect-Denial 'short digest' @{ ArtifactDigest = 'abc' }
    Expect-Denial 'nonhex digest' @{ ArtifactDigest = ('x' * 64) }
    Expect-Denial 'forged URL' @{ ArtifactUrl = 'https://example.com/evil.zip' }
    Expect-Denial 'ID and URL mismatch' @{ ArtifactId = '123457' }
    Expect-Denial 'invalid source SHA' @{ SourceCommit = ('z' * 40) }
    Expect-Denial 'inconsistent run ID' @{ WorkflowRunId = '456790' }
    Write-Host 'P8-C METADATA REGRESSION PASS: valid summary plus six rejections'
}
finally {
    if (Test-Path -LiteralPath $root) { Remove-Item -LiteralPath $root -Recurse -Force }
}
