@echo off
chcp 65001 >nul
title ProxyLayer 卸载 / 还原
echo.
echo   ProxyLayer 卸载（还原原 luaJIT.dll 并移除主插件）
echo   ==================================================
echo.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1" -Uninstall %*
echo.
echo 按任意键关闭窗口...
pause >nul
