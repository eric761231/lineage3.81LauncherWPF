#Requires -Version 5.1
# UTF-8
# Thin wrapper — implementation lives in lib\Invoke-PackUiAssets.ps1 (see also lin.ps1 pack-ui).
# Packs ALL of this project's self-made overlay assets into ONE shared ui.pak + ui.idx.
param(
    [Parameter(Mandatory = $true)]
    [string] $SourceFolder,

    [Parameter(Mandatory = $true)]
    [string] $OutputFolder,

    [string] $PakFileName = "ui.pak",

    [string] $IdxFileName = "ui.idx",

    [string] $FileEncryptKey = "PAt82IqEvNBmERYl",

    [string[]] $Files = $null
)

$ErrorActionPreference = "Stop"
. "$PSScriptRoot\lib\Invoke-PackUiAssets.ps1"
$null = Invoke-PackUiAssets @PSBoundParameters
