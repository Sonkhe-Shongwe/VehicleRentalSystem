# P8-N: external, read-only pre-extraction ZIP trust-boundary inspection.
# Windows PowerShell 5.1; no execution, writes, network, install or elevation.
# The external GitHub run digest is NOT an independently signed attestation.
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$ZipPath,
    [Parameter(Mandatory=$true)][string]$ExpectedZipSha256,
    [Parameter(Mandatory=$true)][string]$ExpectedSourceCommit
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ($ExpectedZipSha256 -cnotmatch '^[0-9a-f]{64}$' -or
    $ExpectedSourceCommit -cnotmatch '^[0-9a-f]{40}$') {
    throw 'P8-N: external digest and source SHA must be lowercase full hex'
}
$item = Get-Item -LiteralPath $ZipPath -Force -ErrorAction Stop
if ($item.PSIsContainer -or
    ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
    throw 'P8-N: ZIP must be a regular file, not a junction or link'
}
if ($item.Length -lt 64 -or $item.Length -gt 268435456) {
    throw 'P8-N: ZIP size outside bounded unsigned QA transfer range'
}
$before = (Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
if ($before -cne $ExpectedZipSha256) {
    throw 'P8-N: downloaded ZIP does not match externally recorded upload digest'
}
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [IO.Compression.ZipFile]::OpenRead($item.FullName)
try {
    $entries = @($archive.Entries)
    if ($entries.Count -lt 1 -or $entries.Count -gt 5000) {
        throw 'P8-N: ZIP entry count outside reviewed bounds'
    }
    $names = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
    $files = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
    $cumulative = [long]0
    $sourceEntry = $null
    foreach ($entry in $entries) {
        $name = [string]$entry.FullName
        if ([string]::IsNullOrWhiteSpace($name) -or
            $name.StartsWith('/') -or $name.Contains('\') -or
            $name.Contains(':') -or $name.Contains([char]0) -or
            $name.Contains([char]10) -or $name.Contains([char]13)) {
            throw 'P8-N: ZIP contains a path unsafe for Windows extraction'
        }
        $isDirectory = $name.EndsWith('/')
        $path = if ($isDirectory) { $name.Substring(0,$name.Length - 1) } else { $name }
        if ([string]::IsNullOrEmpty($path)) {
            throw 'P8-N: ZIP contains an empty path'
        }
        $components = $path.Split('/')
        foreach ($part in $components) {
            if ($part -eq '' -or $part -eq '.' -or $part -eq '..' -or
                $part.EndsWith(' ') -or $part.EndsWith('.') -or
                $part -match '[\x00-\x1f<>"|?*]' -or
                $part -match '^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9]|CONIN[$]|CONOUT[$])([.]|$)') {
                throw 'P8-N: ZIP entry contains traversal, invalid or reserved Windows component'
            }
        }
        $normalized = $path.Normalize([Text.NormalizationForm]::FormC)
        if (-not $names.Add($normalized)) {
            throw 'P8-N: duplicate or Windows-case-colliding ZIP entry'
        }
        if (($entry.ExternalAttributes -shr 16) -band 0xF000) {
            $kind = (($entry.ExternalAttributes -shr 16) -band 0xF000)
            if (($isDirectory -and $kind -ne 0x4000) -or
                (-not $isDirectory -and $kind -ne 0x8000)) {
                throw 'P8-N: ZIP contains a symbolic link or unsupported Unix entry type'
            }
        }
        if ($isDirectory) {
            if ($entry.Length -ne 0) { throw 'P8-N: nonempty directory entry' }
            continue
        }
        [void]$files.Add($normalized)
        if ($entry.Length -lt 0 -or $entry.Length -gt 268435456 -or
            $entry.CompressedLength -lt 0) {
            throw 'P8-N: ZIP member exceeds reviewed file bounds'
        }
        $cumulative += [long]$entry.Length
        if ($cumulative -gt 1073741824) {
            throw 'P8-N: ZIP expanded byte total exceeds reviewed bounds'
        }
        if ($normalized -ceq 'SOURCE_COMMIT.txt') {
            if ($entry.Length -gt 128) { throw 'P8-N: source marker is oversized' }
            $sourceEntry = $entry
        }
    }
    # A file must not also be an implicit parent directory. Windows archive
    # extractors differ on these collisions even when file names are unique.
    foreach ($fileName in $files) {
        $fileParts = $fileName.Split('/')
        $prefix = ''
        for ($index = 0; $index -lt ($fileParts.Length - 1); ++$index) {
            $prefix = if ($index -eq 0) { $fileParts[$index] } else { $prefix + '/' + $fileParts[$index] }
            if ($files.Contains($prefix)) {
                throw 'P8-N: a ZIP file entry is also a parent directory'
            }
        }
    }
    foreach ($required in @('SOURCE_COMMIT.txt','PACKAGE_SHA256.json',
            'RentalStudioP2Review.exe','Qt6Core.dll','platforms/qwindows.dll',
            'sqldrivers/qsqlite.dll','demo/vehicles.txt','demo/rentals.txt',
            'THIRD_PARTY_NOTICES/README.md')) {
        if (-not $files.Contains($required)) {
            throw ('P8-N: ZIP lacks required original QA entry: ' + $required)
        }
    }
    # An exact matched external digest is mandatory before any ZIP content
    # is read. Only the small fictional source marker is ever decompressed.
    $stream = $sourceEntry.Open()
    try {
        $reader = New-Object IO.StreamReader($stream, [Text.Encoding]::UTF8, $true)
        try { $embeddedSource = $reader.ReadToEnd().Trim() }
        finally { $reader.Dispose() }
    } finally { $stream.Dispose() }
    if ($embeddedSource -cne $ExpectedSourceCommit) {
        throw 'P8-N: embedded source marker does not match the selected Actions run'
    }
    $after = (Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($after -cne $before) {
        throw 'P8-N: ZIP bytes changed during read-only preflight'
    }
    Write-Host ('P8-N PRE-EXTRACTION ZIP PREFLIGHT PASS: {0} entries, {1} file entries, {2} uncompressed bytes' -f $entries.Count,$files.Count,$cumulative)
    Write-Host ('P8-N VERIFIED EXTERNAL ZIP DIGEST: ' + $before)
    Write-Host ('P8-N EMBEDDED SOURCE MARKER: ' + $embeddedSource)
    Write-Host 'P8-N NO EXTRACTION OR EXECUTION. Not a publisher signature, malware scan, licensing approval, installer or owner clean-Windows acceptance. Run P8-M only after safe manual extraction.'
} finally { $archive.Dispose() }
