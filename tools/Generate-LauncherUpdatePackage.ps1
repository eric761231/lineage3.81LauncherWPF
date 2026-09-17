#Requires -Version 5.1
# Thin wrapper — implementation in lib\Invoke-GenerateUpdatePackage.ps1 (see also lin.ps1 gen-update).
param(
    [Parameter(Mandatory = $true)]
    [string] $SourceDir,

    [Parameter(Mandatory = $true)]
    [string] $OutputDir,

    [string] $BaseUrl = "http://localhost/updates",

    [string] $Version = "",

    [string] $FileEncryptKey = "PAt82IqEvNBmERYl"
)

$ErrorActionPreference = "Stop"
. "$PSScriptRoot\lib\Invoke-GenerateUpdatePackage.ps1"
Invoke-GenerateLauncherUpdatePackage @PSBoundParameters
