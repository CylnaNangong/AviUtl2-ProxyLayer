[ ProxyLayer インストールパッケージ / 一键安装包 / Installer ]

【日本語】

■ このパッケージの中身
  install.bat                    ダブルクリックでインストール（推奨）
  uninstall.bat                  アンインストール / 復元
  install.ps1                    実処理（パラメータは下記）
  package.txt                    パッケージ情報（日 / 中 / 英）
  Plugin/ProxyLayer.aux2         本体プラグイン
  Language/日本語.ProxyLayer.aul2      UI 表示パック（日本語）
  Language/简体中文.ProxyLayer.aul2    UI 表示パック（簡体字中国語）
  Language/English.ProxyLayer.aul2     UI 表示パック（英語）
  LuaProxy/luaJIT.dll            オプションの luaJIT 同名転送プロキシ（実験的）

■ 使い方
  1. zip をフォルダへ丸ごと展開します（1 ファイルだけ取り出さないでください）。
  2. install.bat をダブルクリックします。
  3. [OK] が表示されたら AviUtl2 を再起動します。
  4. 「編集」メニュー →「代理图层」→「整个工程自动代理 (开/关)」でスイッチを入れます。
  5. 日本語 UI にしたい場合は「設定」→「言語の設定」で 日本語 を選びます。

■ スクリプトが自動で行うこと
  - aviutl2.exe の自動検出：実行中のホスト → 近くのフォルダ → 一般的なインストール先
    → 各ドライブの浅い走査 → それでも見つからなければ手動入力を求めます。
  - データフォルダの判定：exe と同じ場所に data\ があればポータブルモード、
    なければ %ProgramData%\aviutl2 を使用します。exe の隣に data\ を新規作成することは
    絶対にありません（AviUtl2 が設定を「初期化」してしまうため）。
  - ProxyLayer.aux2 を データフォルダ\Plugin\ へ配置（同名の旧ファイルは先にバックアップ）。
  - Language\*.ProxyLayer.aul2 を データフォルダ\Language\ へ配置（既存ファイルはバックアップ）。
  - オプションの luaJIT 転送プロキシ（obj.w/obj.h を元サイズへ写像）：
      * 既存の luaJIT.dll を luaJIT.dll.bak-<タイムスタンプ> にバックアップ
      * それを luaJIT_real.dll に改名（プロキシ DLL の転送先）
      * 本パッケージのプロキシ DLL を luaJIT.dll としてコピー
    ホスト側に luaJIT.dll が無い場合はこの手順だけをスキップし、本体は通常どおり動きます。
  - 冪等：再実行しても二重に改名・バックアップしません。

■ パラメータ
  powershell -ExecutionPolicy Bypass -File install.ps1 -ExePath "D:\AviUtl2\aviutl2.exe"
      ホストのパスを手動指定。
  ... -SkipLuaProxy
      本体と言語パックだけを導入し、luaJIT.dll には一切触れません。
  ... -Lang ja | zh | en
      メッセージ言語を強制指定（既定は OS の表示言語から自動判定）。
  ... -Uninstall
      復元：プロキシの luaJIT.dll を退避し、luaJIT_real.dll を luaJIT.dll に戻し、
      本体と言語表示パックを .removed-<タイムスタンプ> として退避します。
      スクリプトはファイルを削除しません（すべて手動で戻せます）。
  ... -NoPause
      キー入力待ちをしません（自動化向け）。

■ 注意
  - AviUtl2 の実行中でもファイルは置換できますが、新しい DLL の読み込みには再起動が必要です。
  - 上書きされたファイルはすべて .bak-<タイムスタンプ> として残ります。
  - プレビュー画質が下がるのは仕様です。出力は常に元画像を使います。
  - 初回再生はプロキシキャッシュ構築のため数秒遅くなります。
  - メニュー項目と設定ウィンドウの文言はプラグイン内部の文字列（中国語）で、
    言語パック（.aul2）が対象とするのは効果名とパラメータ名のみです。

■ もう 1 つのパッケージ
  同じタグの ProxyLayer-<tag>.au2pkg.zip は AviUtl2 へドラッグ & ドロップするだけで導入でき、
  本体と表示パックのみを含みます（exe と同じフォルダには触れません）。
  luaJIT.dll を触りたくない方はこちらを使ってください。

【简体中文】

■ 本包内容
  install.bat                    双击即可一键安装（推荐）
  uninstall.bat                  一键卸载 / 还原
  install.ps1                    实际安装逻辑（参数见下）
  package.txt                    包信息（日 / 中 / 英）
  Plugin/ProxyLayer.aux2         主插件
  Language/日本語.ProxyLayer.aul2       界面显示包（日语）
  Language/简体中文.ProxyLayer.aul2     界面显示包（简体中文）
  Language/English.ProxyLayer.aul2      界面显示包（英语）
  LuaProxy/luaJIT.dll            可选的 luaJIT 同名转发代理（实验组件）

■ 怎么用
  1. 把整个 zip 解压到任意文件夹（不要只解压其中某个文件）。
  2. 双击 install.bat。
  3. 看到 [OK] 提示后，重启 AviUtl2。
  4. 编辑菜单 ->「代理图层」->「整个工程自动代理 (开/关)」打开开关。
  5. 想用中文界面：设置 -> 语言设置 -> 简体中文。

