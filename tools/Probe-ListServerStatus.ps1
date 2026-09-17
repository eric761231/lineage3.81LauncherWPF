#Requires -Version 5.1
# Thin wrapper — implementation in lib\Invoke-ProbeListServer.ps1 (see also lin.ps1 probe-list).
param(
    [string] $ListPath = "c:\python_training\LauncherWPF381\LinProj\LoginServer\list.txt",
    [uint32] $E = 746996399,
    [uint32] $D = 365159519,
    [uint32] $N = 1833162673,
    [string] $ServerListKey = "4zF8sAc5bYkCRM3w"
)

$ErrorActionPreference = "Stop"
. "$PSScriptRoot\lib\Invoke-ProbeListServer.ps1"
Invoke-ProbeListServerStatus @PSBoundParameters
