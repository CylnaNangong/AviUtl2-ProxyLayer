<#
.SYNOPSIS
  One-click installer for AviUtl2 ProxyLayer.

.DESCRIPTION
  Installs ProxyLayer.aux2 plus the Language\*.aul2 display packs into the
  AviUtl2 data folder and, optionally, replaces luaJIT.dll with the bundled
  same-name forwarding proxy:
     original luaJIT.dll  ->  backup  ->  renamed to luaJIT_real.dll
     bundled proxy DLL    ->  copied to luaJIT.dll
  PE forwarders inside the proxy resolve every LuaJIT export against
  luaJIT_real.dll, which is why the rename has to happen first.

  Idempotent. All overwritten files get a .bak-<timestamp> copy.
  -Uninstall restores the original DLL and moves the plugin aside
  (nothing is ever deleted, so every step stays recoverable).

  Messages are shown in Japanese / Simplified Chinese / English, picked from
  the OS display language unless -Lang is given.

.PARAMETER ExePath
  Explicit path to aviutl2.exe (skips auto-detection).
.PARAMETER Uninstall
  Restore original luaJIT.dll and move ProxyLayer aside.
.PARAMETER SkipLuaProxy
  Install only the main plugin and display packs; never touch luaJIT.dll.
.PARAMETER Lang
  Message language: auto (default), ja, zh or en.
.PARAMETER NoPause
  Do not print the "press any key" hint (used by automation).
#>
param(
    [string]$ExePath = "",
    [switch]$Uninstall,
    [switch]$SkipLuaProxy,
    [ValidateSet('auto', 'ja', 'zh', 'en')][string]$Lang = 'auto',
    [switch]$NoPause
)

$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'

