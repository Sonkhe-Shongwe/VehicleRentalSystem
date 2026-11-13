# P2 portable Windows Qt Quick physical acceptance (disposable, fictional-only).
# No admin rights, network calls, or system scaling changes.
param([switch]$VerifyOnly)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$exe = Join-Path $root 'RentalStudioP2Review.exe'
$vehicles = Join-Path $root 'demo\vehicles.txt'
$rentals = Join-Path $root 'demo\rentals.txt'
$marker = Join-Path $root 'SOURCE_COMMIT.txt'
if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) { throw 'Missing bundled P2 Qt executable' }
if (-not (Test-Path -LiteralPath $vehicles -PathType Leaf) -or
    -not (Test-Path -LiteralPath $rentals -PathType Leaf)) { throw 'Missing fictional demo fixtures' }
if (-not (Test-Path -LiteralPath $marker -PathType Leaf)) { throw 'Missing source provenance file' }
$expectedVehicles = (Get-FileHash -LiteralPath $vehicles -Algorithm SHA256).Hash
$expectedRentals = (Get-FileHash -LiteralPath $rentals -Algorithm SHA256).Hash
$source = (Get-Content -LiteralPath $marker -Raw).Trim()
# Never inherit Qt artificial scaling/platform overrides from a developer shell.
foreach ($var in @('QT_SCALE_FACTOR', 'QT_QPA_PLATFORM',
                   'QT_PLUGIN_PATH', 'QML_IMPORT_PATH', 'QML2_IMPORT_PATH',
                   'QT_QPA_PLATFORM_PLUGIN_PATH')) {
    Remove-Item "Env:$var" -ErrorAction SilentlyContinue
}
$evidence = Join-Path $root 'acceptance-evidence'
New-Item -ItemType Directory -Force -Path $evidence | Out-Null
$report = Join-Path $evidence 'P2_windows_physical_review.txt'
$os = Get-CimInstance Win32_OperatingSystem
$display = Get-CimInstance Win32_VideoController |
    Select-Object -Property Name,CurrentHorizontalResolution,CurrentVerticalResolution
$meta = @(
    "P2 local physical acceptance, $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss zzz')",
    "Source SHA: $source",
    "Windows: $($os.Caption) $($os.Version)",
    "Qt6Core.dll present: $(Test-Path -LiteralPath (Join-Path $root 'Qt6Core.dll'))",
    "Display adapters / pixel resolution (not scale certification):",
    ($display | Out-String),
    "Original fictional demo hashes: $expectedVehicles / $expectedRentals",
    "Physical display scaling: NOT TESTED",
    "Manual GUI/keyboard result: NOT TESTED",
    "Owner Qt Quick toolkit decision: PENDING",
    ""
)
$meta | Set-Content -LiteralPath $report -Encoding UTF8
Write-Host "Testing portable P2 source $source in a disposable environment..."
$tmp = Join-Path $evidence 'automatic-smoke'
New-Item -ItemType Directory -Force -Path $tmp | Out-Null
Push-Location $tmp
try {
    & $exe --self-test
    if ($LASTEXITCODE -ne 0) { throw "P2 real C++ model self-test failed: $LASTEXITCODE" }
    & $exe --ui-smoke --compact
    if ($LASTEXITCODE -ne 0) { throw "P2 packaged QML smoke failed: $LASTEXITCODE" }
} finally { Pop-Location }
$afterVehicles = (Get-FileHash -LiteralPath $vehicles -Algorithm SHA256).Hash
$afterRentals = (Get-FileHash -LiteralPath $rentals -Algorithm SHA256).Hash
if ($afterVehicles -ne $expectedVehicles -or $afterRentals -ne $expectedRentals) {
    throw 'Packaged fictional source fixture changed. Treat as failure.'
}
Add-Content -LiteralPath $report -Value 'Packaged C++ model / compact QML smoke: PASS'
Add-Content -LiteralPath $report -Value 'Bundled fake fixture hashes unchanged: PASS'
Write-Host 'Packaged automatic checks PASSED. Real manual Windows scaling remains NOT TESTED.'
if (-not $VerifyOnly) {
    foreach ($scale in @(100,125,150)) {
        Write-Host "Set Windows Display Scale to $scale% if supported, then run the real Qt UI."
        $answer = Read-Host "Type CONFIRMED only after verifying the actual Settings scale (or SKIP)"
        if ($answer -cne 'CONFIRMED') {
            Add-Content -LiteralPath $report -Value "Physical scaling $scale%: NOT TESTED"
            continue
        }
        Write-Host 'Verify keyboard/mouse, live resize, 5/4 vehicles, Honda M001, Ranger rented.'
        Write-Host 'Save an application-only screenshot to acceptance-evidence (no personal information).'
        $review = Read-Host "Type PASS, FAIL, or SKIP for this physical scale"
        if ($review -notin @('PASS','FAIL')) { $review = 'NOT TESTED' }
        Add-Content -LiteralPath $report -Value "Physical scaling $scale%: $review (self-reported)"
        if ($review -eq 'FAIL') {
            $detail = Read-Host 'Describe the observed defect'
            Add-Content -LiteralPath $report -Value "  Defect: $detail"
        }
    }
    Add-Content -LiteralPath $report -Value 'Qt Creator version, kit and owner go/no-go: record separately'
    Write-Host "Review recorded at $report. Physical acceptance requires observed screenshots and owner go/no-go."
}
