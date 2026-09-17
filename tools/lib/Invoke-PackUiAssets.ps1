#Requires -Version 5.1
# Pack unified ui.pak / ui.idx (Mimir + NpcFlinch + item_*.png + fixed buff icons).

. "$PSScriptRoot\XorFile.ps1"

function Get-UiAssetFilesFromFolder {
    # 2026-09-17：不再手動列檔名清單（漏改是常態——上次就漏了新加的
    # fixedbuff_wisdom.png/healHp.png/healMp.png）。改成直接掃 SourceFolder\png
    # 底下所有 *.png、SourceFolder\xml 底下所有 *.xml，兩邊都掃不到就退回掃
    # SourceFolder 本身（比照 Write-XorPackedAssets 既有的 png\/xml\ 子目錄慣例）。
    # 哪個格子該顯示哪張圖是 PssIcons.xml 的事（見該檔），這裡只負責「有哪些
    # 檔案要打進 pak」，兩者職責分開、互不影響。
    param(
        [Parameter(Mandatory = $true)]
        [string] $SourceFolder
    )

    $pngDir = Join-Path $SourceFolder "png"
    $pngScanDir = if (Test-Path -LiteralPath $pngDir -PathType Container) { $pngDir } else { $SourceFolder }
    $pngFiles = @(Get-ChildItem -LiteralPath $pngScanDir -Filter "*.png" -File -ErrorAction SilentlyContinue |
        Sort-Object Name -Unique | ForEach-Object { $_.Name })

    $xmlDir = Join-Path $SourceFolder "xml"
    $xmlScanDir = if (Test-Path -LiteralPath $xmlDir -PathType Container) { $xmlDir } else { $SourceFolder }
    $xmlFiles = @(Get-ChildItem -LiteralPath $xmlScanDir -Filter "*.xml" -File -ErrorAction SilentlyContinue |
        Sort-Object Name -Unique | ForEach-Object { $_.Name })

    return @($pngFiles) + @($xmlFiles)
}

function Invoke-PackUiAssets {
    param(
        [Parameter(Mandatory = $true)]
        [string] $SourceFolder,

        [Parameter(Mandatory = $true)]
        [string] $OutputFolder,

        [string] $PakFileName = "ui.pak",

        [string] $IdxFileName = "ui.idx",

        [string] $FileEncryptKey = $script:LinDefaultFileEncryptKey,

        [string[]] $Files = $null
    )

    if ($null -eq $Files) {
        $Files = Get-UiAssetFilesFromFolder -SourceFolder $SourceFolder
        Write-Host "Scanned ${SourceFolder}: found $($Files.Count) file(s) to pack." -ForegroundColor Cyan
        if ($Files.Count -eq 0) {
            Write-Host "WARNING: no .png/.xml files found under $SourceFolder — nothing will be packed." -ForegroundColor Yellow
        }
    }

    $result = Write-XorPackedAssets `
        -SourceFolder $SourceFolder `
        -OutputFolder $OutputFolder `
        -Files $Files `
        -PakFileName $PakFileName `
        -IdxFileName $IdxFileName `
        -FileEncryptKey $FileEncryptKey `
        -Title "Packing unified UI assets" `
        -RequireUniqueNames

    Write-Host "`nCopy both files into <game root>\ui\ (next to the game's own exe, not Core\)." -ForegroundColor Green
    return $result
}