# ---------------------------------------------------------------------------
# Localization: one table, every key present in all three languages so a
# missing entry can never produce an empty line.
# ---------------------------------------------------------------------------
$M = @{
    err_exe_path_missing = @{
        ja = '指定された aviutl2.exe が見つかりません: {0}'
        zh = '指定的 aviutl2.exe 不存在: {0}'
        en = 'The specified aviutl2.exe does not exist: {0}' }
    ask_exe = @{
        ja = 'aviutl2.exe を自動検出できませんでした。フルパスを入力してください（空欄のまま Enter で中止）'
        zh = '未能自动找到 aviutl2.exe，请输入完整路径（直接回车放弃）'
        en = 'Could not locate aviutl2.exe automatically. Enter the full path (press Enter to give up)' }
    err_exe_not_found = @{
        ja = 'aviutl2.exe が見つかりません。-ExePath "D:\path\to\aviutl2.exe" を指定して再実行してください。'
        zh = '找不到 aviutl2.exe。请用 -ExePath "D:\path\to\aviutl2.exe" 重新运行。'
        en = 'aviutl2.exe not found. Re-run with -ExePath "D:\path\to\aviutl2.exe".' }
    host = @{
        ja = 'ホスト: {0}'
        zh = '宿主: {0}'
        en = 'Host: {0}' }
    data_portable = @{
        ja = 'データフォルダ（ポータブルモード）: {0}'
        zh = '数据目录（便携模式）: {0}'
        en = 'Data folder (portable mode): {0}' }
    data_std = @{
        ja = 'データフォルダ: {0}'
        zh = '数据目录: {0}'
        en = 'Data folder: {0}' }
    data_missing = @{
        ja = 'データフォルダがありません: {0}（先に AviUtl2 を一度起動してください）'
        zh = '数据目录不存在: {0}（请先启动一次 AviUtl2）'
        en = 'Data folder does not exist: {0} (start AviUtl2 once first)' }
    uninstall_start = @{
        ja = 'アンインストール / 復元を開始します ...'
        zh = '开始卸载 / 还原 ...'
        en = 'Uninstalling / restoring ...' }
    plugin_moved = @{
        ja = '本体プラグインを退避しました（ProxyLayer.aux2.removed-{0} として保持）'
        zh = '主插件已移开（保留为 ProxyLayer.aux2.removed-{0}）'
        en = 'Main plugin moved aside (kept as ProxyLayer.aux2.removed-{0})' }
    proxy_moved = @{
        ja = 'プロキシ DLL を退避しました（luaJIT.dll.proxy-removed-{0} として保持）'
        zh = '代理 DLL 已移开（保留为 luaJIT.dll.proxy-removed-{0}）'
        en = 'Proxy DLL moved aside (kept as luaJIT.dll.proxy-removed-{0})' }
    dll_restored = @{
        ja = '元の luaJIT.dll を復元しました'
        zh = '原 luaJIT.dll 已还原'
        en = 'Original luaJIT.dll restored' }
    dll_restored_bak = @{
        ja = 'バックアップから luaJIT.dll を復元しました: {0}'
        zh = '已从备份还原 luaJIT.dll: {0}'
        en = 'Restored luaJIT.dll from backup: {0}' }
    dll_restore_skipped = @{
        ja = 'luaJIT_real.dll もバックアップも見つからないため、DLL の復元をスキップしました'
        zh = '没有找到 luaJIT_real.dll 或备份，跳过 DLL 还原'
        en = 'No luaJIT_real.dll or backup found; skipping the DLL restore' }
    langpacks_moved = @{
        ja = '言語表示パックを退避しました（{0} 件、.removed-{1} として保持）'
        zh = '语言显示包已移开（{0} 个，保留为 .removed-{1}）'
        en = 'Display packs moved aside ({0} file(s), kept as .removed-{1})' }
    uninstall_done = @{
        ja = 'アンインストール完了。AviUtl2 を再起動してください。'
        zh = '卸载完成，请重启 AviUtl2。'
        en = 'Uninstall complete. Restart AviUtl2.' }
    pkg_missing_plugin = @{
        ja = 'パッケージに Plugin\ProxyLayer.aux2 がありません'
        zh = '安装包缺少 Plugin\ProxyLayer.aux2'
        en = 'The package is missing Plugin\ProxyLayer.aux2' }
    plugin_same = @{
        ja = '本体プラグインは既に本パッケージの版です。スキップ（冪等）'
        zh = '主插件已是本包版本，跳过（幂等）'
        en = 'Main plugin already matches this package; skipped (idempotent)' }
    plugin_backup = @{
        ja = '旧プラグインをバックアップしました: ProxyLayer.aux2.bak-{0}'
        zh = '已备份旧插件: ProxyLayer.aux2.bak-{0}'
        en = 'Backed up the old plugin: ProxyLayer.aux2.bak-{0}' }
    plugin_installed = @{
        ja = '本体プラグインを配置しました -> {0}'
        zh = '主插件已安装 -> {0}'
        en = 'Main plugin installed -> {0}' }
    langdir_created = @{
        ja = 'Language フォルダを作成しました: {0}'
        zh = '已创建 Language 目录: {0}'
        en = 'Created the Language folder: {0}' }
    langpacks_installed = @{
        ja = '言語表示パックを配置しました（{0} 件）-> {1}'
        zh = '语言显示包已安装（{0} 个）-> {1}'
        en = 'Display packs installed ({0} file(s)) -> {1}' }
    langpacks_same = @{
        ja = '言語表示パックは既に本パッケージの版です（{0} 件、スキップ）'
        zh = '语言显示包已是本包版本（{0} 个，跳过）'
        en = 'Display packs already match this package ({0} file(s), skipped)' }
    langpack_backup = @{
        ja = '既存の表示パックをバックアップしました: {0}.bak-{1}'
        zh = '已备份已有的显示包: {0}.bak-{1}'
        en = 'Backed up the existing display pack: {0}.bak-{1}' }
    langpack_none = @{
        ja = 'パッケージに言語表示パック（Language\*.aul2）が含まれていません'
        zh = '安装包内没有语言显示包（Language\*.aul2）'
        en = 'The package contains no display packs (Language\*.aul2)' }
    skip_luaproxy = @{
        ja = '-SkipLuaProxy が指定されたため、luaJIT 転送プロキシをスキップします'
        zh = '按 -SkipLuaProxy 跳过 luaJIT 代理 DLL'
        en = 'Skipping the luaJIT proxy because -SkipLuaProxy was given' }
    pkg_missing_proxy = @{
        ja = 'パッケージに LuaProxy\luaJIT.dll がないためスキップします（本体には影響しません）'
        zh = '安装包缺少 LuaProxy\luaJIT.dll，跳过（主插件不受影响）'
        en = 'The package has no LuaProxy\luaJIT.dll; skipped (the main plugin is unaffected)' }
    host_no_luajit = @{
        ja = 'ホストフォルダに luaJIT.dll がありません: {0}'
        zh = '宿主目录没有 luaJIT.dll: {0}'
        en = 'The host folder has no luaJIT.dll: {0}' }
    skip_proxy_note = @{
        ja = '転送プロキシの導入だけをスキップします（本体は通常どおり動作します）'
        zh = '跳过代理安装（主插件仍可正常使用）'
        en = 'Only the proxy install is skipped; the main plugin keeps working' }
    proxy_same = @{
        ja = '転送プロキシは既に本パッケージの版です。スキップ（冪等）'
        zh = '代理 DLL 已是本包版本，跳过（幂等）'
        en = 'The proxy DLL already matches this package; skipped (idempotent)' }
    dll_backup = @{
        ja = '既存の DLL をバックアップしました: luaJIT.dll.bak-{0}'
        zh = '已备份原 DLL: luaJIT.dll.bak-{0}'
        en = 'Backed up the existing DLL: luaJIT.dll.bak-{0}' }
    dll_renamed = @{
        ja = 'luaJIT.dll を luaJIT_real.dll に改名しました'
        zh = '原 luaJIT.dll 已改名为 luaJIT_real.dll'
        en = 'Renamed luaJIT.dll to luaJIT_real.dll' }
    dll_refresh = @{
        ja = 'luaJIT.dll と luaJIT_real.dll が一致しません（AviUtl2 更新の可能性）。現在の公式 DLL で転送先を更新しました'
        zh = 'luaJIT.dll 与 luaJIT_real.dll 不一致（AviUtl2 可能已更新）：已用当前官方 DLL 刷新转发目标'
        en = 'luaJIT.dll and luaJIT_real.dll differ (AviUtl2 was probably updated); refreshed the forwarding target from the current official DLL' }
    dll_refresh_warn = @{
        ja = '新しい LuaJIT でエクスポートが増減している場合、新しい Release のプロキシ DLL を使用してください（旧プロキシはシンボル不足の可能性）'
        zh = '若新版 LuaJIT 导出表有增减，请改用新版 Release 的代理 DLL（旧代理可能缺符号）'
        en = 'If the new LuaJIT changed its exports, use the proxy DLL from a newer Release (the old proxy may lack symbols)' }
    dll_reuse = @{
        ja = 'luaJIT_real.dll は現在の公式 DLL と一致。そのまま再利用します'
        zh = 'luaJIT_real.dll 与当前官方 DLL 一致，直接复用'
        en = 'luaJIT_real.dll matches the current official DLL; reusing it' }
    proxy_ready = @{
        ja = '転送プロキシを配置しました -> {0}'
        zh = '转发代理已就位 -> {0}'
        en = 'Forwarding proxy installed -> {0}' }
    forwarders = @{
        ja = 'PE forwarder は同じフォルダの luaJIT_real.dll を解決します'
        zh = 'PE forwarders 会解析到同目录的 luaJIT_real.dll'
        en = 'The PE forwarders resolve against luaJIT_real.dll in the same folder' }
    host_running = @{
        ja = 'AviUtl2 が実行中です。ファイルは置換済みですが、新しい DLL の読み込みには再起動が必要です。'
        zh = 'AviUtl2 正在运行：文件已替换，但必须重启宿主才会加载新 DLL。'
        en = 'AviUtl2 is running: the files are replaced, but the host must be restarted to load the new DLL.' }
    install_done = @{
        ja = 'インストール完了。AviUtl2 を起動してください。'
        zh = '安装完成，启动 AviUtl2 即可使用。'
        en = 'Install complete. Start AviUtl2.' }
    next_step = @{
        ja = '次の手順: 「編集」メニュー ->「代理图层」->「整个工程自动代理 (开/关)」'
        zh = '下一步：编辑菜单 -> 代理图层 -> 整个工程自动代理 (开/关)'
        en = 'Next: Edit menu -> 代理图层 -> 整个工程自动代理 (开/关)' }
    lang_hint = @{
        ja = 'UI 表示: 「設定」->「言語の設定」で 日本語 / 简体中文 / English を選択できます（Language\*.ProxyLayer.aul2）'
        zh = '界面语言：设置 -> 语言设置 -> 选择 简体中文 / 日本語 / English（Language\*.ProxyLayer.aul2）'
        en = 'UI language: Setup -> Language -> pick Japanese / Chinese / English (Language\*.ProxyLayer.aul2)' }
    menu_note = @{
        ja = '※ メニュー名は現在も中国語表記です（.aul2 で変えられるのは効果名とパラメータ名のみ）'
        zh = '※ 菜单项仍是中文（.aul2 只能改效果名与参数名）'
        en = 'Note: the menu entries are still Chinese; .aul2 only covers effect and parameter names' }
    uninstall_hint = @{
        ja = 'アンインストール: uninstall.bat を実行（元の luaJIT.dll を復元します）'
        zh = '卸载：双击 uninstall.bat（会还原原 luaJIT.dll）'
        en = 'Uninstall: run uninstall.bat (restores the original luaJIT.dll)' }
    press_key = @{
        ja = '何かキーを押すと閉じます...'
        zh = '按任意键关闭...'
        en = 'Press any key to close...' }
}

