@echo off
chcp 65001 >nul
title ProxyLayer アンインストール / 卸载 / Uninstall
echo.
echo   ProxyLayer アンインストール / 卸载 / Uninstall
echo   ============================================================
echo   元の luaJIT.dll を復元し、本体と言語表示パックを退避します（削除はしません）
echo   Restores the original luaJIT.dll and moves the plugin + language packs aside.
echo.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1" -Uninstall %*
echo.
echo   何かキーを押すと閉じます / 按任意键关闭 / Press any key to close...
pause >nul
