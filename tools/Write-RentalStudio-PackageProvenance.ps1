# P8-C: GitHub upload SHA-256 digest, published outside the unsigned fictional QA ZIP.
# Windows PowerShell 5.1; metadata only. No network access, customer data or executable launch.
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$WorkflowRunId,
    [Parameter(Mandatory=$true)][string]$SourceCommit,
    [Parameter(Mandatory=$true)][string]$ArtifactId,
    [Parameter(Mandatory=$true)][string]$ArtifactUrl,
    [Parameter(Mandatory=$true)][string]$ArtifactDigest,
    [Parameter(Mandatory=$true)][string]$SummaryPath
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ($WorkflowRunId -cnotmatch '^[1-9][0-9]*$' -or
    $ArtifactId -cnotmatch '^[1-9][0-9]*$') {
    throw 'Workflow run and artifact IDs must be positive decimal integers.'
}
if ($SourceCommit -cnotmatch '^[a-fA-F0-9]{40}$' -or
    $ArtifactDigest -cnotmatch '^[a-fA-F0-9]{64}$') {
    throw 'Source SHA and uploaded ZIP digest must be 40 and 64 hexadecimal digits.'
}
$source = $SourceCommit.ToLowerInvariant()
$digest = $ArtifactDigest.ToLowerInvariant()
$repoUrl = 'https://github.com/Sonkhe-Shongwe/vehicle-rental-system'
$expectedUrl = $repoUrl + '/actions/runs/' + $WorkflowRunId + '/artifacts/' + $ArtifactId
if ($ArtifactUrl -cne $expectedUrl) {
    throw 'Upload artifact URL does not match this repository, run and artifact ID.'
}
$parent = Split-Path -Parent $SummaryPath
if ([string]::IsNullOrWhiteSpace($parent) -or
    -not (Test-Path -LiteralPath $parent -PathType Container) -or
    (Test-Path -LiteralPath $SummaryPath -PathType Container)) {
    throw 'Invalid GitHub workflow summary file location.'
}
$tick = [string][char]96
$lines = @(
    '### P8-C: unsigned fictional Windows QA ZIP provenance',
    '',
    ('- Exact source: [' + $source + '](' + $repoUrl + '/commit/' + $source + ')'),
    ('- GitHub Actions run: [#' + $WorkflowRunId + '](' + $repoUrl + '/actions/runs/' + $WorkflowRunId + ')'),
    ('- Uploaded QA ZIP: [artifact #' + $ArtifactId + '](' + $ArtifactUrl + ')'),
    ('- **Uploaded ZIP SHA-256 (GitHub upload digest):** ' + $digest),
    '',
    'Download the full p2-windows-physical-acceptance-kit.zip and compare its ZIP hash in PowerShell:',
    '',
    (($tick * 3) + 'powershell'),
    '$actual = (Get-FileHash -LiteralPath .\p2-windows-physical-acceptance-kit.zip -Algorithm SHA256).Hash.ToLowerInvariant()',
    ('if ($actual -cne "' + $digest + '") { throw "Downloaded QA ZIP SHA-256 mismatch" }'),
    ($tick * 3),
    '',
    'After extraction, verify PACKAGE_SHA256.json using the bundled Verify-RentalStudio-PackageHash.ps1 before launching or generating local logs.',
    '',
    '**Scope:** The GitHub-run digest is outside the ZIP but is not an independently signed attestation, publisher certificate, malware scan, installer or release. Fictional data only.',
    ''
)
[IO.File]::AppendAllText($SummaryPath,
    (($lines -join [Environment]::NewLine) + [Environment]::NewLine),
    [Text.UTF8Encoding]::new($false))
Write-Host ('P8-C external ZIP SHA-256 published in GitHub Actions run Summary: ' + $digest)
