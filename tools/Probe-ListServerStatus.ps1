#Requires -Version 5.1
# Quiet list.txt health + TCP reachability probe (prints ports/status only, not IPs/names).
param(
    [string] $ListPath = "c:\python_training\LauncherWPF381\LinProj\LoginServer\list.txt",
    [uint32] $E = 746996399,
    [uint32] $D = 365159519,
    [uint32] $N = 1833162673
)

$ErrorActionPreference = "Stop"
Add-Type -TypeDefinition @'
using System;
using System.Security.Cryptography;
public static class ListCrypto {
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
}
'@

$key = [Text.Encoding]::ASCII.GetBytes("4zF8sAc5bYkCRM3w")
$hosts = @{}
Get-Content -LiteralPath $ListPath | ForEach-Object {
    if ($_ -notmatch '^ServerData(\d+)=(.+)$') { return }
    $idx = [int]$Matches[1]
    $buf = [Convert]::FromBase64String($Matches[2].Trim())
    [ListCrypto]::Decrypt($key, $buf)
    $port = [BitConverter]::ToInt32($buf, 96)
    $used = $buf[100]
    $ip = [Text.Encoding]::ASCII.GetString($buf, 64, 32).TrimEnd([char]0)
    $e = [BitConverter]::ToUInt32($buf, 185)
    $d = [BitConverter]::ToUInt32($buf, 189)
    $n = [BitConverter]::ToUInt32($buf, 193)
    $rsaOk = ($e -eq $E -and $d -eq $D -and $n -eq $N)
    $ipHash = (New-Object System.Security.Cryptography.SHA1Managed).ComputeHash([Text.Encoding]::ASCII.GetBytes($ip))
    $ipTag = ([BitConverter]::ToString($ipHash[0..2])).Replace("-","").ToLowerInvariant()
    $tcpOk = $false
    $statOk = $false
    $statKind = ""
    $err = ""
    try {
        $client = New-Object System.Net.Sockets.TcpClient
        $iar = $client.BeginConnect($ip, $port, $null, $null)
        $tcpOk = $iar.AsyncWaitHandle.WaitOne(2500, $false) -and $client.Connected
        if ($tcpOk) {
            $stream = $client.GetStream()
            $stream.ReadTimeout = 2500
            $stream.WriteTimeout = 2500
            $marker = [Text.Encoding]::ASCII.GetBytes("STAT")
            $stream.Write($marker, 0, $marker.Length)
            $stream.Flush()
            $respBuf = New-Object byte[] 64
            $total = 0
            try {
                while ($total -lt $respBuf.Length) {
                    $n = $stream.Read($respBuf, $total, $respBuf.Length - $total)
                    if ($n -le 0) { break }
                    $total += $n
                    if ([Array]::IndexOf($respBuf, [byte][char]"`n", 0, $total) -ge 0) { break }
                }
            } catch {
                $err = "read:" + $_.Exception.GetType().Name
            }
            $text = [Text.Encoding]::ASCII.GetString($respBuf, 0, $total).Trim()
            if ($text.StartsWith("MAINT", [StringComparison]::OrdinalIgnoreCase)) {
                $statOk = $true; $statKind = "MAINT"
            } else {
                $clean = $text.TrimEnd([char[]]@("`r","`n","%")).Trim()
                $rate = 0.0
                if ([double]::TryParse($clean, [Globalization.NumberStyles]::Any, [Globalization.CultureInfo]::InvariantCulture, [ref]$rate)) {
                    $statOk = $true
                    $statKind = "PCT"
                } elseif ($text.Contains("/")) {
                    # Legacy "{online}/{max}" — current launcher parse rejects this → shows 關閉
                    $parts = $clean.Split([char]'/', 2)
                    $a=0; $b=0
                    $legacyOk = $parts.Length -eq 2 -and [int]::TryParse($parts[0], [ref]$a) -and [int]::TryParse($parts[1], [ref]$b)
                    $statOk = $false
                    $statKind = "LEGACY_ONLINE_MAX parseOk=$legacyOk"
                } else {
                    $ascii = 0
                    foreach ($ch in $text.ToCharArray()) {
                        if ([int][char]$ch -ge 32 -and [int][char]$ch -le 126) { $ascii++ }
                    }
                    $statKind = "BAD_FMT len=$total ascii=$ascii"
                }
            }
        }
        $client.Close()
    } catch {
        $err = $_.Exception.GetType().Name
    }
    if (-not $hosts.ContainsKey($ipTag)) { $hosts[$ipTag] = @() }
    $hosts[$ipTag] += $idx
    Write-Host ("ServerData{0}: len={1} port={2} used={3} rsaOk={4} ipTag={5} ipLen={6} tcpOk={7} statOk={8} kind={9} err={10}" -f $idx,$buf.Length,$port,$used,$rsaOk,$ipTag,$ip.Length,$tcpOk,$statOk,$statKind,$err)
}
Write-Host "--- ipTag grouping (same tag = same host) ---"
foreach ($k in $hosts.Keys) {
    Write-Host ("ipTag={0} entries=[{1}]" -f $k, ($hosts[$k] -join ","))
}
