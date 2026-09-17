#Requires -Version 5.1
# UTF-8
# Thin wrapper — implementation in lib\Invoke-VerifyPak.ps1 (see also lin.ps1 verify-pak).
param(
    [Parameter(Mandatory = $true)]
    [string] $GameRoot,

    [string] $PakFileName = "TW13081901.pak",

    [string] $FileEncryptKey = "PAt82IqEvNBmERYl",

    [switch] $CheckLog,

    [string] $LogFileName = "Core\launcher.log",

    [switch] $SaveDecryptedTo,

    [string] $DecryptedOutPath = ""
)

$ErrorActionPreference = "Stop"
. "$PSScriptRoot\lib\Invoke-VerifyPak.ps1"
Invoke-VerifyTw13081901Pak @PSBoundParameters
