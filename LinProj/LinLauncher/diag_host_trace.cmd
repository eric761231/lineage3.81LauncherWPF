@echo off
chcp 65001 >nul
REM 在「尚未進入 CLR」階段失敗時，用此腳本啟動可產生 host 追蹤檔（與 LinLauncher.exe 放同一資料夾執行）。
REM 產出：同目錄 hostfxr_trace.txt
set COREHOST_TRACE=1
set COREHOST_TRACEFILE=%~dp0hostfxr_trace.txt
cd /d "%~dp0"
if exist "%COREHOST_TRACEFILE%" del "%COREHOST_TRACEFILE%"
echo 正在啟動 LinLauncher，追蹤將寫入: %COREHOST_TRACEFILE%
start "LinLauncher" /wait "%~dp0LinLauncher.exe"
echo 結束代碼: %ERRORLEVEL%
if exist "%COREHOST_TRACEFILE%" (echo 已產生追蹤檔，請將 hostfxr_trace.txt 提供除錯) else (echo 未產生追蹤檔：可能連 host 都未執行或路徑錯誤)
pause
