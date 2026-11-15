@echo off
chcp 65001 >nul
title ProxyLayer インストール / 安装 / Install
echo.
echo   ProxyLayer - AviUtl ExEdit2 プレビュー用プロキシレイヤー / 预览代理图层
echo   インストール / 安装 / Install
echo   ============================================================
echo   本体 + 言語表示パック（言語パック3種） + オプションの luaJIT 転送プロキシ
echo   main plugin + Language packs (ja/zh/en) + optional luaJIT proxy
echo.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1" %*
echo.
echo   何かキーを押すと閉じます / 按任意键关闭 / Press any key to close...
pause >nul
