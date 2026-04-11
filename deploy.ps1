$ErrorActionPreference = "Stop"

Write-Host ">>> [1/5] Initializing paths..." -ForegroundColor Cyan
$Root = $PSScriptRoot
if ([string]::IsNullOrEmpty($Root)) { $Root = Get-Location }

$EncoderProj = "$Root\LinProj\Encoder\Encoder.csproj"
$LauncherProj = "$Root\LinProj\LinLauncher\LinLauncher.csproj"
$PartnerDir = "$Root\EncoderForPartners"
$EnvDir = "$PartnerDir\LinLauncher_Environment"
$IconPath = "$Root\LinProj\LinLauncher\Assets\LinLauncher.ico"

Write-Host ">>> [2/5] Stopping running processes..." -ForegroundColor Cyan
try {
    taskkill /F /IM encoder.exe /T /FI "STATUS eq RUNNING" 2>$null
    taskkill /F /IM LinEncoder.exe /T /FI "STATUS eq RUNNING" 2>$null
    taskkill /F /IM LinLauncher.exe /T /FI "STATUS eq RUNNING" 2>$null
} catch { }
Start-Sleep -Seconds 1

Write-Host ">>> [3/5] Compiling projects (dotnet publish)..." -ForegroundColor Cyan
Write-Host "    - Restoring packages..."
dotnet restore "$Root\LinProj\LinProj381.sln"

Write-Host "    - Publishing Encoder..."
if (Test-Path "$Root\LinProj\Encoder\publish") { Remove-Item -Recurse -Force "$Root\LinProj\Encoder\publish" }
dotnet publish "$EncoderProj" -c Release -r win-x86 --no-restore --self-contained true -p:PublishSingleFile=true -o "$Root\LinProj\Encoder\publish"

Write-Host "    - Publishing Launcher..."
if (Test-Path "$Root\LinProj\LinLauncher\publish") { Remove-Item -Recurse -Force "$Root\LinProj\LinLauncher\publish" }
dotnet publish "$LauncherProj" -c Release -r win-x86 --no-restore --self-contained true -o "$Root\LinProj\LinLauncher\publish"

Write-Host ">>> [4/5] Syncing files to distribution folder..." -ForegroundColor Cyan
if (!(Test-Path $EnvDir)) {
    New-Item -ItemType Directory -Path $EnvDir -Force
}
# Encoder publish 含 LinLauncher.dat（製作端模板，與 LinEncoder 同層）；不複製進 LinLauncher_Environment，玩家端不需該檔
Copy-Item "$Root\LinProj\Encoder\publish\*" "$PartnerDir" -Recurse -Force
Copy-Item "$Root\LinProj\LinLauncher\publish\*" "$EnvDir" -Recurse -Force

# LinLauncher.dat 由 LinLauncher.Proxy 建置，經 Encoder publish 置於上列 Encoder 目錄
Write-Host ">>> [5/5] config.dat (ConfigDatGen)..." -ForegroundColor Cyan
# 以與 LinLauncher 相同的 LauncherConfig + 加密封裝，從 LinEncoder.ini 覆寫 config.dat，避免沿用舊檔造成金鑰／內容不符
Write-Host "    - Generating config.dat via ConfigDatGen (LinEncoder.ini -> LinLauncher_Environment)..." -ForegroundColor Cyan
$IniForConfig = "$PartnerDir\LinEncoder.ini"
if (Test-Path $IniForConfig) {
    dotnet run --project "$Root\LinProj\ConfigDatGen\ConfigDatGen.csproj" -c Release -r win-x86 -- --ini $IniForConfig --out "$EnvDir\config.dat"
    if ($LASTEXITCODE -ne 0) { throw "ConfigDatGen failed with exit code $LASTEXITCODE" }
} else {
    Write-Host "    [!] Skipped config.dat: not found $IniForConfig" -ForegroundColor Yellow
}

# Cleanup old encoder names to avoid confusion
if (Test-Path "$PartnerDir\encoder.exe") { Remove-Item "$PartnerDir\encoder.exe" -Force }
if (Test-Path "$PartnerDir\encoder.pdb") { Remove-Item "$PartnerDir\encoder.pdb" -Force }

if (Test-Path $IconPath) {
    Copy-Item "$IconPath" "$PartnerDir\LinLauncher.ico" -Force
}

Write-Host "`n✅ All updates deployed successfully!" -ForegroundColor Green
Write-Host "Target: $PartnerDir"
pause
