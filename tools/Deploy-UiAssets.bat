@echo off
REM Deploy-UiAssets.bat: thin wrapper → lin.ps1 deploy-ui
REM (Mimir Power + overlays). Run after editing tools\ui_sample\. No DLL rebuild needed.
setlocal

set "SCRIPT_DIR=%~dp0"

echo === UI使用者介面素材打包中 ===
powershell -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%lin.ps1" deploy-ui %*
if errorlevel 1 (
    echo.
    echo [失敗] 打包/部署失敗.
    pause
    exit /b 1
)

pause
