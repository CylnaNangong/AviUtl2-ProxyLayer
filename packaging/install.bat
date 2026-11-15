@echo off
chcp 65001 >nul
title ProxyLayer 一键安装
echo.
echo   ProxyLayer 一键安装（主插件 + 可选 luaJIT 转发代理）
echo   ====================================================
echo.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1" %*
echo.
echo 按任意键关闭窗口...
pause >nul