■ 脚本会自动做什么
  - 自动定位 aviutl2.exe：优先用正在运行的宿主，其次常见安装位置，再浅层扫描各磁盘，
    找不到会提示手动输入路径。
  - 自动判断数据目录：若 exe 同目录已有 data/ 则是便携模式，否则用 %ProgramData%\aviutl2。
    绝不会新建 exe 同目录的 data/（那会让 AviUtl2 误切换便携模式，配置全部「恢复初始化」）。
  - 把 ProxyLayer.aux2 装到 数据目录\Plugin\，同名旧文件先备份。
  - 把 Language\*.ProxyLayer.aul2 复制到 数据目录\Language\（已有文件先备份）。
  - luaJIT 转发代理（可选，用于把 obj.w/obj.h 映射回原始尺寸）：
      * 先把原来的 luaJIT.dll 备份为 luaJIT.dll.bak-<时间戳>
      * 再把它改名为 luaJIT_real.dll（代理 DLL 的转发目标）
      * 最后把本包的代理 DLL 复制成 luaJIT.dll
    如果宿主目录本来没有 luaJIT.dll，会跳过这步，主插件照常可用。
  - 幂等：重复运行不会重复改名，也不会重复备份。

■ 参数
  powershell -ExecutionPolicy Bypass -File install.ps1 -ExePath "D:\AviUtl2\aviutl2.exe"
      手动指定宿主路径。
  ... -SkipLuaProxy
      只装主插件与语言包，完全不碰 luaJIT.dll。
  ... -Lang ja | zh | en
      强制指定提示语言（默认按系统显示语言自动判断）。
  ... -Uninstall
      还原：把代理 luaJIT.dll 移开，把 luaJIT_real.dll 改回 luaJIT.dll，
      并把主插件与语言显示包改为 .removed-<时间戳> 保留。脚本不删除任何文件。
  ... -NoPause
      不等待按键（自动化用）。

■ 注意
  - 安装时如果 AviUtl2 正在运行，文件仍可替换，但必须重启宿主才会加载新 DLL。
  - 所有被覆盖的文件都会留下 .bak-<时间戳> 备份。
  - 预览画质下降是设计使然，导出始终使用原图。
  - 首次播放需要构建代理缓存，开头几秒偏慢。
  - 菜单项与设置窗口文本是插件内部字符串（中文）；.aul2 语言包只覆盖效果名与参数名。

■ 另一个包
  同 tag 的 ProxyLayer-<tag>.au2pkg.zip 是拖入式安装包，只含主插件与显示包，
  不修改 exe 同目录任何文件。

【English】

■ What is in this package
  install.bat                    double-click to install (recommended)
  uninstall.bat                  uninstall / restore
  install.ps1                    the actual logic (parameters below)
  package.txt                    package information (ja / zh / en)
  Plugin/ProxyLayer.aux2         main plugin
  Language/*.ProxyLayer.aul2     UI display packs (Japanese, Simplified Chinese, English)
  LuaProxy/luaJIT.dll            optional same-name luaJIT forwarding proxy (experimental)

■ How to install
  1. Extract the WHOLE zip into any folder (do not pull out single files).
  2. Double-click install.bat.
  3. When you see [OK], restart AviUtl2.
  4. Switch it on under Edit menu -> 代理图层 -> 整个工程自动代理 (开/关).
  5. For an English UI: Setup -> Language -> English.

■ What the script does
  - Finds aviutl2.exe: a running host first, then nearby folders, common install
    locations, a shallow drive scan, and finally asks you for the path.
  - Chooses the data folder: an existing data\ next to the exe means portable mode,
    otherwise %ProgramData%\aviutl2. It NEVER creates data\ next to the exe (that would
    switch AviUtl2 into portable mode and silently reset your configuration).
  - Installs ProxyLayer.aux2 into <data>\Plugin\ (backing up an existing file first).
  - Copies Language\*.ProxyLayer.aul2 into <data>\Language\ (backing up existing files).
  - Optional luaJIT forwarding proxy (maps obj.w/obj.h back to the original size):
      * backs up the current luaJIT.dll as luaJIT.dll.bak-<timestamp>
      * renames it to luaJIT_real.dll (the forwarding target)
      * copies the bundled proxy DLL in as luaJIT.dll
    If the host folder has no luaJIT.dll this step is skipped; the main plugin still works.
  - Idempotent: re-running never renames or backs up twice.

■ Parameters
  powershell -ExecutionPolicy Bypass -File install.ps1 -ExePath "D:\AviUtl2\aviutl2.exe"
      Point at the host explicitly.
  ... -SkipLuaProxy
      Install only the plugin and display packs; never touch luaJIT.dll.
  ... -Lang ja | zh | en
      Force the message language (default: detected from the OS display language).
  ... -Uninstall
      Restore: move the proxy luaJIT.dll aside, rename luaJIT_real.dll back to
      luaJIT.dll, and move the plugin + display packs aside as .removed-<timestamp>.
      Nothing is ever deleted.
  ... -NoPause
      Do not wait for a key press (automation).

■ Notes
  - Files can be replaced while AviUtl2 is running, but the host must be restarted to
    load the new DLL.
  - Every overwritten file keeps a .bak-<timestamp> copy.
  - Softer preview quality is by design; exports always use the original image.
  - The first playback builds the proxy cache and is slower for a few seconds.
  - The menu entries and settings-window text are compiled strings (Chinese); the .aul2
    language packs only cover the effect name and parameter names.

■ The other package
  ProxyLayer-<tag>.au2pkg.zip with the same tag is a drag-and-drop package containing
  the plugin and the display packs; it never touches the folder next to the exe.
