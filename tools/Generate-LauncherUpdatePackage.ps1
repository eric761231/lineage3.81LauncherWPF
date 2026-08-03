param(
    [Parameter(Mandatory = $true)]
    [string]$SourceDir,
    [Parameter(Mandatory = $true)]
    [string]$OutputDir,
    [string]$BaseUrl = "http://localhost/updates",
    [string]$Version = ""
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($Version)) {
    $Version = (Get-Date).ToUniversalTime().ToString('yyyyMMddHHmm')
}

if (-not (Test-Path $SourceDir)) {
    throw "SourceDir 不存在: $SourceDir"
}

New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
$packDir = Join-Path $OutputDir $Version
New-Item -ItemType Directory -Path $packDir -Force | Out-Null

$xorKey = [System.Text.Encoding]::ASCII.GetBytes('PAt82IqEvNBmERYl')
$files = Get-ChildItem -Path $SourceDir -File -Recurse | Where-Object { $_.Name -notin @('Thumbs.db', '.DS_Store') } | Sort-Object FullName

function Get-RelativePathCompat {
    param(
        [Parameter(Mandatory = $true)] [string]$BasePath,
        [Parameter(Mandatory = $true)] [string]$TargetPath
    )

    $baseFull = [System.IO.Path]::GetFullPath($BasePath)
    $targetFull = [System.IO.Path]::GetFullPath($TargetPath)
    if (-not $baseFull.EndsWith([System.IO.Path]::DirectorySeparatorChar)) {
        $baseFull = $baseFull + [System.IO.Path]::DirectorySeparatorChar
    }

    $baseUri = New-Object System.Uri($baseFull)
    $targetUri = New-Object System.Uri($targetFull)
    $relativeUri = $baseUri.MakeRelativeUri($targetUri)
    return [System.Uri]::UnescapeDataString($relativeUri.ToString()).Replace('\\', '/')
}

$lines = New-Object System.Collections.Generic.List[string]
$index = 0

foreach ($file in $files) {
    $fullName = $file.FullName
    $relativePath = Get-RelativePathCompat -BasePath $SourceDir -TargetPath $fullName
    $rawBytes = [System.IO.File]::ReadAllBytes($fullName)

    $md5Provider = [System.Security.Cryptography.MD5]::Create()
    try {
        $hash = $md5Provider.ComputeHash($rawBytes)
        $md5 = [System.BitConverter]::ToString($hash).Replace('-', '').ToLowerInvariant()
    }
    finally {
        $md5Provider.Dispose()
    }

    $ms = New-Object System.IO.MemoryStream
    try {
        $gzip = New-Object System.IO.Compression.GZipStream($ms, [System.IO.Compression.CompressionLevel]::Optimal, $true)
        $gzip.Write($rawBytes, 0, $rawBytes.Length)
        $gzip.Dispose()
        $compressed = $ms.ToArray()
    }
    finally {
        $ms.Dispose()
    }

    $packet = New-Object byte[] (4 + $compressed.Length)
    [System.Buffer]::BlockCopy($compressed, 0, $packet, 4, $compressed.Length)
    for ($i = 0; $i -lt 16; $i++) {
        $packet[4 + $i] = $packet[4 + $i] -bxor $xorKey[$i % $xorKey.Length]
    }

    $outPath = Join-Path $packDir ($relativePath.Replace('/', [System.IO.Path]::DirectorySeparatorChar) + '.bin')
    $outDir = Split-Path $outPath -Parent
    if ($outDir) { New-Item -ItemType Directory -Path $outDir -Force | Out-Null }
    [System.IO.File]::WriteAllBytes($outPath, $packet)

    $index++
    $lines.Add("file_$($index - 1)=$relativePath")
    $lines.Add("md5_$($index - 1)=$md5")
}

$sb = New-Object System.Text.StringBuilder
$null = $sb.AppendLine('[main]')
$null = $sb.AppendLine("count=$index")
$null = $sb.AppendLine("url=$($BaseUrl.TrimEnd('/'))")
$null = $sb.AppendLine()
$null = $sb.AppendLine('[update]')
foreach ($line in $lines) {
    $null = $sb.AppendLine($line)
}

[System.IO.File]::WriteAllText((Join-Path $packDir 'update.txt'), $sb.ToString(), [System.Text.Encoding]::UTF8)
[System.IO.File]::WriteAllText((Join-Path $packDir 'version.txt'), $Version, [System.Text.Encoding]::UTF8)

Write-Host "已生成更新包: $packDir"
Write-Host "更新清單: $(Join-Path $packDir 'update.txt')"
