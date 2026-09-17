#Requires -Version 5.1
# UTF-8
# Unified entry for LauncherWPF381 tools.
# Usage:
#   powershell -File tools\lin.ps1 pack-ui -SourceFolder ... -OutputFolder ...
#   powershell -File tools\lin.ps1 deploy-ui
#   powershell -File tools\lin.ps1 sync-rsa -E ... -D ... -N ...
#   powershell -File tools\lin.ps1 rebuild-login -E ... -D ... -N ...
#   powershell -File tools\lin.ps1 probe-list
#   powershell -File tools\lin.ps1 verify-pak -GameRoot ...
#   powershell -File tools\lin.ps1 gen-update -SourceDir ... -OutputDir ...
#   powershell -File tools\lin.ps1 sync-flinch
#
# No param() block on purpose: unknown named args must reach $args for forwarding.
# Legacy script names remain as thin wrappers over tools\lib\*.

$ErrorActionPreference = "Stop"

function Show-LinHelp {
    @"
lin.ps1 — LauncherWPF381 tools entry

Commands:
  pack-ui       Pack tools\ui_sample into ui.pak / ui.idx
  deploy-ui     pack-ui then copy to game ui folder
  sync-flinch   Launch NpcFlinch sync GUI (Sync-NpcFlinch.ps1)
  sync-rsa      Stamp RSA E/D/N into list.txt + Login.ini
  rebuild-login Rebuild Login.ini (short101) from list.txt + stamp RSA
  probe-list    Decrypt list.txt and probe TCP/STAT
  verify-pak    Verify TW13081901.pak path / XOR
  gen-update    Generate launcher update package

Examples:
  powershell -File tools\lin.ps1 pack-ui -SourceFolder tools\ui_sample -OutputFolder tools\ui_sample\_packed
  powershell -File tools\lin.ps1 deploy-ui
  powershell -File tools\lin.ps1 sync-rsa -E 1 -D 2 -N 3
"@ | Write-Host
}

if ($args.Count -lt 1) {
    Show-LinHelp
    exit 1
}

$Command = [string]$args[0]
$forward = @()
if ($args.Count -gt 1) {
    $forward = $args[1..($args.Count - 1)]
}

$valid = @(
    "pack-ui", "deploy-ui", "sync-flinch",
    "sync-rsa", "rebuild-login", "probe-list",
    "verify-pak", "gen-update", "help"
)
if ($valid -notcontains $Command) {
    Write-Host "Unknown command: $Command" -ForegroundColor Red
    Show-LinHelp
    exit 1
}

function Invoke-LinForward {
    param(
        [Parameter(Mandatory = $true)]
        [string] $ScriptPath,
        [object[]] $ForwardArgs
    )
    if (-not (Test-Path -LiteralPath $ScriptPath)) {
        throw "Missing script: $ScriptPath"
    }
    if (-not $ForwardArgs -or $ForwardArgs.Count -eq 0) {
        & $ScriptPath
    } else {
        & $ScriptPath @ForwardArgs
    }
    if ($null -ne $LASTEXITCODE -and $LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

switch ($Command) {
    "help" {
        Show-LinHelp
    }
    "pack-ui" {
        Invoke-LinForward -ScriptPath (Join-Path $PSScriptRoot "Pack-UiAssets.ps1") -ForwardArgs $forward
    }
    "deploy-ui" {
        . (Join-Path $PSScriptRoot "lib\Invoke-DeployUiAssets.ps1")
        if (-not $forward -or $forward.Count -eq 0) {
            Invoke-DeployUiAssets
        } else {
            & {
                param(
                    [string] $SourceFolder,
                    [string] $OutputFolder,
                    [string] $DeployUiDir,
                    [string] $PakFileName,
                    [string] $IdxFileName,
                    [string] $FileEncryptKey,
                    [string[]] $Files
                )
                $splat = @{}
                foreach ($k in @($PSBoundParameters.Keys)) {
                    if ($null -ne $PSBoundParameters[$k] -and "$($PSBoundParameters[$k])" -ne "") {
                        $splat[$k] = $PSBoundParameters[$k]
                    }
                }
                Invoke-DeployUiAssets @splat
            } @forward
        }
    }
    "sync-flinch" {
        Invoke-LinForward -ScriptPath (Join-Path $PSScriptRoot "Sync-NpcFlinch.ps1") -ForwardArgs $forward
    }
    "sync-rsa" {
        Invoke-LinForward -ScriptPath (Join-Path $PSScriptRoot "Sync-RsaKeys.ps1") -ForwardArgs $forward
    }
    "rebuild-login" {
        Invoke-LinForward -ScriptPath (Join-Path $PSScriptRoot "Rebuild-LoginIniFromList.ps1") -ForwardArgs $forward
    }
    "probe-list" {
        Invoke-LinForward -ScriptPath (Join-Path $PSScriptRoot "Probe-ListServerStatus.ps1") -ForwardArgs $forward
    }
    "verify-pak" {
        Invoke-LinForward -ScriptPath (Join-Path $PSScriptRoot "Verify-Tw13081901Pak.ps1") -ForwardArgs $forward
    }
    "gen-update" {
        Invoke-LinForward -ScriptPath (Join-Path $PSScriptRoot "Generate-LauncherUpdatePackage.ps1") -ForwardArgs $forward
    }
}
