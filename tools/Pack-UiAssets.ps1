#Requires -Version 5.1
# UTF-8
# Packs ALL of this project's self-made overlay assets (Mimir Power UI +
# DisconnectOverlay UI) into ONE shared ui.pak + ui.idx, since
# MimirPowerOverlay.cpp and DisconnectOverlay.cpp both call
# OverlayAssets_Load("ui", "ui") — same folderName/pakBaseName, so they hit
# the same cache entry and share one decrypted pak in memory instead of each
# loading/decrypting its own separate .pak.
# Whole-file XOR with the same fixed key used by LauncherDll's GetFileBuffer()
# / TW13081901.pak (PAt82IqEvNBmERYl), so the C++ loader can reuse identical
# decrypt logic. Re-run this after editing any .xml or swapping any image —
# no DLL rebuild needed, just replace the .pak/.idx in <game root>\ui\
# (game root = the folder GetModuleFileNameA(NULL) resolves to for the
# injected DLL, i.e. the folder holding the game's own exe
# (TW13081901.bin/lineage381.exe), NOT the Core\ folder LinLauncher.exe/
# LauncherDll.dll ship from — see OverlayAssets.cpp's header comment).
param(
    [Parameter(Mandatory = $true)]
    [string] $SourceFolder,

    [Parameter(Mandatory = $true)]
    [string] $OutputFolder,

    [string] $PakFileName = "ui.pak",

    [string] $IdxFileName = "ui.idx",

    [string] $FileEncryptKey = "PAt82IqEvNBmERYl",

    # Which files (in SourceFolder) to include, in this order.
    [string[]] $Files = @(
        # Mimir Power UI (MimirPowerOverlay.cpp / mimir_ui.xml)
        "mimir_bg.png",
        "option.png",
        "btn_close.png",
        "btn_confirm.png",
        "btn_confirm_pressed.png",
        "7800.png",
        "7801.png",
        "7802.png",
        "7803.png",
        "7804.png",
        "7805.png",
        "7806.png",
        "7807.png",
        "7808.png",
        "7809.png",
        "7810.png",
        "7811.png",
        "7812.png",
        "7813.png",
        "7814.png",
        "7815.png",
        "7816.png",
        "7817.png",
        "7818.png",
        "7819.png",
        "7820.png",
        "7821.png",
        "7822.png",
        "7823.png",
        "7824.png",
        "7825.png",
        "7826.png",
        "7827.png",
        "7828.png",
        "7829.png",
        "7830.png",
        "7831.png",
        "diamond.png",
        "mimir_ui.xml",
        # DisconnectOverlay UI (DisconnectOverlay.cpp / strings.xml)
        "bg_disconnect.png",
        "strings.xml"
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

Write-Host "=== Packing unified UI assets (Mimir + Disconnect) ===" -ForegroundColor Cyan

$indexLines = New-Object System.Collections.Generic.List[string]
$allBytes = New-Object System.Collections.Generic.List[byte]
$offset = 0
$seenNames = New-Object System.Collections.Generic.HashSet[string]

foreach ($name in $Files) {
    if (-not $seenNames.Add($name)) {
        throw "Duplicate entry name in `$Files`: $name (asset names must be unique across the whole merged pak)."
    }
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
Write-Host "`nCopy both files into <game root>\ui\ (next to the game's own exe, not Core\)." -ForegroundColor Green
