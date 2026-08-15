#Requires -Version 5.1
# Sync RSA_KEY_E/D/N into LoginServer\list.txt and Login.ini ServerData blobs.
# Does NOT print IPs, names, or key material — only counts / status.
param(
    [Parameter(Mandatory = $true)]
    [uint32] $E,

    [Parameter(Mandatory = $true)]
    [uint32] $D,

    [Parameter(Mandatory = $true)]
    [uint32] $N,

    [string] $Folder = "c:\python_training\LauncherWPF381\LinProj\LoginServer",

    [string] $ServerListKey = "4zF8sAc5bYkCRM3w"
)

$ErrorActionPreference = "Stop"

$xorTableBase = [byte[]]@(
    0x7E,0x89,0xDC,0x78,0x7F,0x4B,0xB6,0x4F,0x7D,0x0D,0x08,0x16,0x7C,0xCF,0x62,0x21,
    0x79,0x80,0x74,0xA4,0x78,0x42,0x1E,0x93,0x7A,0x04,0xA0,0xCA,0x7B,0xC6,0xCA,0xFD,
    0x6C,0xBC,0x2E,0xB0,0x6D,0x7E,0x44,0x87,0x6F,0x38,0xFA,0xDE,0x6E,0xFA,0x90,0xE9,
    0x6B,0xB5,0x86,0x6C,0x6A,0x77,0xEC,0x5B,0x68,0x31,0x52,0x02,0x69,0xF3,0x38,0x35,
    0x62,0xAF,0x7F,0x08,0x63,0x6D,0x15,0x3F,0x61,0x2B,0xAB,0x66,0x60,0xE9,0xC1,0x51,
    0x65,0xA6,0xD7,0xD4,0x64,0x64,0xBD,0xE3,0x66,0x22,0x03,0xBA,0x67,0xE0,0x69,0x8D,
    0x48,0xD7,0xCB,0x20,0x49,0x15,0xA1,0x17,0x4B,0x53,0x1F,0x4E,0x4A,0x91,0x75,0x79,
    0x4F,0xDE,0x63,0xFC,0x4E,0x1C,0x09,0xCB,0x4C,0x5A,0xB7,0x92,0x4D,0x98,0xDD,0xA5,
    0x46,0xC4,0x9A,0x98,0x47,0x06,0xF0,0xAF,0x45,0x40,0x4E,0xF6,0x44,0x82,0x24,0xC1,
    0x41,0xCD,0x32,0x44,0x40,0x0F,0x58,0x73,0x42,0x49,0xE6,0x2A,0x43,0x8B,0x8C,0x1D,
    0x54,0xF1,0x68,0x50,0x55,0x33,0x02,0x67,0x57,0x75,0xBC,0x3E,0x56,0xB7,0xD6,0x09,
    0x53,0xF8,0xC0,0x8C,0x52,0x3A,0xAA,0xBB,0x50,0x7C,0x14,0xE2,0x51,0xBE,0x7E,0xD5,
    0x5A,0xE2,0x39,0xE8,0x5B,0x20,0x53,0xDF,0x59,0x66,0xED,0x86,0x58,0xA4,0x87,0xB1,
    0x5D,0xEB,0x91,0x34,0x5C,0x29,0xFB,0x03,0x5E,0x6F,0x45,0x5A,0x5F,0xAD,0x2F,0x6D,
    0xE1,0x35,0x1B,0x80,0xE0,0xF7,0x71,0xB7,0xE2,0xB1,0xCF,0xEE,0xE3,0x73,0xA5,0xD9,
    0xE6,0x3C,0xB3,0x5C,0xE7,0xFE,0xD9,0x6B,0xE5,0xB8,0x67,0x32,0xE4,0x7A,0x0D,0x05
)

function Get-XorTable([byte[]]$key) {
    $t = New-Object byte[] 256
    [Array]::Copy($xorTableBase, $t, 256)
    for ($i = 0; $i -lt 256; $i++) { $t[$i] = $t[$i] -bxor $key[$i % 16] }
    return $t
}

function Invoke-ConfigDecrypt([byte[]]$key, [byte[]]$buffer) {
    $aes = [System.Security.Cryptography.Aes]::Create()
    $aes.Key = $key
    $aes.Mode = [System.Security.Cryptography.CipherMode]::ECB
    $aes.Padding = [System.Security.Cryptography.PaddingMode]::None
    $dec = $aes.CreateDecryptor()
    $count = [int]([Math]::Floor($buffer.Length / 16))
    for ($i = 0; $i -lt $count; $i++) {
        $null = $dec.TransformBlock($buffer, $i * 16, 16, $buffer, $i * 16)
    }
    $dec.Dispose(); $aes.Dispose()
    $table = Get-XorTable $key
    for ($i = 0; $i -lt $buffer.Length; $i++) { $buffer[$i] = $buffer[$i] -bxor $table[$i % 256] }
}

