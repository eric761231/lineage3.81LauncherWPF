#Requires -Version 5.1
# UTF-8
# Packs loose MimirPowerOverlay assets (mimir_bg.png, detail_bg.png, row_bg.png,
# row_bg_hover.png, btn_close.png, btn_confirm.png, btn_confirm_pressed.png,
# icon_*.png, 7800.png, 7801.png, 7802.png, diamond.png, mimir_ui.xml, ...)
# from a source folder into mimir_ui.pak + mimir_ui.idx.
# Whole-file XOR with the same fixed key used by LauncherDll's GetFileBuffer()
# / TW13081901.pak (PAt82IqEvNBmERYl), so the C++ loader can reuse identical
# decrypt logic. Re-run this after editing mimir_ui.xml or swapping images —
# no DLL rebuild needed, just replace the .pak/.idx in <game root>\mimir_ui\.
param(
    [Parameter(Mandatory = $true)]
    [string] $SourceFolder,

    [Parameter(Mandatory = $true)]
    [string] $OutputFolder,

    [string] $PakFileName = "mimir_ui.pak",

    [string] $IdxFileName = "mimir_ui.idx",

    [string] $FileEncryptKey = "PAt82IqEvNBmERYl",

    # Which files (in SourceFolder) to include, in this order.
    [string[]] $Files = @(
        "mimir_bg.png",
        "option.png",
        "detail_bg.png",
        "row_bg.png",
        "row_bg_hover.png",
        "btn_close.png",
        "btn_confirm.png",
        "btn_confirm_pressed.png",
        "icon_default.png",
        "icon_dmg_reduce.png",
        "7800.png",
        "7801.png",
        "7802.png",
        "diamond.png",
        "mimir_ui.xml"
    )
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

$SourceFolder = (Resolve-Path -LiteralPath $SourceFolder).Path
$OutputFolder = (Resolve-Path -LiteralPath $OutputFolder).Path

Write-Host "=== Packing Mimir UI assets ===" -ForegroundColor Cyan

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
$noBomUtf8 = New-Object System.Text.UTF8Encoding $false
[System.IO.File]::WriteAllLines($idxPath, $indexLines, $noBomUtf8)

Write-Host "`nWrote:" -ForegroundColor Green
Write-Host "  $pakPath  ($($cipher.Length) bytes)"
Write-Host "  $idxPath"
Write-Host "`nCopy both files into <game root>\mimir_ui\ next to LauncherDll.dll (no rebuild needed)." -ForegroundColor Green
