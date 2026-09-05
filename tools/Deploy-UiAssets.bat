@echo off
REM Deploy-UiAssets.bat: repack + deploy the self-drawn UI assets
REM (Mimir Power + Disconnect overlay). Run this after editing any image
REM under tools\ui_sample\ or mimir_ui.xml/strings.xml. No DLL rebuild needed.
setlocal

set "SCRIPT_DIR=%~dp0"
set "SOURCE=%SCRIPT_DIR%ui_sample"
set "OUTPUT=%SCRIPT_DIR%ui_sample\_packed"
set "DEPLOY=D:\天堂資料\天堂專案#380客戶端+自製登入器\ui"

echo === UI使用者介面素材打包中 ===
powershell -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%Pack-UiAssets.ps1" -SourceFolder "%SOURCE%" -OutputFolder "%OUTPUT%"
if errorlevel 1 (
    echo.
    echo [失敗] 打包失敗, 無法進行部署.
    pause
    exit /b 1
)

echo.
echo === 部署到路徑 %DEPLOY% ===
if not exist "%DEPLOY%" (
    echo [失敗] 找不到部署路徑: %DEPLOY%
    pause
    exit /b 1
)
copy /Y "%OUTPUT%\ui.pak" "%DEPLOY%\ui.pak" >nul
copy /Y "%OUTPUT%\ui.idx" "%DEPLOY%\ui.idx" >nul

echo.
echo [成功] 已布署 ui.pak / ui.idx 到 %DEPLOY%
echo 注意：每個遊戲進程只會載入一次 pak 文件，並將其緩存在記憶體中。
echo 如果遊戲正在執行中，請關閉遊戲重新啟動確認素材是否啟用
pause
