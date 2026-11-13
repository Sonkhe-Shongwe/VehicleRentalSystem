# P4 portable Windows accessibility checks. Fictional SHA-pinned data only.
# Default and -VerifyOnly run automation only; physical Narrator never auto-passes.
param([switch]$VerifyOnly)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$exe = Join-Path $root 'RentalStudioP2Review.exe'
$sourceFile = Join-Path $root 'SOURCE_COMMIT.txt'
$vehicles = Join-Path $root 'demo\vehicles.txt'
$rentals = Join-Path $root 'demo\rentals.txt'
foreach ($file in @($exe, $sourceFile, $vehicles, $rentals)) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) {
        throw "Required P4 review kit file missing: $file"
    }
}
$sha = (Get-Content -LiteralPath $sourceFile -Raw).Trim()
if ($sha -cnotmatch '^[0-9a-fA-F]{40}$') {
    throw 'SOURCE_COMMIT.txt does not contain a full Git commit SHA'
}
$initialVehicleHash = (Get-FileHash -LiteralPath $vehicles -Algorithm SHA256).Hash
$initialRentalHash = (Get-FileHash -LiteralPath $rentals -Algorithm SHA256).Hash

# Never let inherited test overrides fake physical OS display / Qt execution.
foreach ($key in @('QT_SCALE_FACTOR','QT_QPA_PLATFORM','QT_PLUGIN_PATH',
                    'QML_IMPORT_PATH','QML2_IMPORT_PATH',
                    'QT_QPA_PLATFORM_PLUGIN_PATH')) {
    Remove-Item "Env:$key" -ErrorAction SilentlyContinue
}
$dir = Join-Path $root 'acceptance-evidence'
New-Item -ItemType Directory -Path $dir -Force | Out-Null
$report = Join-Path $dir 'P4_WINDOWS_ACCESSIBILITY_RESULTS.txt'
$rows = [System.Collections.Generic.List[string]]::new()
$rows.Add("P4 Windows portable accessibility evidence $(Get-Date -Format o)")
$rows.Add("Bundled source SHA: $sha")
$rows.Add("Windows: $([Environment]::OSVersion.VersionString)")
$rows.Add("Qt Creator installed kit and local SHA: NOT TESTED")
$rows.Add("Physical Narrator announcements: NOT TESTED")
$rows.Add("Physical 100/125/150 percent display scaling: NOT TESTED")
$rows.Add("Physical Windows animations preference: NOT TESTED")
$rows.Add("Owner design / release approval: NOT TESTED")
$rows.Add("")

$checks = @(
    @{ Name='native accessible roles and states'; Arguments=@('--p4-a11y-smoke','--compact') },
    @{ Name='cross-workspace keyboard and modal focus'; Arguments=@('--p4-tab-smoke','--compact') },
    @{ Name='normal animation preference injection'; Arguments=@('--p4-motion-smoke','--compact') },
    @{ Name='reduced motion preference injection'; Arguments=@('--p4-motion-smoke','--p4-reduce-motion','--compact') }
)
$failed = $false
foreach ($check in $checks) {
    $slug = $check.Name -replace '[^a-zA-Z0-9]+','-'
    $log = Join-Path $dir "P4-$slug.log"
    $stdoutLog = Join-Path $dir "P4-$slug-stdout.log"
    $stderrLog = Join-Path $dir "P4-$slug-stderr.log"
    $exitCode = -1
    $launchError = $null
    try {
        # PowerShell 5.1 converts native stderr into NativeCommandError when
        # redirected with *> under ErrorActionPreference=Stop. Qt qInfo()
        # writes diagnostic PASS messages to stderr even when the exe exits 0.
        # Start-Process redirects both streams at the OS process boundary,
        # so only the real exit code determines pass/fail on PS 5.1 and PS 7.
        $commandArgs = [string[]]$check.Arguments
        $process = Start-Process -FilePath $exe -ArgumentList $commandArgs `
            -WorkingDirectory $dir -Wait -PassThru `
            -RedirectStandardOutput $stdoutLog `
            -RedirectStandardError $stderrLog -ErrorAction Stop
        $exitCode = $process.ExitCode
    } catch {
        $launchError = ($_ | Out-String)
    }
    # Preserve a stable human-readable log path used by the review checklist.
    # No native diagnostic message should be interpreted as a process failure.
    Set-Content -LiteralPath $log -Value "Actual process exit code: $exitCode" -Encoding UTF8
    if (Test-Path -LiteralPath $stdoutLog -PathType Leaf) {
        Add-Content -LiteralPath $log -Value '--- native stdout ---'
        Get-Content -LiteralPath $stdoutLog | Add-Content -LiteralPath $log
    }
    if (Test-Path -LiteralPath $stderrLog -PathType Leaf) {
        Add-Content -LiteralPath $log -Value '--- native stderr / Qt diagnostics ---'
        Get-Content -LiteralPath $stderrLog | Add-Content -LiteralPath $log
    }
    if ($null -ne $launchError) {
        Add-Content -LiteralPath $log -Value $launchError
    }
    if ($exitCode -ne 0) {
        $failed = $true
        $rows.Add("Packaged $($check.Name): FAIL (exit $exitCode; see $(Split-Path $log -Leaf))")
        Write-Warning "FAILED: $($check.Name) (exit $exitCode)"
    } else {
        $rows.Add("Packaged $($check.Name): PASS (exit 0; see $(Split-Path $log -Leaf))")
        Write-Host "PASS: $($check.Name)"
    }
}
$finalVehicleHash = (Get-FileHash -LiteralPath $vehicles -Algorithm SHA256).Hash
$finalRentalHash = (Get-FileHash -LiteralPath $rentals -Algorithm SHA256).Hash
if ($initialVehicleHash -eq $finalVehicleHash -and
    $initialRentalHash -eq $finalRentalHash) {
    $rows.Add('Bundled fictional fixture bytes unchanged: PASS')
} else {
    $failed = $true
    $rows.Add('Bundled fictional fixture bytes unchanged: FAIL')
}
$rows.Add('Manual reference: P4_WINDOWS_ACCESSIBILITY_ACCEPTANCE.md')
$rows | Set-Content -LiteralPath $report -Encoding UTF8
Write-Host "P4 report: $report"
if ($failed) { throw 'At least one P4 packaged check failed; do not approve accessibility' }
Write-Host 'P4 packaged automation PASS. Physical Narrator, scaling and owner acceptance remain NOT TESTED.'
