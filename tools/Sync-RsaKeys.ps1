#Requires -Version 5.1
# Thin wrapper — implementation in lib\Invoke-SyncRsaKeys.ps1 (see also lin.ps1 sync-rsa).
param(
    [Parameter(Mandatory = $true)]
    [uint32] $E,

    [Parameter(Mandatory = $true)]
    [uint32] $D,

    [Parameter(Mandatory = $true)]
    [uint32] $N,

    [string] $Folder = "c:\python_training\LauncherWPF381\LinProj\LoginServer",

    [string] $ServerListKey = "4zF8sAc5bYkCRM3w"
)

$ErrorActionPreference = "Stop"
. "$PSScriptRoot\lib\Invoke-SyncRsaKeys.ps1"
Invoke-SyncRsaKeys @PSBoundParameters
