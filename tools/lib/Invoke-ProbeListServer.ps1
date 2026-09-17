#Requires -Version 5.1
# Quiet list.txt health + TCP reachability probe (ports/status only).

. "$PSScriptRoot\ListCrypto.ps1"

function Invoke-ProbeListServerStatus {
    param(
        [string] $ListPath = "c:\python_training\LauncherWPF381\LinProj\LoginServer\list.txt",
        [uint32] $E = 746996399,
        [uint32] $D = 365159519,
        [uint32] $N = 1833162673,
        [string] $ServerListKey = $script:LinDefaultServerListKey
    )

    Ensure-LinListCrypto
    $key = Get-LinServerListKeyBytes -ServerListKey $ServerListKey
    $hosts = @{}
    Get-Content -LiteralPath $ListPath | ForEach-Object {
        if ($_ -notmatch '^ServerData(\d+)=(.+)$') { return }
        $idx = [int]$Matches[1]
        $buf = [Convert]::FromBase64String($Matches[2].Trim())
        Invoke-LinConfigDecrypt $key $buf
        $port = [BitConverter]::ToInt32($buf, 96)
        $used = $buf[100]
        $ip = [Text.Encoding]::ASCII.GetString($buf, 64, 32).TrimEnd([char]0)
        $eVal = [BitConverter]::ToUInt32($buf, 185)
        $dVal = [BitConverter]::ToUInt32($buf, 189)
        $nVal = [BitConverter]::ToUInt32($buf, 193)
        $rsaOk = ($eVal -eq $E -and $dVal -eq $D -and $nVal -eq $N)
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
                        $nRead = $stream.Read($respBuf, $total, $respBuf.Length - $total)
                        if ($nRead -le 0) { break }
                        $total += $nRead
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
                        $parts = $clean.Split([char]'/', 2)
                        $a = 0; $b = 0
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
}
