#Requires -Version 5.1
# Thin wrapper — implementation in lib\Invoke-RebuildLoginIni.ps1 (see also lin.ps1 rebuild-login).
param(
    [Parameter(Mandatory = $true)]
    [uint32] $E,

    [Parameter(Mandatory = $true)]
    [uint32] $D,

    [Parameter(Mandatory = $true)]
    [uint32] $N,

    [string] $Folder = "c:\python_training\LauncherWPF381\LinProj\LoginServer",

    [string] $ServerListKey = "4zF8sAc5bYkCRM3w",

    [string] $GameRoot = "C:\357E0~1.81L"
)

$ErrorActionPreference = "Stop"
. "$PSScriptRoot\lib\Invoke-RebuildLoginIni.ps1"
Invoke-RebuildLoginIniFromList @PSBoundParameters
