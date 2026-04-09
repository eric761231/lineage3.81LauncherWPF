@echo off
pushd "%~dp0"
title Launcher Deploy Tool
powershell -NoProfile -ExecutionPolicy Bypass -File "deploy.ps1"
popd
pause