function Invoke-ConfigEncrypt([byte[]]$key, [byte[]]$buffer) {
    $table = Get-XorTable $key
    for ($i = 0; $i -lt $buffer.Length; $i++) { $buffer[$i] = $buffer[$i] -bxor $table[$i % 256] }
    $aes = [System.Security.Cryptography.Aes]::Create()
    $aes.Key = $key
    $aes.Mode = [System.Security.Cryptography.CipherMode]::ECB
    $aes.Padding = [System.Security.Cryptography.PaddingMode]::None
    $enc = $aes.CreateEncryptor()
    $count = [int]([Math]::Floor($buffer.Length / 16))
    for ($i = 0; $i -lt $count; $i++) {
        $null = $enc.TransformBlock($buffer, $i * 16, 16, $buffer, $i * 16)
    }
    $enc.Dispose(); $aes.Dispose()
}

function Set-UInt32LE([byte[]]$buf, [int]$offset, [uint32]$value) {
    $bytes = [BitConverter]::GetBytes([uint32]$value)
    if (-not [BitConverter]::IsLittleEndian) { [Array]::Reverse($bytes) }
    [Array]::Copy($bytes, 0, $buf, $offset, 4)
}

function Get-UInt32LE([byte[]]$buf, [int]$offset) {
    return [BitConverter]::ToUInt32($buf, $offset)
}

# Wire (213): E@185 D@189 N@193
# Ansi-ish ~101 without bdfile/fix: E@89 D@93 N@97 (last 12 bytes)
function Set-RsaInBlob([byte[]]$plain) {
    if ($plain.Length -eq 213) {
        Set-UInt32LE $plain 185 $E
        Set-UInt32LE $plain 189 $D
        Set-UInt32LE $plain 193 $N
        return "wire213"
    }
    if ($plain.Length -ge 12 -and $plain.Length -le 160) {
        # Prefer last-12 layout used by old Login.ini blobs (~99-101).
        $offE = $plain.Length - 12
        Set-UInt32LE $plain $offE $E
        Set-UInt32LE $plain ($offE + 4) $D
        Set-UInt32LE $plain ($offE + 8) $N
        return "legacy$($plain.Length)"
    }
    throw "Unsupported ServerData length $($plain.Length)"
}

function Update-IniServerDataLines([string]$path, [string]$linePattern) {
    if (-not (Test-Path -LiteralPath $path)) {
        Write-Host "[SKIP] missing: $path"
        return 0
    }
    $key = [Text.Encoding]::ASCII.GetBytes($ServerListKey)
    $lines = [System.IO.File]::ReadAllLines($path)
    $updated = 0
    for ($i = 0; $i -lt $lines.Length; $i++) {
        $line = $lines[$i]
        if ($line -notmatch $linePattern) { continue }
        $prefix = $Matches[1]
        $b64 = $Matches[2].Trim()
        $buf = [Convert]::FromBase64String($b64)
        Invoke-ConfigDecrypt $key $buf
        $kind = Set-RsaInBlob $buf
        # verify writeback without printing values
        $off = if ($buf.Length -eq 213) { 185 } else { $buf.Length - 12 }
        $ok = (Get-UInt32LE $buf $off) -eq $E -and
              (Get-UInt32LE $buf ($off + 4)) -eq $D -and
              (Get-UInt32LE $buf ($off + 8)) -eq $N
        if (-not $ok) { throw "RSA write verify failed for $path ($kind)" }
        Invoke-ConfigEncrypt $key $buf
        $lines[$i] = "$prefix$([Convert]::ToBase64String($buf))"
        $updated++
        Write-Host "[OK] $([IO.Path]::GetFileName($path)) entry#$updated format=$kind len=$($buf.Length)"
    }
    if ($updated -eq 0) {
        Write-Host "[WARN] no ServerData lines matched in $path"
        return 0
    }
    $utf8 = New-Object System.Text.UTF8Encoding $false
    [System.IO.File]::WriteAllLines($path, $lines, $utf8)
    return $updated
}

Write-Host "=== Sync RSA keys into LoginServer list/Login.ini ==="
$listPath = Join-Path $Folder "list.txt"
$loginPath = Join-Path $Folder "Login.ini"

$nList = Update-IniServerDataLines $listPath '^(ServerData\d+=)(.+)$'
$nLogin = Update-IniServerDataLines $loginPath '^(ServerData=)(.+)$'

$packPath = Join-Path $Folder "pack.properties"
$pack = @"
; Synced by Sync-RsaKeys.ps1 — merge into server ./config/pack.properties
Autoentication=True
RSA_KEY_E=$E
RSA_KEY_D=$D
RSA_KEY_N=$N
"@
[System.IO.File]::WriteAllText($packPath, $pack, (New-Object System.Text.UTF8Encoding $false))
Write-Host "[OK] wrote $packPath"
Write-Host "Done. list entries=$nList, Login.ini entries=$nLogin"
