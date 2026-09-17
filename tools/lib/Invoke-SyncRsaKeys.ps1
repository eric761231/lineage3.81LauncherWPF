#Requires -Version 5.1
# Sync RSA_KEY_E/D/N into LoginServer list.txt and Login.ini ServerData blobs.

. "$PSScriptRoot\ListCrypto.ps1"

function Set-LinRsaInBlob {
    param(
        [Parameter(Mandatory = $true)] [byte[]] $Plain,
        [Parameter(Mandatory = $true)] [uint32] $E,
        [Parameter(Mandatory = $true)] [uint32] $D,
        [Parameter(Mandatory = $true)] [uint32] $N
    )
    if ($Plain.Length -eq 213) {
        Set-LinUInt32LE $Plain 185 $E
        Set-LinUInt32LE $Plain 189 $D
        Set-LinUInt32LE $Plain 193 $N
        return "wire213"
    }
    if ($Plain.Length -ge 12 -and $Plain.Length -le 160) {
        $offE = $Plain.Length - 12
        Set-LinUInt32LE $Plain $offE $E
        Set-LinUInt32LE $Plain ($offE + 4) $D
        Set-LinUInt32LE $Plain ($offE + 8) $N
        return "legacy$($Plain.Length)"
    }
    throw "Unsupported ServerData length $($Plain.Length)"
}

function Update-LinIniServerDataLines {
    param(
        [Parameter(Mandatory = $true)] [string] $Path,
        [Parameter(Mandatory = $true)] [string] $LinePattern,
        [Parameter(Mandatory = $true)] [byte[]] $Key,
        [Parameter(Mandatory = $true)] [uint32] $E,
        [Parameter(Mandatory = $true)] [uint32] $D,
        [Parameter(Mandatory = $true)] [uint32] $N
    )
    if (-not (Test-Path -LiteralPath $Path)) {
        Write-Host "[SKIP] missing: $Path"
        return 0
    }
    $lines = [System.IO.File]::ReadAllLines($Path)
    $updated = 0
    for ($i = 0; $i -lt $lines.Length; $i++) {
        $line = $lines[$i]
        if ($line -notmatch $LinePattern) { continue }
        $prefix = $Matches[1]
        $b64 = $Matches[2].Trim()
        $buf = [Convert]::FromBase64String($b64)
        Invoke-LinConfigDecrypt $Key $buf
        $kind = Set-LinRsaInBlob -Plain $buf -E $E -D $D -N $N
        $off = if ($buf.Length -eq 213) { 185 } else { $buf.Length - 12 }
        $ok = (Get-LinUInt32LE $buf $off) -eq $E -and
              (Get-LinUInt32LE $buf ($off + 4)) -eq $D -and
              (Get-LinUInt32LE $buf ($off + 8)) -eq $N
        if (-not $ok) { throw "RSA write verify failed for $Path ($kind)" }
        Invoke-LinConfigEncrypt $Key $buf
        $lines[$i] = "$prefix$([Convert]::ToBase64String($buf))"
        $updated++
        Write-Host "[OK] $([IO.Path]::GetFileName($Path)) entry#$updated format=$kind len=$($buf.Length)"
    }
    if ($updated -eq 0) {
        Write-Host "[WARN] no ServerData lines matched in $Path"
        return 0
    }
    $utf8 = New-Object System.Text.UTF8Encoding $false
    [System.IO.File]::WriteAllLines($Path, $lines, $utf8)
    return $updated
}

function Invoke-SyncRsaKeys {
    param(
        [Parameter(Mandatory = $true)] [uint32] $E,
        [Parameter(Mandatory = $true)] [uint32] $D,
        [Parameter(Mandatory = $true)] [uint32] $N,
        [string] $Folder = "c:\python_training\LauncherWPF381\LinProj\LoginServer",
        [string] $ServerListKey = $script:LinDefaultServerListKey
    )

    Write-Host "=== Sync RSA keys into LoginServer list/Login.ini ==="
    $listPath = Join-Path $Folder "list.txt"
    $loginPath = Join-Path $Folder "Login.ini"
    $key = Get-LinServerListKeyBytes -ServerListKey $ServerListKey

    $nList = Update-LinIniServerDataLines -Path $listPath -LinePattern '^(ServerData\d+=)(.+)$' -Key $key -E $E -D $D -N $N
    $nLogin = Update-LinIniServerDataLines -Path $loginPath -LinePattern '^(ServerData=)(.+)$' -Key $key -E $E -D $D -N $N

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
}
