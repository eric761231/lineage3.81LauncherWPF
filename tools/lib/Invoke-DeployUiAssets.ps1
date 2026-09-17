#Requires -Version 5.1
# Deploy: pack ui assets then copy ui.pak/ui.idx to game ui folder.

. "$PSScriptRoot\Invoke-PackUiAssets.ps1"

# When this file is dot-sourced, $PSScriptRoot is tools\lib → parent is tools.
$script:LinToolsRoot = Split-Path $PSScriptRoot -Parent

function Invoke-DeployUiAssets {
    param(
        [string] $SourceFolder = (Join-Path $script:LinToolsRoot "ui_sample"),
        [string] $OutputFolder = (Join-Path $script:LinToolsRoot "ui_sample\_packed"),
        [string] $DeployUiDir = "D:\天堂資料\天堂專案#380客戶端+自製登入器\ui",
        [string] $PakFileName = "ui.pak",
        [string] $IdxFileName = "ui.idx",
        [string] $FileEncryptKey = $script:LinDefaultFileEncryptKey,
        [string[]] $Files = $null
    )

    Write-Host "=== UI使用者介面素材打包中 ===" -ForegroundColor Cyan
    $packParams = @{
        SourceFolder   = $SourceFolder
        OutputFolder   = $OutputFolder
        PakFileName    = $PakFileName
        IdxFileName    = $IdxFileName
        FileEncryptKey = $FileEncryptKey
    }
    if ($null -ne $Files) { $packParams.Files = $Files }
    $null = Invoke-PackUiAssets @packParams

    Write-Host ""
    Write-Host "=== 部署到路徑 $DeployUiDir ===" -ForegroundColor Cyan
    if (-not (Test-Path -LiteralPath $DeployUiDir)) {
        New-Item -ItemType Directory -Path $DeployUiDir -Force | Out-Null
    }

    Copy-Item -LiteralPath (Join-Path $OutputFolder $PakFileName) -Destination (Join-Path $DeployUiDir $PakFileName) -Force
    Copy-Item -LiteralPath (Join-Path $OutputFolder $IdxFileName) -Destination (Join-Path $DeployUiDir $IdxFileName) -Force

    Write-Host ""
    Write-Host "[成功] 已布署 $PakFileName / $IdxFileName 到 $DeployUiDir" -ForegroundColor Green
    Write-Host "注意：每個遊戲進程只會載入一次 pak 文件，並將其緩存在記憶體中。"
    Write-Host "如果遊戲正在執行中，請關閉遊戲重新啟動確認素材是否啟用"
}