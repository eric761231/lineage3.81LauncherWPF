#Requires -Version 5.1
# Rebuild Login.ini from list.txt (213-byte wire -> short101 ANSI) + stamp RSA.

. "$PSScriptRoot\ListCrypto.ps1"

function Convert-LinWireToShort101 {
    param(
        [Parameter(Mandatory = $true)] [byte[]] $Wire,
        [Parameter(Mandatory = $true)] [uint32] $E,
        [Parameter(Mandatory = $true)] [uint32] $D,
        [Parameter(Mandatory = $true)] [uint32] $N
    )
    $out = New-Object byte[] 101
    $uniName = [Text.Encoding]::Unicode.GetString($Wire, 0, 64).TrimEnd([char]0)
    $ansi = [Text.Encoding]::Default.GetBytes($uniName)
    $nlen = [Math]::Min(31, $ansi.Length)
    if ($nlen -gt 0) { [Array]::Copy($ansi, 0, $out, 0, $nlen) }
    [Array]::Copy($Wire, 64, $out, 32, 32)
    [Array]::Copy($Wire, 96, $out, 64, 4)
    $out[68] = $Wire[100]
    [Array]::Copy($Wire, 101, $out, 69, 16)
    $out[85] = $Wire[117]
    $out[86] = $Wire[118]
    $out[87] = $Wire[119]
    $out[88] = $Wire[184]
    Set-LinUInt32LE $out 89 $E
    Set-LinUInt32LE $out 93 $D
    Set-LinUInt32LE $out 97 $N
    return $out
}

function Read-LinWireEntries {
    param(
        [Parameter(Mandatory = $true)] [string] $ListPath,
        [Parameter(Mandatory = $true)] [byte[]] $Key,
        [Parameter(Mandatory = $true)] [uint32] $E,
        [Parameter(Mandatory = $true)] [uint32] $D,
        [Parameter(Mandatory = $true)] [uint32] $N
    )
    $entries = New-Object System.Collections.Generic.List[object]
    foreach ($line in [System.IO.File]::ReadAllLines($ListPath)) {
        if ($line -notmatch '^ServerData\d+=(.+)$') { continue }
        $buf = [Convert]::FromBase64String($Matches[1].Trim())
        if ($buf.Length -ne 213) { throw "list.txt entry length $($buf.Length), expected 213" }
        Invoke-LinConfigDecrypt $Key $buf
        Set-LinUInt32LE $buf 185 $E
        Set-LinUInt32LE $buf 189 $D
        Set-LinUInt32LE $buf 193 $N
        $port = [BitConverter]::ToInt32($buf, 96)
        if ($port -le 0 -or $port -gt 65535) { throw "list.txt entry has invalid port after decrypt" }
        $entries.Add($buf) | Out-Null
    }
    if ($entries.Count -eq 0) { throw "no ServerData entries in $ListPath" }
    return $entries
}

function Write-LinListTxt {
    param(
        [Parameter(Mandatory = $true)] [string] $Path,
        [Parameter(Mandatory = $true)] $WireEntries,
        [Parameter(Mandatory = $true)] [byte[]] $Key
    )
    $sb = New-Object System.Text.StringBuilder
    [void]$sb.AppendLine("[list]")
    for ($i = 0; $i -lt $WireEntries.Count; $i++) {
        $buf = New-Object byte[] 213
        [Array]::Copy($WireEntries[$i], $buf, 213)
        Invoke-LinConfigEncrypt $Key $buf
        [void]$sb.AppendLine("ServerData$i=$([Convert]::ToBase64String($buf))")
    }
    $utf8 = New-Object System.Text.UTF8Encoding $false
    [System.IO.File]::WriteAllText($Path, $sb.ToString(), $utf8)
}

function Write-LinLoginIni {
    param(
        [Parameter(Mandatory = $true)] [string] $Path,
        [Parameter(Mandatory = $true)] $WireEntries,
        [Parameter(Mandatory = $true)] [byte[]] $Key,
        [Parameter(Mandatory = $true)] [uint32] $E,
        [Parameter(Mandatory = $true)] [uint32] $D,
        [Parameter(Mandatory = $true)] [uint32] $N
    )
    $ansiEnc = [Text.Encoding]::Default
    $chunks = New-Object System.Collections.Generic.List[byte]
    for ($i = 0; $i -lt $WireEntries.Count; $i++) {
        $wire = $WireEntries[$i]
        $uniName = [Text.Encoding]::Unicode.GetString($wire, 0, 64).TrimEnd([char]0)
        $short = Convert-LinWireToShort101 -Wire $wire -E $E -D $D -N $N
        $port = [BitConverter]::ToInt32($short, 64)
        if ($port -le 0 -or $port -gt 65535) { throw "short101 invalid port before encrypt" }
        if ((Get-LinUInt32LE $short 89) -ne $E -or (Get-LinUInt32LE $short 93) -ne $D -or (Get-LinUInt32LE $short 97) -ne $N) {
            throw "short101 RSA stamp mismatch"
        }
        Invoke-LinConfigEncrypt $Key $short
        $check = New-Object byte[] 101
        [Array]::Copy($short, $check, 101)
        Invoke-LinConfigDecrypt $Key $check
        $port2 = [BitConverter]::ToInt32($check, 64)
        if ($port2 -ne $port) { throw "short101 encrypt round-trip corrupted port" }
        if ((Get-LinUInt32LE $check 89) -ne $E) { throw "short101 encrypt round-trip corrupted E" }

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
    [System.IO.File]::WriteAllBytes($Path, $chunks.ToArray())
}

function Invoke-RebuildLoginIniFromList {
    param(
        [Parameter(Mandatory = $true)] [uint32] $E,
        [Parameter(Mandatory = $true)] [uint32] $D,
        [Parameter(Mandatory = $true)] [uint32] $N,
        [string] $Folder = "c:\python_training\LauncherWPF381\LinProj\LoginServer",
        [string] $ServerListKey = $script:LinDefaultServerListKey,
        [string] $GameRoot = "C:\357E0~1.81L"
    )

    Write-Host "=== Rebuild Login.ini from list.txt + stamp RSA ==="
    $listPath = Join-Path $Folder "list.txt"
    $loginPath = Join-Path $Folder "Login.ini"
    if (-not (Test-Path -LiteralPath $listPath)) { throw "missing $listPath" }

    $key = Get-LinServerListKeyBytes -ServerListKey $ServerListKey
    $wires = Read-LinWireEntries -ListPath $listPath -Key $key -E $E -D $D -N $N
    Write-Host "[OK] loaded $($wires.Count) list.txt entries"

    Write-LinListTxt -Path $listPath -WireEntries $wires -Key $key
    Write-Host "[OK] rewrote list.txt with stamped RSA"

    Write-LinLoginIni -Path $loginPath -WireEntries $wires -Key $key -E $E -D $D -N $N
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
}
