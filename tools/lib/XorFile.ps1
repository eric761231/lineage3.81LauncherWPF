#Requires -Version 5.1
# Shared whole-file XOR helpers (same key as LauncherDll GetFileBuffer / OverlayAssets).

$script:LinDefaultFileEncryptKey = "PAt82IqEvNBmERYl"

function Get-XorBytes {
    param(
        [Parameter(Mandatory = $true)]
        [byte[]] $Data,

        [Parameter(Mandatory = $true)]
        [byte[]] $KeyBytes
    )
    $out = New-Object byte[] $Data.Length
    $kLen = $KeyBytes.Length
    for ($i = 0; $i -lt $Data.Length; $i++) {
        $out[$i] = [byte]($Data[$i] -bxor $KeyBytes[$i % $kLen])
    }
    return $out
}

function Get-FileEncryptKeyBytes {
    param(
        [string] $FileEncryptKey = $script:LinDefaultFileEncryptKey
    )
    $keyBytes = [System.Text.Encoding]::ASCII.GetBytes($FileEncryptKey)
    if ($keyBytes.Length -ne 16) {
        throw "FileEncryptKey must be 16 ASCII bytes (got $($keyBytes.Length))."
    }
    return $keyBytes
}

function Write-XorPackedAssets {
    <#
    .SYNOPSIS
      Concatenate named source files, XOR-encrypt, write .pak + .idx (name=offset,len).
    #>
    param(
        [Parameter(Mandatory = $true)]
        [string] $SourceFolder,

        [Parameter(Mandatory = $true)]
        [string] $OutputFolder,

        [Parameter(Mandatory = $true)]
        [string[]] $Files,

        [string] $PakFileName = "ui.pak",

        [string] $IdxFileName = "ui.idx",

        [string] $FileEncryptKey = $script:LinDefaultFileEncryptKey,

        [string] $Title = "Packing assets",

        [switch] $RequireUniqueNames
    )

    $keyBytes = Get-FileEncryptKeyBytes -FileEncryptKey $FileEncryptKey

    if (-not (Test-Path -LiteralPath $SourceFolder -PathType Container)) {
        throw "SourceFolder not found: $SourceFolder"
    }
    if (-not (Test-Path -LiteralPath $OutputFolder)) {
        New-Item -ItemType Directory -Path $OutputFolder | Out-Null
    }

    $SourceFolder = (Resolve-Path -LiteralPath $SourceFolder).Path
    $OutputFolder = (Resolve-Path -LiteralPath $OutputFolder).Path

    Write-Host "=== $Title ===" -ForegroundColor Cyan

    $indexLines = New-Object System.Collections.Generic.List[string]
    $allBytes = New-Object System.Collections.Generic.List[byte]
    $offset = 0
    $seenNames = New-Object System.Collections.Generic.HashSet[string]

    # 2026-09-17：SourceFolder 底下的檔案可能是攤平放（舊版），也可能分在
    # png\ / xml\ 子目錄（新版，方便整理），依序找第一個存在的路徑；idx 裡的
    # 名稱一律是不含目錄的檔名，跟哪個子目錄無關（DLL 端本來就只用檔名查找）。
    $searchDirs = @($SourceFolder, (Join-Path $SourceFolder "png"), (Join-Path $SourceFolder "xml"))

    foreach ($name in $Files) {
        if ($RequireUniqueNames) {
            if (-not $seenNames.Add($name)) {
                throw "Duplicate entry name in Files: $name (asset names must be unique across the whole merged pak)."
            }
        }
        $srcPath = $null
        foreach ($dir in $searchDirs) {
            $candidate = Join-Path $dir $name
            if (Test-Path -LiteralPath $candidate -PathType Leaf) {
                $srcPath = $candidate
                break
            }
        }
        if (-not $srcPath) {
            Write-Host "  [SKIP] Not found: $name (looked in $($searchDirs -join ', '))" -ForegroundColor Yellow
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

    return [pscustomobject]@{
        PakPath     = $pakPath
        IdxPath     = $idxPath
        ByteLength  = $cipher.Length
        EntryCount  = $indexLines.Count
    }
}
