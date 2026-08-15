#Requires -Version 5.1
# Rebuild Login.ini from a known-good list.txt (213-byte wire entries),
# using the short 101-byte ANSI layout that old Login.exe expects.
# Also can re-stamp RSA E/D/N into list.txt.
# Never prints IP / names / key material — only counts and boolean checks.
param(
    [Parameter(Mandatory = $true)]
    [uint32] $E,

    [Parameter(Mandatory = $true)]
    [uint32] $D,

    [Parameter(Mandatory = $true)]
    [uint32] $N,

    [string] $Folder = "c:\python_training\LauncherWPF381\LinProj\LoginServer",

    [string] $ServerListKey = "4zF8sAc5bYkCRM3w",

    # Also copy rebuilt Login.ini / list.txt to this game root (optional).
    [string] $GameRoot = "C:\357E0~1.81L"
)

$ErrorActionPreference = "Stop"

Add-Type -TypeDefinition @"
using System;
using System.Security.Cryptography;
public static class ServerListCrypto {
  static readonly byte[] XorTableBase = {
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
  };
  public static void Decrypt(byte[] key, byte[] buffer) {
    using (Aes aes = Aes.Create()) {
      aes.Key = key; aes.Mode = CipherMode.ECB; aes.Padding = PaddingMode.None;
      using (var d = aes.CreateDecryptor()) {
        int count = buffer.Length / 16;
        for (int i = 0; i < count; i++)
          d.TransformBlock(buffer, i * 16, 16, buffer, i * 16);
      }
    }
    byte[] xorTable = new byte[256];
    Array.Copy(XorTableBase, xorTable, 256);
    for (int i = 0; i < 256; i++) xorTable[i] ^= key[i % 16];
    for (int i = 0; i < buffer.Length; i++) buffer[i] ^= xorTable[i % 256];
  }
  public static void Encrypt(byte[] key, byte[] buffer) {
    byte[] xorTable = new byte[256];
    Array.Copy(XorTableBase, xorTable, 256);
    for (int i = 0; i < 256; i++) xorTable[i] ^= key[i % 16];
    for (int i = 0; i < buffer.Length; i++) buffer[i] ^= xorTable[i % 256];
    using (Aes aes = Aes.Create()) {
      aes.Key = key; aes.Mode = CipherMode.ECB; aes.Padding = PaddingMode.None;
      using (var e = aes.CreateEncryptor()) {
        int count = buffer.Length / 16;
        for (int i = 0; i < count; i++)
          e.TransformBlock(buffer, i * 16, 16, buffer, i * 16);
      }
    }
  }
}
"@ -Language CSharp

function Invoke-ConfigDecrypt([byte[]] $key, [byte[]] $buffer) {
    [ServerListCrypto]::Decrypt($key, $buffer)
}

function Invoke-ConfigEncrypt([byte[]] $key, [byte[]] $buffer) {
    [ServerListCrypto]::Encrypt($key, $buffer)
}

function Set-UInt32LE([byte[]] $buf, [int] $offset, [uint32] $value) {
    $bytes = [BitConverter]::GetBytes([uint32]$value)
    if (-not [BitConverter]::IsLittleEndian) { [Array]::Reverse($bytes) }
    [Array]::Copy($bytes, 0, $buf, $offset, 4)
}

function Get-UInt32LE([byte[]] $buf, [int] $offset) {
    return [BitConverter]::ToUInt32($buf, $offset)
}

function Read-WireEntries([string] $listPath, [byte[]] $key) {
    $entries = New-Object System.Collections.Generic.List[object]
    foreach ($line in [System.IO.File]::ReadAllLines($listPath)) {
        if ($line -notmatch '^ServerData\d+=(.+)$') { continue }
        $buf = [Convert]::FromBase64String($Matches[1].Trim())
        if ($buf.Length -ne 213) { throw "list.txt entry length $($buf.Length), expected 213" }
        Invoke-ConfigDecrypt $key $buf
        Set-UInt32LE $buf 185 $E
        Set-UInt32LE $buf 189 $D
        Set-UInt32LE $buf 193 $N
        $port = [BitConverter]::ToInt32($buf, 96)
        if ($port -le 0 -or $port -gt 65535) { throw "list.txt entry has invalid port after decrypt" }
        $entries.Add($buf) | Out-Null
    }
    if ($entries.Count -eq 0) { throw "no ServerData entries in $listPath" }
    return $entries
}

function Convert-WireToShort101([byte[]] $wire) {
    # Short ANSI layout (101): name[32] ip[32] port used key[16] enc help bd rand e d n
    $out = New-Object byte[] 101
    $uniName = [Text.Encoding]::Unicode.GetString($wire, 0, 64).TrimEnd([char]0)
    $ansi = [Text.Encoding]::Default.GetBytes($uniName)
    $nlen = [Math]::Min(31, $ansi.Length)
    if ($nlen -gt 0) { [Array]::Copy($ansi, 0, $out, 0, $nlen) }
    [Array]::Copy($wire, 64, $out, 32, 32)   # ip ascii
    [Array]::Copy($wire, 96, $out, 64, 4)    # port
    $out[68] = $wire[100]                   # used
    [Array]::Copy($wire, 101, $out, 69, 16) # key
    $out[85] = $wire[117]                   # encrypt
    $out[86] = $wire[118]                   # usehelper
    $out[87] = $wire[119]                   # usebd
    $out[88] = $wire[184]                   # randkey
    Set-UInt32LE $out 89 $E
    Set-UInt32LE $out 93 $D
    Set-UInt32LE $out 97 $N
    return $out
}