$script:L = switch ($Lang) {
    'ja' { 'ja' }
    'zh' { 'zh' }
    'en' { 'en' }
    default {
        $ui = ''
        try { $ui = (Get-UICulture).Name } catch { }
        if (-not $ui) { try { $ui = $PSUICulture } catch { } }
        if ($ui -like 'ja*') { 'ja' }
        elseif ($ui -like 'zh*') { 'zh' }
        else { 'en' }
    }
}

function Msg([string]$key, [object[]]$fmt) {
    $entry = $M[$key]
    if (-not $entry) { return $key }
    $text = $entry[$script:L]
    if (-not $text) { $text = $entry['en'] }
    if ($null -ne $fmt -and $fmt.Count -gt 0) { return ($text -f $fmt) }
    return $text
}

function Step($m) { Write-Host "[*] $m" -ForegroundColor Cyan }
function Ok($m)   { Write-Host "[OK] $m" -ForegroundColor Green }
function Warn($m) { Write-Host "[!]  $m" -ForegroundColor Yellow }
function Die($m)  { Write-Host "[X] $m" -ForegroundColor Red; exit 1 }
function Sha($p)  { (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash }

function Resolve-HostExe {
    if ($ExePath) {
        if (-not (Test-Path -LiteralPath $ExePath -PathType Leaf)) {
            Die (Msg 'err_exe_path_missing' @($ExePath))
        }
        return (Resolve-Path -LiteralPath $ExePath).Path
    }
    # 1) a running AviUtl2 is the most reliable source of truth
    $proc = Get-Process aviutl2 -ErrorAction SilentlyContinue |
            Where-Object { $_.Path } | Select-Object -First 1
    if ($proc) { return $proc.Path }

    # 2) right next to this installer, or one level up
    $near = @((Join-Path $here 'aviutl2.exe'),
              (Join-Path (Split-Path $here -Parent) 'aviutl2.exe'))
    foreach ($c in $near) {
        if (Test-Path -LiteralPath $c -PathType Leaf) { return (Resolve-Path -LiteralPath $c).Path }
    }

    # 3) common install locations
    $cands = @()
    if ($env:ProgramFiles) { $cands += (Join-Path $env:ProgramFiles 'AviUtl2\aviutl2.exe') }
    if (${env:ProgramFiles(x86)}) { $cands += (Join-Path ${env:ProgramFiles(x86)} 'AviUtl2\aviutl2.exe') }
    if ($env:LOCALAPPDATA) { $cands += (Join-Path $env:LOCALAPPDATA 'Programs\AviUtl2\aviutl2.exe') }
    foreach ($c in $cands) {
        if (Test-Path -LiteralPath $c -PathType Leaf) { return $c }
    }

    # 4) shallow scan of fixed drives (depth 3 under common roots only)
    foreach ($drive in (Get-PSDrive -PSProvider FileSystem -ErrorAction SilentlyContinue)) {
        $rootPath = $drive.Root
        if (-not $rootPath) { continue }
        $bases = @($rootPath)
        foreach ($sub in @('Program Files', 'Program Files (x86)', 'Apps', 'Games', 'Tools', 'AviUtl2')) {
            $bases += (Join-Path $rootPath $sub)
        }
        foreach ($base in $bases) {
            if (-not (Test-Path -LiteralPath $base -PathType Container)) { continue }
            try {
                $hit = Get-ChildItem -LiteralPath $base -Filter 'aviutl2.exe' -Recurse -File `
                        -Depth 3 -ErrorAction SilentlyContinue | Select-Object -First 1
                if ($hit) { return $hit.FullName }
            } catch { }
        }
    }

    # 5) last resort: ask the user (only when auto-detection failed)
    $manual = Read-Host (Msg 'ask_exe' @())
    if ($manual -and (Test-Path -LiteralPath $manual -PathType Leaf)) {
        return (Resolve-Path -LiteralPath $manual).Path
    }
    return $null
}

# ---------------------------------------------------------------------------
$exe = Resolve-HostExe
if (-not $exe) { Die (Msg 'err_exe_not_found' @()) }
$exeDir = Split-Path $exe -Parent
Step (Msg 'host' @($exe))

# NEVER create a `data` folder next to the exe: AviUtl2 switches to portable
# mode the moment it exists, which silently resets the whole environment.
$portable = Join-Path $exeDir 'data'
if (Test-Path -LiteralPath $portable -PathType Container) {
    $dataRoot = $portable
    Step (Msg 'data_portable' @($dataRoot))
} else {
    $dataRoot = Join-Path $env:ProgramData 'aviutl2'
    Step (Msg 'data_std' @($dataRoot))
}
if (-not (Test-Path -LiteralPath $dataRoot -PathType Container)) {
    Die (Msg 'data_missing' @($dataRoot))
}
$pluginDir = Join-Path $dataRoot 'Plugin'
$dstPlugin = Join-Path $pluginDir 'ProxyLayer.aux2'
$srcPlugin = Join-Path $here 'Plugin\ProxyLayer.aux2'

$langDir = Join-Path $dataRoot 'Language'
$srcLangDir = Join-Path $here 'Language'

$proxySrc = Join-Path $here 'LuaProxy\luaJIT.dll'
$origDll  = Join-Path $exeDir 'luaJIT.dll'
$realDll  = Join-Path $exeDir 'luaJIT_real.dll'

if ($Uninstall) {
    Step (Msg 'uninstall_start' @())
    # Never DELETE anything: rename aside so every step stays recoverable
    # (and so the script also works under restrictive managed environments).
    if (Test-Path -LiteralPath $dstPlugin -PathType Leaf) {
        Rename-Item -LiteralPath $dstPlugin -NewName "ProxyLayer.aux2.removed-$stamp" -Force
        Ok (Msg 'plugin_moved' @($stamp))
    }
    if (Test-Path -LiteralPath $realDll -PathType Leaf) {
        if (Test-Path -LiteralPath $origDll -PathType Leaf) {
            Rename-Item -LiteralPath $origDll -NewName "luaJIT.dll.proxy-removed-$stamp" -Force
            Ok (Msg 'proxy_moved' @($stamp))
        }
        Rename-Item -LiteralPath $realDll -NewName 'luaJIT.dll' -Force
        Ok (Msg 'dll_restored' @())
    } else {
        $bak = Get-ChildItem -LiteralPath $exeDir -Filter 'luaJIT.dll.bak-*' -File -ErrorAction SilentlyContinue |
               Sort-Object LastWriteTime -Descending | Select-Object -First 1
        if ($bak) {
            Copy-Item -LiteralPath $bak.FullName -Destination $origDll -Force
            Ok (Msg 'dll_restored_bak' @($bak.Name))
        } else {
            Warn (Msg 'dll_restore_skipped' @())
        }
    }
    # Display packs: move aside (never delete), so a reinstall can restore them.
    if (Test-Path -LiteralPath $langDir -PathType Container) {
        $packs = Get-ChildItem -LiteralPath $langDir -Filter '*.ProxyLayer.aul2' -File -ErrorAction SilentlyContinue
        $moved = 0
        foreach ($p in $packs) {
            Rename-Item -LiteralPath $p.FullName -NewName "$($p.Name).removed-$stamp" -Force
            $moved++
        }
        if ($moved -gt 0) { Ok (Msg 'langpacks_moved' @($moved, $stamp)) }
    }
    Ok (Msg 'uninstall_done' @())
    return
}

# ---- install main plugin --------------------------------------------------
if (-not (Test-Path -LiteralPath $srcPlugin -PathType Leaf)) {
    Die (Msg 'pkg_missing_plugin' @())
}
if (-not (Test-Path -LiteralPath $pluginDir -PathType Container)) {
    New-Item -ItemType Directory -Path $pluginDir -Force | Out-Null
}
if ((Test-Path -LiteralPath $dstPlugin -PathType Leaf) -and
    ((Sha $dstPlugin) -eq (Sha $srcPlugin))) {
    Ok (Msg 'plugin_same' @())
} else {
    if (Test-Path -LiteralPath $dstPlugin -PathType Leaf) {
        Copy-Item -LiteralPath $dstPlugin -Destination "$dstPlugin.bak-$stamp" -Force
        Ok (Msg 'plugin_backup' @($stamp))
    }
    Copy-Item -LiteralPath $srcPlugin -Destination $dstPlugin -Force
    Ok (Msg 'plugin_installed' @($dstPlugin))
}

# ---- install Language\*.aul2 display packs -------------------------------
# These are pure data: no restart of the host binary is needed beyond the
# normal restart, and they never touch keys stored inside .aup2 projects.
if (-not (Test-Path -LiteralPath $srcLangDir -PathType Container)) {
    Warn (Msg 'langpack_none' @())
} else {
    $srcPacks = Get-ChildItem -LiteralPath $srcLangDir -Filter '*.ProxyLayer.aul2' -File -ErrorAction SilentlyContinue
    if (-not $srcPacks) {
        Warn (Msg 'langpack_none' @())
    } else {
        if (-not (Test-Path -LiteralPath $langDir -PathType Container)) {
            New-Item -ItemType Directory -Path $langDir -Force | Out-Null
            Ok (Msg 'langdir_created' @($langDir))
        }
        $installed = 0
        $skipped = 0
        foreach ($p in $srcPacks) {
            $dst = Join-Path $langDir $p.Name
            if ((Test-Path -LiteralPath $dst -PathType Leaf) -and
                ((Sha $dst) -eq (Sha $p.FullName))) {
                $skipped++
                continue
            }
            if (Test-Path -LiteralPath $dst -PathType Leaf) {
                Copy-Item -LiteralPath $dst -Destination "$dst.bak-$stamp" -Force
                Warn (Msg 'langpack_backup' @($p.Name, $stamp))
            }
            Copy-Item -LiteralPath $p.FullName -Destination $dst -Force
            $installed++
        }
        if ($installed -gt 0) { Ok (Msg 'langpacks_installed' @($installed, $langDir)) }
        if ($skipped -gt 0)   { Ok (Msg 'langpacks_same' @($skipped)) }
    }
}

# ---- install optional luaJIT proxy ---------------------------------------
if ($SkipLuaProxy) {
    Warn (Msg 'skip_luaproxy' @())
} elseif (-not (Test-Path -LiteralPath $proxySrc -PathType Leaf)) {
    Warn (Msg 'pkg_missing_proxy' @())
} elseif (-not (Test-Path -LiteralPath $origDll -PathType Leaf)) {
    Warn (Msg 'host_no_luajit' @($exeDir))
    Warn (Msg 'skip_proxy_note' @())
} else {
    $proxyHash = Sha $proxySrc
    if ((Sha $origDll) -eq $proxyHash) {
        Ok (Msg 'proxy_same' @())
    } else {
        # Windows lets us RENAME a loaded DLL even while the host is running.
        # Renaming first frees the luaJIT.dll path, so the copy never hits a
        # sharing violation. The host keeps its old in-memory image until restart.
        Copy-Item -LiteralPath $origDll -Destination "$origDll.bak-$stamp" -Force
        Ok (Msg 'dll_backup' @($stamp))
        if (-not (Test-Path -LiteralPath $realDll -PathType Leaf)) {
            Rename-Item -LiteralPath $origDll -NewName 'luaJIT_real.dll' -Force
            Ok (Msg 'dll_renamed' @())
        } else {
            # An AviUtl2 update usually overwrites luaJIT.dll with a newer
            # official build. Keeping the OLD luaJIT_real.dll would leave our
            # PE forwarders pointing at a different LuaJIT build, so refresh
            # the forwarding target from the CURRENT official DLL. The old
            # real DLL is backed up, never deleted.
            if ((Sha $realDll) -ne (Sha $origDll)) {
                Copy-Item -LiteralPath $realDll -Destination "$realDll.bak-$stamp" -Force
                Copy-Item -LiteralPath $origDll -Destination $realDll -Force
                Warn (Msg 'dll_refresh' @())
                Warn (Msg 'dll_refresh_warn' @())
            } else {
                Ok (Msg 'dll_reuse' @())
            }
        }
        Copy-Item -LiteralPath $proxySrc -Destination $origDll -Force
        Ok (Msg 'proxy_ready' @($origDll))
        Ok (Msg 'forwarders' @())
    }
}

# ---- summary --------------------------------------------------------------
$running = Get-Process aviutl2 -ErrorAction SilentlyContinue
if ($running) {
    Warn (Msg 'host_running' @())
} else {
    Ok (Msg 'install_done' @())
}
Write-Host ''
Write-Host ('  ' + (Msg 'next_step' @())) -ForegroundColor White
Write-Host ('  ' + (Msg 'lang_hint' @())) -ForegroundColor White
Write-Host ('  ' + (Msg 'menu_note' @())) -ForegroundColor DarkGray
Write-Host ('  ' + (Msg 'uninstall_hint' @())) -ForegroundColor White
Write-Host ''
if (-not $NoPause -and $Host.Name -eq 'ConsoleHost') {
    Write-Host (Msg 'press_key' @()) -ForegroundColor DarkGray
    try { $null = $Host.UI.RawUI.ReadKey('NoEcho,IncludeKeyDown') } catch { }
}
