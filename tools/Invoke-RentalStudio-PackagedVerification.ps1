# P8-T: one-command, original uploaded Windows fictional QA ZIP verifier.
# Windows PowerShell 5.1. No production/customer data, install or network.
[CmdletBinding()]
param(
 [Parameter(Mandatory=$true)][string]$ZipPath,
 [Parameter(Mandatory=$true)][string]$ExpectedZipSha256,
 [Parameter(Mandatory=$true)][string]$ExpectedSourceCommit,
 [Parameter(Mandatory=$true)][string]$MinimumVCRuntimeVersion,
 [Parameter(Mandatory=$true)][string]$ReportDirectory
)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$report=[ordered]@{
 schema='rental-studio.qa.independent-upload-verification.v1'
 result='FAIL'
 last_gate='input_validation'
 source_commit=$ExpectedSourceCommit
 original_upload_sha256=$ExpectedZipSha256
 minimum_vc_runtime=$MinimumVCRuntimeVersion
 pre_extraction_zip='NOT_RUN'
 staged_file_hashes='NOT_RUN'
 packaged_files_checked=0
 fictional_self_test='NOT_RUN'
 packaged_functional_checks='NOT_RUN'
 packaged_checks_passed=0
 independent_hosted_runner=$true
 clean_windows_certified=$false
 offline_certified=$false
 installer_or_release_approved=$false
 customer_data_used=$false
}
$failure=$null
$work=$null
$reportFile=$null
try {
 if($ExpectedZipSha256 -cnotmatch '^[0-9a-f]{64}$' -or
    $ExpectedSourceCommit -cnotmatch '^[0-9a-f]{40}$' -or
    $MinimumVCRuntimeVersion -cnotmatch '^14[.][0-9]+[.][0-9]+[.][0-9]+$'){
     throw 'Invalid source, uploaded ZIP SHA-256 or compiler floor'
 }
 $folder=[IO.Path]::GetFullPath($ReportDirectory)
 if(-not(Test-Path -LiteralPath $folder -PathType Container)){
     [void](New-Item -ItemType Directory -Path $folder -Force)
 }
 $reportFile=Join-Path $folder 'p8t-verification.json'
 if(Test-Path -LiteralPath $reportFile){throw 'Existing report may not be overwritten'}
 if(-not(Test-Path -LiteralPath $ZipPath -PathType Leaf)){throw 'Original uploaded ZIP missing'}
 $pre=Join-Path $PSScriptRoot 'Inspect-RentalStudio-QAZipBeforeExtraction.ps1'
 $post=Join-Path $PSScriptRoot 'Inspect-RentalStudio-CleanWindowsPrerequisites.ps1'
 foreach($helper in @($pre,$post)){
     if(-not(Test-Path -LiteralPath $helper -PathType Leaf)){throw 'Source-pinned preflight missing'}
 }
 $report.last_gate='verify_original_zip_before_extraction'
 & powershell.exe -NoProfile -NonInteractive -File $pre -ZipPath $ZipPath -ExpectedZipSha256 $ExpectedZipSha256 -ExpectedSourceCommit $ExpectedSourceCommit
 if($LASTEXITCODE -ne 0){throw 'P8-N original uploaded ZIP rejected'}
 $report.pre_extraction_zip='PASS'
 $report.last_gate='extract_verified_original_zip'
 $work=Join-Path ([IO.Path]::GetTempPath()) ('rental-p8t-'+[guid]::NewGuid().ToString('N'))
 [void](New-Item -ItemType Directory -Path $work -ErrorAction Stop)
 $root=Join-Path $work 'original-kit'
 Expand-Archive -LiteralPath $ZipPath -DestinationPath $root -ErrorAction Stop
 foreach($name in @('SOURCE_COMMIT.txt','PACKAGE_SHA256.json','RentalStudioP2Review.exe','Check-Rental-Studio.cmd','Launch-Rental-Studio.cmd')){
     if(-not(Test-Path -LiteralPath (Join-Path $root $name) -PathType Leaf)){throw 'Extracted original QA ZIP lacks a required file'}
 }
 $report.last_gate='verify_exact_manifest_and_official_vc'
 & powershell.exe -NoProfile -NonInteractive -File $post -PackageRoot $root -ExpectedSourceCommit $ExpectedSourceCommit -MinimumVCRuntimeVersion $MinimumVCRuntimeVersion
 if($LASTEXITCODE -ne 0){throw 'P8-M original source, full manifest or hosted x64 VC refused'}
 $originalManifest=Get-Content -LiteralPath (Join-Path $root 'PACKAGE_SHA256.json') -Raw -Encoding UTF8 | ConvertFrom-Json -ErrorAction Stop
 $report.packaged_files_checked=@($originalManifest.files).Count
 if($report.packaged_files_checked -lt 1000){throw 'Source-bound Qt package file count implausibly small'}
 $report.staged_file_hashes='PASS'
 $report.last_gate='run_original_fictional_self_test'
 & (Join-Path $root 'Launch-Rental-Studio.cmd') --verify
 if($LASTEXITCODE -ne 0){throw 'Packaged fictional launcher self-test failed'}
 $report.fictional_self_test='PASS'
 $report.last_gate='run_original_eight_packaged_checks'
 & (Join-Path $root 'Check-Rental-Studio.cmd')
 if($LASTEXITCODE -ne 0){throw 'Packaged fictional lifecycle checks failed'}
 $evidence=@(Get-ChildItem -LiteralPath $root -Directory -Filter 'functional-check-evidence-*' -ErrorAction Stop)
 if($evidence.Count -ne 1){throw 'Packaged checker did not produce unique evidence'}
 $resultsFile=Join-Path $evidence[0].FullName 'RESULTS.txt'
 if(-not(Test-Path -LiteralPath $resultsFile -PathType Leaf)){throw 'Packaged results missing'}
 $lines=@(Get-Content -LiteralPath $resultsFile -Encoding UTF8)
 $passed=@($lines | Where-Object {$_ -cmatch '^.+: PASS$'}).Count
 $final=@($lines | Where-Object {$_ -ceq 'FINAL: PASS - all eight checks exited 0'}).Count
 if($passed -ne 8 -or $final -ne 1){throw 'Packaged results not exactly 8/8'}
 $report.packaged_checks_passed=$passed
 $report.packaged_functional_checks='PASS'
 $report.last_gate='complete'
 $report.result='PASS'
}
catch {
 $failure=$_.Exception.Message
}
finally {
 try {
     if($null -ne $reportFile){
         $json=ConvertTo-Json -InputObject $report -Depth 5
         [IO.File]::WriteAllText($reportFile,$json+[Environment]::NewLine,[Text.UTF8Encoding]::new($false))
     }
 }catch{
     if($null -eq $failure){$failure='Sanitized QA report could not be written'}
 }
 if($null -ne $work -and (Test-Path -LiteralPath $work)){
     Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
 }
}
if($null -ne $failure){
 Write-Host ('P8-T FAIL at '+$report.last_gate+': '+$failure)
 Write-Host 'No clean/offline Windows, installer or release acceptance.'
 exit 1
}
Write-Host ('P8-T PASS: original uploaded ZIP; '+$report.packaged_files_checked+' source-bound hashes; self-test; 8/8 packaged fictional tests.')
Write-Host 'Separate hosted runner has dev tools. This is NOT clean consumer/offline Windows or release approval.'
exit 0
