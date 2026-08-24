#Requires -Version 5.1
# UTF-8
# Verify TW13081901.pak: path == GamePathHelper.ResolveBdFilePath, file exists,
# whole-file XOR with PAt82IqEvNBmERYl (same as Encoder + LauncherDll).
# Also optional: scan Core\launcher.log for [GetFileBuffer] lines.
param(
    [Parameter(Mandatory = $true)]
    [string] $GameRoot,

    [string] $PakFileName = "TW13081901.pak",

    [string] $FileEncryptKey = "PAt82IqEvNBmERYl",

    [switch] $CheckLog,

    [string] $LogFileName = "Core\launcher.log",

    [switch] $SaveDecryptedTo,

    [string] $DecryptedOutPath = ""
)

$ErrorActionPreference = "Stop"

function Get-XorDecryptedBytes {
    param([byte[]] $Cipher, [byte[]] $KeyBytes)
    $out = New-Object byte[] $Cipher.Length
    $kLen = $KeyBytes.Length
    for ($i = 0; $i -lt $Cipher.Length; $i++) {
        $out[$i] = [byte]($Cipher[$i] -bxor $KeyBytes[$i % $kLen])
    }
    return $out
}

function Get-PreviewHex([byte[]] $b, [int] $max = 32) {
    $n = [Math]::Min($max, $b.Length)
    $parts = for ($i = 0; $i -lt $n; $i++) { "{0:X2}" -f $b[$i] }
    return ($parts -join " ")
}

$root = $GameRoot.TrimEnd("\", "/")
$rel = $PakFileName.Trim()
if ([string]::IsNullOrWhiteSpace($rel)) { throw "PakFileName is empty." }

if ([System.IO.Path]::IsPathRooted($rel)) {
    $fullPath = [System.IO.Path]::GetFullPath($rel)
} else {
    $fullPath = [System.IO.Path]::GetFullPath([System.IO.Path]::Combine($root, $rel))
}

# 與 GamePathHelper.MaxBdFileCharCount 一致（wchar_t bdfile[260] 可存約 259 字元路徑）
$maxBdChars = 259
Write-Host "=== [1] Path (same as ResolveBdFilePath under game root) ===" -ForegroundColor Cyan
Write-Host "  GameRoot    : $root"
Write-Host "  PakFileName : $rel"
Write-Host "  Full path   : $fullPath"
Write-Host "  Path length : $($fullPath.Length) chars (native bdfile buffer max $maxBdChars; longer paths are truncated)"
if ($fullPath.Length -gt $maxBdChars) {
    Write-Host "  [WARN] Path longer than $maxBdChars chars — DLL may open wrong path." -ForegroundColor Yellow
}

Write-Host "`n=== [2] File exists ===" -ForegroundColor Cyan
if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) {
    Write-Host "  [FAIL] File not found." -ForegroundColor Red
    exit 1
}
$fi = Get-Item -LiteralPath $fullPath
Write-Host "  [OK] Size $($fi.Length) bytes"

Write-Host "`n=== [3] Whole-file XOR (key = FileEncryptKey / LauncherDll GetFileBuffer) ===" -ForegroundColor Cyan
Write-Host "  Key: $FileEncryptKey ($($FileEncryptKey.Length) chars)"
$keyBytes = [System.Text.Encoding]::ASCII.GetBytes($FileEncryptKey)
if ($keyBytes.Length -ne 16) { throw "Key must be 16 ASCII bytes." }

$cipher = [System.IO.File]::ReadAllBytes($fullPath)
$plain = Get-XorDecryptedBytes -Cipher $cipher -KeyBytes $keyBytes
Write-Host "  After XOR length: $($plain.Length) bytes (same as file size)"
Write-Host "  First 32 bytes (hex, decrypted): $(Get-PreviewHex $plain 32)"

if ($SaveDecryptedTo -or $DecryptedOutPath) {
    $outPath = if ($DecryptedOutPath) { $DecryptedOutPath } else { [System.IO.Path]::Combine($root, "TW13081901.pak.decrypted.bin") }
    [System.IO.File]::WriteAllBytes($outPath, $plain)
    Write-Host "  Wrote decrypted bytes to: $outPath" -ForegroundColor Green
}

Write-Host "`n=== [4] Core\launcher.log (optional) ===" -ForegroundColor Cyan
Write-Host "  Expected lines contain: [GetFileBuffer] bdfile= , file opened, len= , GetFileBuffer result:"
$logPath = [System.IO.Path]::Combine($root, $LogFileName)
if ($CheckLog) {
    if (-not (Test-Path -LiteralPath $logPath -PathType Leaf)) {
        Write-Host "  [SKIP] Not found: $logPath" -ForegroundColor Yellow
    } else {
        $lines = Get-Content -LiteralPath $logPath -ErrorAction SilentlyContinue
        $hits = $lines | Where-Object { $_ -match "GetFileBuffer|bdfile=" }
        if (-not $hits) {
            Write-Host "  [WARN] No GetFileBuffer lines in log." -ForegroundColor Yellow
        } else {
            Write-Host "  Last matching lines:" -ForegroundColor Green
            $hits | Select-Object -Last 20 | ForEach-Object { Write-Host "    $_" }
        }
    }
} else {
    Write-Host "  Run with -CheckLog to scan: $logPath"
}

Write-Host "`nDone. Cross-check: (1) path (2) file (3) XOR key (4) log after running game." -ForegroundColor Green
