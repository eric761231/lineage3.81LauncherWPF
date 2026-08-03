$ErrorActionPreference = "Stop"

Write-Host ">>> [1/5] Initializing paths..." -ForegroundColor Cyan
$Root = $PSScriptRoot
if ([string]::IsNullOrEmpty($Root)) { $Root = Get-Location }

$EncoderProj = "$Root\LinProj\Encoder\Encoder.csproj"
$LauncherProj = "$Root\LinProj\LinLauncher\LinLauncher.csproj"
$PartnerDir = "$Root\EncoderForPartners"
$EnvDir = "$PartnerDir\Core"
$EncoderToolDir = "$PartnerDir\EncoderTool"
$IconPath = "$Root\LinProj\LinLauncher\Assets\LinLauncher.ico"

# 開發期在 Core\ 裡直接測試登入器時會產生的診斷/紀錄檔——這些含本機路徑等開發資訊，
# 絕對不能被複製給客戶端，每次部署都要清掉，確保交出去的是乾淨的版本
$DevOnlyLogFiles = @('launcher.log', 'LinLauncher_boot.log', 'LinLauncher_cwd.log', 'LinLauncher_errors.log')

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
dotnet restore "$Root\LinProj\LinLauncher.Proxy\LinLauncher.Proxy.csproj" -r win-x86
if ($LASTEXITCODE -ne 0) { throw "Restore for LinLauncher.Proxy failed with exit code $LASTEXITCODE" }
dotnet publish "$EncoderProj" -c Release -r win-x86 --no-restore --self-contained true -p:PublishSingleFile=true -o "$Root\LinProj\Encoder\publish"

Write-Host "    - Publishing Launcher..."
if (Test-Path "$Root\LinProj\LinLauncher\publish") { Remove-Item -Recurse -Force "$Root\LinProj\LinLauncher\publish" }
dotnet publish "$LauncherProj" -c Release -r win-x86 --no-restore --self-contained true -o "$Root\LinProj\LinLauncher\publish"

Write-Host ">>> [4/5] Syncing files to distribution folder..." -ForegroundColor Cyan
if (!(Test-Path $EnvDir)) {
    New-Item -ItemType Directory -Path $EnvDir -Force
}
if (!(Test-Path $EncoderToolDir)) {
    New-Item -ItemType Directory -Path $EncoderToolDir -Force
}
# Encoder publish 含 LinLauncher.dat（製作端模板，與 LinEncoder 同層）；操作端工具一律放 EncoderTool\，
# 不跟給客戶端的殼 exe／login／Core 混在 EncoderForPartners 最上層
Copy-Item "$Root\LinProj\Encoder\publish\*" "$EncoderToolDir" -Recurse -Force
Copy-Item "$Root\LinProj\LinLauncher\publish\*" "$EnvDir" -Recurse -Force

# 清掉 Core\ 裡可能殘留的開發期診斷/紀錄檔，避免夾帶本機路徑等資訊被交給客戶端
foreach ($devLog in $DevOnlyLogFiles) {
    $devLogPath = "$EnvDir\$devLog"
    if (Test-Path $devLogPath) { Remove-Item $devLogPath -Force }
}

# 確保給客戶端上傳用的 login\ 資料夾存在（只建立，不覆蓋既有內容）
# 注意：素材打包（sprite 等）走 EncoderForPartners\addon\add -> addon\update 這組既有流程，
# 不複製進這裡，避免跟真正的來源/輸出目錄脫節。
$LoginDir = "$PartnerDir\login"
if (!(Test-Path $LoginDir)) { New-Item -ItemType Directory -Path $LoginDir -Force | Out-Null }
# 舊版可能把 list.txt 誤產生在 EncoderForPartners 最上層，搬進 login\ 統一位置
if (Test-Path "$PartnerDir\list.txt") {
    Move-Item "$PartnerDir\list.txt" "$LoginDir\list.txt" -Force
}

# LinLauncher.dat 由 LinLauncher.Proxy 建置，經 Encoder publish 置於上列 Encoder 目錄
Write-Host ">>> [5/5] Generating config.dat..." -ForegroundColor Cyan
# 以與 LinLauncher 相同的 LauncherConfig + 加密封裝，從 LinEncoder.ini 覆寫 config.dat，避免沿用舊檔造成金鑰／內容不符
Write-Host "    - Generating config.dat via ConfigDatGen (LinEncoder.ini -> Core)..." -ForegroundColor Cyan
$IniForConfig = "$EncoderToolDir\LinEncoder.ini"
$ConfigDatStatus = ""
if (Test-Path $IniForConfig) {
    dotnet run --project "$Root\LinProj\ConfigDatGen\ConfigDatGen.csproj" -c Release -r win-x86 -- --ini $IniForConfig --out "$EnvDir\config.dat"
    if ($LASTEXITCODE -ne 0) { throw "ConfigDatGen failed with exit code $LASTEXITCODE" }
    $ConfigDatStatus = "已從 $IniForConfig 重新產生 $EnvDir\config.dat"
} else {
    Write-Host "    [!] Skipped config.dat: not found $IniForConfig" -ForegroundColor Yellow
    $ConfigDatStatus = "略過（找不到 $IniForConfig）"
}

# Cleanup old encoder names to avoid confusion
if (Test-Path "$EncoderToolDir\encoder.exe") { Remove-Item "$EncoderToolDir\encoder.exe" -Force }
if (Test-Path "$EncoderToolDir\encoder.pdb") { Remove-Item "$EncoderToolDir\encoder.pdb" -Force }

if (Test-Path $IconPath) {
    Copy-Item "$IconPath" "$EncoderToolDir\LinLauncher.ico" -Force
}

# 產生本次部署報告，讓操作者知道剛才做了什麼、接下來要做什麼——每次執行都新增一筆帶時間戳記的
# 紀錄，附加在檔案最後，不覆蓋，保留完整部署歷史
$ReportPath = "$PartnerDir\DEPLOY_REPORT.md"
$ReportEntry = @"
## $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')

### 這次做了什麼

- Encoder 已重新建置並複製到 ``EncoderTool\``
- LinLauncher 已重新建置（自封裝）並複製到 ``Core\``（已清除開發期診斷/紀錄檔）
- config.dat：$ConfigDatStatus

### 接下來要做什麼

1. 用 ``EncoderTool\LinEncoder.exe`` 確認／重新產生 ``login\list.txt`` 與登入殼（{名稱}.exe）
2. 確認 ``addon\update\`` 內容是最新的；如有新素材，先在 ``addon\add\`` 準備好，再用「補丁產生工具」重新打包
3. 把 ``login\``、``addon\update\``、殼 exe＋``Core\`` 上傳／複製到對應位置
4. 實際測試登入流程

---

"@
if (!(Test-Path $ReportPath)) {
    Set-Content -Path $ReportPath -Value "# 部署報告`n`n" -Encoding UTF8
}
Add-Content -Path $ReportPath -Value $ReportEntry -Encoding UTF8

Write-Host "`n✅ All updates deployed successfully!" -ForegroundColor Green
Write-Host "Target: $PartnerDir"
Write-Host "Report: $ReportPath"
pause