function Write-ListTxt([string] $path, $wireEntries, [byte[]] $key) {
    $sb = New-Object System.Text.StringBuilder
    [void]$sb.AppendLine("[list]")
    for ($i = 0; $i -lt $wireEntries.Count; $i++) {
        $buf = New-Object byte[] 213
        [Array]::Copy($wireEntries[$i], $buf, 213)
        Invoke-ConfigEncrypt $key $buf
        [void]$sb.AppendLine("ServerData$i=$([Convert]::ToBase64String($buf))")
    }
    $utf8 = New-Object System.Text.UTF8Encoding $false
    [System.IO.File]::WriteAllText($path, $sb.ToString(), $utf8)
}

function Write-LoginIni([string] $path, $wireEntries, [byte[]] $key) {
    # Old Login.exe reads via GetPrivateProfile* → system ANSI (CP950 here).
    $ansiEnc = [Text.Encoding]::Default
    $chunks = New-Object System.Collections.Generic.List[byte]
    for ($i = 0; $i -lt $wireEntries.Count; $i++) {
        $wire = $wireEntries[$i]
        $uniName = [Text.Encoding]::Unicode.GetString($wire, 0, 64).TrimEnd([char]0)
        $short = Convert-WireToShort101 $wire
        # sanity before encrypt
        $port = [BitConverter]::ToInt32($short, 64)
        if ($port -le 0 -or $port -gt 65535) { throw "short101 invalid port before encrypt" }
        if ((Get-UInt32LE $short 89) -ne $E -or (Get-UInt32LE $short 93) -ne $D -or (Get-UInt32LE $short 97) -ne $N) {
            throw "short101 RSA stamp mismatch"
        }
        Invoke-ConfigEncrypt $key $short
        # round-trip verify without printing payload
        $check = New-Object byte[] 101
        [Array]::Copy($short, $check, 101)
        Invoke-ConfigDecrypt $key $check
        $port2 = [BitConverter]::ToInt32($check, 64)
        if ($port2 -ne $port) { throw "short101 encrypt round-trip corrupted port" }
        if ((Get-UInt32LE $check 89) -ne $E) { throw "short101 encrypt round-trip corrupted E" }

        $sec = $i + 1
        $header = $ansiEnc.GetBytes("[Server$sec]`r`nServerName=")
        $nameBytes = $ansiEnc.GetBytes($uniName)
        $mid = $ansiEnc.GetBytes("`r`nServerData=")
        $b64 = $ansiEnc.GetBytes([Convert]::ToBase64String($short))
        $nl = $ansiEnc.GetBytes("`r`n")
        $chunks.AddRange($header)
        $chunks.AddRange($nameBytes)
        $chunks.AddRange($mid)
        $chunks.AddRange($b64)
        $chunks.AddRange($nl)
        Write-Host "[OK] Login.ini Server$sec short101 round-trip portOk rsaOk"
    }
    [System.IO.File]::WriteAllBytes($path, $chunks.ToArray())
}

Write-Host "=== Rebuild Login.ini from list.txt + stamp RSA ==="
$listPath = Join-Path $Folder "list.txt"
$loginPath = Join-Path $Folder "Login.ini"
if (-not (Test-Path -LiteralPath $listPath)) { throw "missing $listPath" }

$key = [Text.Encoding]::ASCII.GetBytes($ServerListKey)
$wires = Read-WireEntries $listPath $key
Write-Host "[OK] loaded $($wires.Count) list.txt entries"

Write-ListTxt $listPath $wires $key
Write-Host "[OK] rewrote list.txt with stamped RSA"

Write-LoginIni $loginPath $wires $key
Write-Host "[OK] rebuilt Login.ini (ANSI/CP950, short101)"

$packPath = Join-Path $Folder "pack.properties"
$pack = @"
; Synced by Rebuild-LoginIniFromList.ps1 — merge into server ./config/pack.properties
Autoentication=True
RSA_KEY_E=$E
RSA_KEY_D=$D
RSA_KEY_N=$N
"@
[System.IO.File]::WriteAllText($packPath, $pack, (New-Object System.Text.UTF8Encoding $false))
Write-Host "[OK] wrote pack.properties"

if ($GameRoot -and (Test-Path -LiteralPath $GameRoot)) {
    Copy-Item -LiteralPath $loginPath -Destination (Join-Path $GameRoot "Login.ini") -Force
    Write-Host "[OK] deployed Login.ini -> GameRoot"
}

Write-Host "Done."
