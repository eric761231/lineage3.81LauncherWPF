#Requires -Version 5.1
# UTF-8
# Packs loose DisconnectOverlay assets (bg_disconnect.png, strings.xml, ...)
# from a source folder into disconnect_ui.pak + disconnect_ui.idx.
# Whole-file XOR with the same fixed key used by LauncherDll's GetFileBuffer()
# / TW13081901.pak (PAt82IqEvNBmERYl), so the C++ loader can reuse identical
# decrypt logic. Re-run this after editing strings.xml or swapping images —
# no DLL rebuild needed, just replace the .pak/.idx in <game root>\disconnect_ui\.
param(
    [Parameter(Mandatory = $true)]
    [string] $SourceFolder,

    [Parameter(Mandatory = $true)]
    [string] $OutputFolder,

    [string] $PakFileName = "disconnect_ui.pak",

    [string] $IdxFileName = "disconnect_ui.idx",

    [string] $FileEncryptKey = "PAt82IqEvNBmERYl",

    # Which files (in SourceFolder) to include, in this order.
    [string[]] $Files = @("bg_disconnect.png", "strings.xml")
)

$ErrorActionPreference = "Stop"

function Get-XorBytes {
    param([byte[]] $Data, [byte[]] $KeyBytes)
    $out = New-Object byte[] $Data.Length
    $kLen = $KeyBytes.Length
    for ($i = 0; $i -lt $Data.Length; $i++) {
        $out[$i] = [byte]($Data[$i] -bxor $KeyBytes[$i % $kLen])
    }
    return $out
}

$keyBytes = [System.Text.Encoding]::ASCII.GetBytes($FileEncryptKey)
if ($keyBytes.Length -ne 16) { throw "FileEncryptKey must be 16 ASCII bytes." }

if (-not (Test-Path -LiteralPath $SourceFolder -PathType Container)) {
    throw "SourceFolder not found: $SourceFolder"
}
if (-not (Test-Path -LiteralPath $OutputFolder)) {
    New-Item -ItemType Directory -Path $OutputFolder | Out-Null
}

# Resolve to absolute paths: relative paths like ".\foo" resolve fine for
# PowerShell cmdlets (Test-Path above) but [System.IO.File]::ReadAllBytes
# below is a .NET API that resolves relative paths against
# [Environment]::CurrentDirectory, which can silently differ from
# PowerShell's own $PWD and drop a path segment.
$SourceFolder = (Resolve-Path -LiteralPath $SourceFolder).Path
$OutputFolder = (Resolve-Path -LiteralPath $OutputFolder).Path

Write-Host "=== Packing DisconnectOverlay assets ===" -ForegroundColor Cyan

$indexLines = New-Object System.Collections.Generic.List[string]
$allBytes = New-Object System.Collections.Generic.List[byte]
$offset = 0

foreach ($name in $Files) {
    $srcPath = Join-Path $SourceFolder $name
    if (-not (Test-Path -LiteralPath $srcPath -PathType Leaf)) {
        Write-Host "  [SKIP] Not found: $srcPath" -ForegroundColor Yellow
        continue
    }
    $bytes = [System.IO.File]::ReadAllBytes($srcPath)
    $len = $bytes.Length
    $allBytes.AddRange($bytes)
    $indexLines.Add("$name=$offset,$len")
    Write-Host "  [OK] $name  offset=$offset len=$len"
    $offset += $len
}

if ($allBytes.Count -eq 0) {
    throw "No input files found in $SourceFolder (looked for: $($Files -join ', '))."
}

$plain = $allBytes.ToArray()
$cipher = Get-XorBytes -Data $plain -KeyBytes $keyBytes

$pakPath = Join-Path $OutputFolder $PakFileName
$idxPath = Join-Path $OutputFolder $IdxFileName

[System.IO.File]::WriteAllBytes($pakPath, $cipher)
# [System.Text.Encoding]::UTF8 writes a BOM, which would corrupt the first
# idx entry's name when the C++ side reads it byte-for-byte. Use a no-BOM
# UTF8 encoding instead.
$noBomUtf8 = New-Object System.Text.UTF8Encoding $false
[System.IO.File]::WriteAllLines($idxPath, $indexLines, $noBomUtf8)

Write-Host "`nWrote:" -ForegroundColor Green
Write-Host "  $pakPath  ($($cipher.Length) bytes)"
Write-Host "  $idxPath"
Write-Host "`nCopy both files into Core\disconnect_ui\ next to LauncherDll.dll (no rebuild needed)." -ForegroundColor Green
