# P8-N: Windows PowerShell 5.1 synthetic ZIP safety tests.
# Temporary nonexecutable fake archives only. No network, installs or app launch.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$tool = Join-Path $PSScriptRoot '..\tools\Inspect-RentalStudio-QAZipBeforeExtraction.ps1'
$root = Join-Path ([IO.Path]::GetTempPath()) ('rental-studio-p8n-' + [guid]::NewGuid().ToString('N'))
$zip = Join-Path $root 'fictional-qa.zip'
$source = ('a' * 40)
$required = @(
    'SOURCE_COMMIT.txt', 'PACKAGE_SHA256.json', 'RentalStudioP2Review.exe',
    'Qt6Core.dll', 'platforms/qwindows.dll', 'sqldrivers/qsqlite.dll',
    'demo/vehicles.txt', 'demo/rentals.txt', 'THIRD_PARTY_NOTICES/README.md'
)
function New-FictionalArchive([string[]]$Extra = @(), [string[]]$Omit = @()) {
    if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
    $archive = [IO.Compression.ZipFile]::Open($zip, [IO.Compression.ZipArchiveMode]::Create)
    try {
        foreach ($path in @($required + $Extra)) {
            if ($Omit -ccontains $path) { continue }
            $entry = $archive.CreateEntry($path, [IO.Compression.CompressionLevel]::Optimal)
            $stream = $entry.Open()
            try {
                $writer = [IO.StreamWriter]::new($stream, [Text.UTF8Encoding]::new($false))
                try {
                    $payload = if ($path -ceq 'SOURCE_COMMIT.txt') { $source } else { 'FAKE_NONEXECUTABLE:' + $path }
                    $writer.Write($payload)
                } finally { $writer.Dispose() }
            } finally { $stream.Dispose() }
        }
    } finally { $archive.Dispose() }
    return (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()
}
function Invoke-Preflight([string]$Digest,[string]$Commit = $source) {
    $prior = $ErrorActionPreference
    try {
        $ErrorActionPreference='Continue'
        $log = & powershell.exe -NoProfile -NonInteractive -File $tool -ZipPath $zip -ExpectedZipSha256 $Digest -ExpectedSourceCommit $Commit 2>&1 | Out-String
        $code = $LASTEXITCODE
    } finally { $ErrorActionPreference=$prior }
    return @{ Code=$code; Log=$log }
}
function Must-Deny([string]$Label,[string]$Digest,[string]$Commit = $source) {
    $result = Invoke-Preflight $Digest $Commit
    if ($result.Code -eq 0 -or $result.Log -match 'P8-N PRE-EXTRACTION ZIP PREFLIGHT PASS') {
        throw ('P8-N incorrectly allowed ' + $Label)
    }
    Write-Host ('PASS: deny ' + $Label)
}
try {
    [IO.Directory]::CreateDirectory($root) | Out-Null
    $hash = New-FictionalArchive
    $valid = Invoke-Preflight $hash
    if ($valid.Code -ne 0 -or $valid.Log -notmatch 'P8-N PRE-EXTRACTION ZIP PREFLIGHT PASS') {
        throw ('Valid synthetic fake archive failed: ' + $valid.Log)
    }
    if ((Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant() -cne $hash) {
        throw 'P8-N changed fake archive during preflight'
    }
    Write-Host 'PASS: complete fake archive, exact external digest/source and zero mutation'
    Must-Deny 'incorrect external SHA-256' ('b' * 64)
    Must-Deny 'invalid digest format' 'nothex'
    Must-Deny 'mismatched run source' $hash ('b' * 40)
    Must-Deny 'invalid source format' $hash 'invalid-source'
    foreach ($name in @('../outside.txt','C:/Windows/system.ini',
                      'sub\windows.txt','CON.txt','demo/../other.txt',
                      'demo//invalid.txt','NUL.config','folder/trailing.','logs:alternate',
                      'Qt6Core.DLL','/absolute.txt',
                      'demo/vehicles.txt/child.txt')) {
        $hash = New-FictionalArchive @($name)
        Must-Deny ('unsafe or colliding entry ' + $name) $hash
    }
    $hash = New-FictionalArchive @() @('sqldrivers/qsqlite.dll')
    Must-Deny 'missing SQLite plugin' $hash
    $hash = New-FictionalArchive
    [IO.File]::WriteAllText($zip, 'not a valid ZIP')
    $corruptDigest=(Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()
    Must-Deny 'corrupt archive with matching supplied digest' $corruptDigest
    Write-Host 'P8-N PRE-EXTRACTION ZIP STRUCTURE SYNTHETIC GUARDS PASS'
} finally {
    if (Test-Path -LiteralPath $root) { Remove-Item -LiteralPath $root -Recurse -Force }
}
