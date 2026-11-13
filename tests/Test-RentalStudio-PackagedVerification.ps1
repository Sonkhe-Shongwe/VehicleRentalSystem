# P8-T negative tests: invalid ZIP/source must never execute an application.
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$root=Join-Path ([IO.Path]::GetTempPath()) ('p8t-negative-'+[guid]::NewGuid().ToString('N'))
[void](New-Item -ItemType Directory -Path $root)
$runner=Join-Path $PSScriptRoot '..\tools\Invoke-RentalStudio-PackagedVerification.ps1'
try {
 foreach($case in @('missing_zip','fake_zip','bad_source')){
     $zip=Join-Path $root 'nonexistent.zip'
     if($case -ne 'missing_zip'){
         $zip=Join-Path $root 'fake.zip'
         [IO.File]::WriteAllBytes($zip,[Text.Encoding]::ASCII.GetBytes('NOT_AN_UPLOADED_ZIP'))
     }
     $src=if($case -eq 'bad_source'){'untrusted'}else{('a'*40)}
     $reportDir=Join-Path $root ('report-'+$case)
     & powershell.exe -NoProfile -NonInteractive -File $runner -ZipPath $zip -ExpectedZipSha256 ('b'*64) -ExpectedSourceCommit $src -MinimumVCRuntimeVersion '14.44.35229.0' -ReportDirectory $reportDir
     if($LASTEXITCODE -ne 1){throw ('P8-T did not refuse unsafe case '+$case)}
     $reportFile=Join-Path $reportDir 'p8t-verification.json'
     if(Test-Path -LiteralPath $reportFile -PathType Leaf){
         $result=Get-Content -LiteralPath $reportFile -Raw -Encoding UTF8 | ConvertFrom-Json
         if($result.result -cne 'FAIL' -or $result.fictional_self_test -cne 'NOT_RUN' -or
            $result.packaged_functional_checks -cne 'NOT_RUN' -or $result.clean_windows_certified -ne $false){
             throw ('P8-T negative case falsely claimed success: '+$case)
         }
     } elseif($case -ne 'bad_source'){
         throw ('Missing refusal JSON for '+$case)
     }
 }
 Write-Host 'P8-T PowerShell 5.1 synthetic tampered/missing ZIP and malformed source refusals PASS'
}
finally {
 if(Test-Path -LiteralPath $root){
    Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
 }
}
