<div align="center">

# ProxyLayer / プロキシレイヤー / 代理图层

**AviUtl ExEdit2 用「プレビュー用プロキシレイヤー」ネイティブプラグイン**  
スイッチを 1 つ入れるだけで、プロジェクト全体の大きな静止画像が低解像度プロキシに置き換わり、再生がすぐに軽くなります。**出力時は自動的に元画像へ戻る**ため、書き出した映像の画質は一切変わりません。変換後もタイムラインには元のファイル名が表示され、従来の「画像ファイル」と同じ感覚で扱えます。

`ProxyLayer.aux2 v0.2.1` &nbsp;|&nbsp; `AviUtl ExEdit2 v2.1.x` &nbsp;|&nbsp; `MIT License`

**🌐 言語 / Language:** &nbsp; **[简体中文 (Chinese)](../README.md)** &nbsp;|&nbsp; **日本語** &nbsp;|&nbsp; **[English](README_en.md)**

*(UI 表示パック 3 言語同梱：`Language/日本語.ProxyLayer.aul2` / `Language/简体中文.ProxyLayer.aul2` / `Language/English.ProxyLayer.aul2`)*

<sub>※ 本ドキュメントの日本語・英語部分は翻訳です。内容に差異がある場合は中国語版を正とします。</sub>

</div>

---

## 効果

| テスト | 元画像 | AUTO プロキシ |
| --- | --- | --- |
| 12 レイヤー 7000×8000 @ 1920×1080 | 約 1216 ms / フレーム | **約 19 ms / フレーム** |
| 実案件 21 シーン構成（8000×5000 の画像オブジェクト 114 個） | 再生がカクつく | 一括変換後は滑らかに再生 |

プロキシ倍率は `sqrt(元の画素数 / シーンの画素数)` から自動決定し、2 の冪へ切り上げ（上限 1/8）。8000×5000 の素材を 2048×1080 のシーンで使うと `div=8` となり、プロキシは 1000×625 —— 画面より小さくなります。

## 機能

- **プロジェクト全体の一括自動プロキシ**：現在のシーンだけでなく全シーンを走査します（AviUtl2 の `EDIT_SECTION` は現在のシーンしか指さないため）。
- **AUTO 倍率** + 手動 1/2 / 1/4 / 1/8 / 1/16 / 1/32。
- **出力ゲート**：保存／出力状態（`EDIT_STATE_SAVE`）を検出すると強制的に L0（元画像）へ戻し、ピクセル差分 0 で出力します。
- **レイヤー間サンプリング保護**：体積光・体積フォグ・3D ブリッジ等のスクリプトが他レイヤーの画素を読む場合、読まれる側のレイヤーは自動的に原寸を維持します（「光条が中央だけ明るい」現象を防ぐ）。
- **元画像を二重にデコードしない**：プロキシがヒットした後は 40 MPx の元画像をホストへ要求しません（実測：1 回のデコード 95.6 ms、ファストパス命中率 97%）。
- **タイムラインに元のファイル名を表示**：変換後も `ProxyLayer` の羅列にならず、OFF で戻すと標準名も復元されます。
- **元サイズのメタデータ**：下流スクリプトは `obj.getvalue("ProxyLayer","原始宽"/"原始高")` で補正できます。
- **厳格なホワイトリスト**：静的画像入力（`画像ファイル` 等）と画像拡張子のみ変換。音声・動画・OBJ・VRM・3D モデルは誤変換しません。
- **グローバルスイッチ + 設定ウィンドウ**：「確定」で保存、「キャンセル」で破棄。プロジェクトや再起動をまたいで保持されます。
- **3 言語 UI 表示パック**：`Language/*.ProxyLayer.aul2` が日本語 / 简体中文 / English のパラメータ名とヒントを提供。**DLL の再ビルド不要**、プロジェクト内に保存されるキー名も変わりません。
- **オプションの実験コンポーネント**：同名 `luaJIT.dll` 転送プロキシ（`obj.w/obj.h` を元サイズへ写像。`src/ProxyLuaJIT.cpp`、既定では未インストール）。

## インストール

### Release からワンクリック導入（推奨）

Release ごとに 2 種類のパッケージがあります。必要に応じてどちらかを選んでください。

| パッケージ | 内容 | 対象 |
| --- | --- | --- |
| `ProxyLayer-<tag>.au2pkg.zip` | `Plugin/ProxyLayer.aux2` + `Language/*.aul2`（3 言語）を同梱。AviUtl2 のウィンドウへドラッグ & ドロップで導入 | ほとんどの方。**`aviutl2.exe` と同じフォルダには一切触れません** |
| `ProxyLayer-<tag>-Installer.zip` | 本体 + 3 言語表示パック + オプションの `luaJIT.dll` 転送プロキシ + `install.bat`（日本語 / 中文 / English 表示） | `obj.w/obj.h` のサイズ補正が必要な方 |

**Installer パッケージの手順**（「元の DLL をリネームして自前 DLL を置く」処理）：

1. zip を丸ごと展開 → `install.bat` をダブルクリック。
2. スクリプトは OS の表示言語（日本語 / 簡体字中国語 / それ以外 → English）を自動判定してメッセージを切り替えます。`-Lang ja|zh|en` で強制指定も可能。
3. `aviutl2.exe` とデータフォルダを自動検出し、本体プラグインと `Language\` 以下の表示パックを配置。
4. 既存の `luaJIT.dll` を `luaJIT.dll.bak-<タイムスタンプ>` にバックアップ → `luaJIT_real.dll` に改名 → 同梱の転送プロキシを `luaJIT.dll` としてコピー。プロキシ内の PE forwarder が 322 個の LuaJIT エクスポートを `luaJIT_real.dll` へ解決します。
5. AviUtl2 を再起動すると有効になります。`uninstall.bat` で完全に元へ戻せます（言語表示パックも削除せず退避）。

`install.ps1` は `-ExePath "D:/AviUtl2/aviutl2.exe"`、`-SkipLuaProxy`、`-Uninstall`、`-Lang` に対応。冪等なので再実行しても二重に改名・バックアップしません。ホスト側に `luaJIT.dll` が無い場合は転送プロキシの導入だけをスキップし、本体は通常どおり動作します。

### 手動インストール

1. `dist/ProxyLayer.aux2` を用意（または下記の手順でビルド）。
2. AviUtl2 のデータフォルダの `Plugin\` へ配置：
   - 通常インストール：`C:/ProgramData/aviutl2/Plugin/`
   - ポータブル：`<aviutl2.exe と同じフォルダ>\data\Plugin\`
3. UI を日本語／中国語／英語表示にしたい場合は `Language/*.ProxyLayer.aul2` をデータフォルダの `Language\` へ置き、「設定」→「言語の設定」で選択します（ホスト UI 全体の言語も切り替わります）。
4. AviUtl2 を再起動し、ホストがプラグインを信頼したこと（`module.ini`）を確認。
5. 「編集」メニュー → `代理图层` → `整个工程自动代理 (开/关)`、または「設定」→ `ProxyLayer 自动代理` で設定ウィンドウを開く。
6. 10〜20 秒ほど再生してから滑らかさを判断してください。初回再生時は大きな画像ごとにプロキシキャッシュを構築するため、最初の数秒はむしろ重くなります。

> ⚠️ 本プラグインは `aviutl2.exe` と同じフォルダに `data` フォルダを**絶対に作りません**。AviUtl2 はこのフォルダを検出するとポータブルモードへ切り替えて空の設定を読むため、言語・プラグイン・レイアウトがすべて「初期化」されます。開発初期に踏んだ罠で、該当するフォルダ生成処理はすべて削除済みです。

プレビュー時の画質低下は仕様です。出力は常に元画像を使います。

## UI の言語（ローカライズ）

プラグイン本体のパラメータ名はコンパイル時に埋め込まれており（日本語と中国語が混在：`画像ファイル` と `档位` が並ぶ）、AviUtl2 公式の `.aul2` 言語機構で**表示名だけ**を差し替えられます。

| ファイル | 効果 |
| --- | --- |
| `Language/日本語.ProxyLayer.aul2` | 中国語のパラメータ名（档位 / 导出时强制原图 / …）をすべて日本語表示に。エフェクト名は「プロキシレイヤー」 |
| `Language/简体中文.ProxyLayer.aul2` | 残った日本語項目（`モード` / `画像ファイル`）を中国語表示に統一 |
| `Language/English.ProxyLayer.aul2` | 全パラメータ名を英語表示に |

設計上の注意（言語パックを編集する前に必ず読んでください）：

- 言語パックは**表示テキストのみ**を置換します。内部キー名（`画像ファイル`、`档位`、`原始宽`…）と `.aup2` に保存される内容は変わらないため、**既存プロジェクトはそのまま動きます**。
- セクション名はプラグインのファイル名 `[ProxyLayer]`、キーはソース内の元の文字列と完全一致させる必要があります。
- **プラグイン名 `ProxyLayer` 自体は翻訳しないでください**：プラグインは `wcscmp(name, L"ProxyLayer")` で自分のエフェクトを識別しており、名前を変えるとプロジェクト全体の変換・復元が動かなくなります。表示ラベルの `代理图层` は翻訳可能です。
- ホスト側のメニュー項目（`代理图层\整个工程自动代理`）と設定ウィンドウの文言は C++ が登録時に書き込むため `.aul2` では変えられません。対応方針（`InitializeConfig` + `CONFIG_HANDLE::translate` + `RELOAD_LANGUAGE` イベント）は [`LOCALIZATION.md`](LOCALIZATION.md) に記載していますが、反映には DLL の再ビルドが必要です。

語条一覧と「新しい言語を追加する」手順は [`LOCALIZATION.md`](LOCALIZATION.md) を参照してください。

## パラメータ

| 項目 | 説明 |
| --- | --- |
| `画像ファイル` | 元画像のパス（変換時に自動設定） |
| `モード` | 3 = 本番モード（`set_image_data` + 拡大補正。下流のエフェクトチェーンとカメラを維持） |
| `档位` | AUTO / 1/2 / 1/4 / 1/8 / 1/16 / 1/32 |
| `导出时强制原图` | 既定でオン。出力ゲート |
| `本图层保持原图` | レイヤー単位のエスケープスイッチ。他レイヤーからサンプリングされる際に自動で 1 になります |
| `原始宽` / `原始高` / `原始效果名` / `原始文件名项` | 変換時に書き込まれるメタデータ（復元と下流補正用） |

> `补偿 obj.w/h (実験)` は **v0.2.1 で削除**しました。ネイティブの `OBJECT_INFO::width/height` へ書き戻す試みは Lua プローブで反証済み（ホストは現在のテクスチャサイズを Lua に渡すため 2048 → 1024 のまま）で、単なるデッドコードでした。サイズ補正の正しい手段は**オプションの `luaJIT.dll` 転送プロキシ**（`obj.w/h` を元サイズへ写像。OpenGlow や 光束_H などサイズ依存スクリプトが依存するため必ず残す）か、スクリプト側で `obj.getvalue("ProxyLayer","原始宽"/"原始高")` を読む方法です。

## 動作の仕組み（概要）

`ProxyLayer` は `FLAG_INPUT` のメディアオブジェクトエフェクトで、元の `画像ファイル` を置き換えます。

1. 元画像を読み、`div` でダウンサンプリングして 1/div のプロキシテクスチャを作り、メモリ + ディスク PNG（`image:` として VRAM 常駐）にキャッシュ。
2. `mode 3` で `set_image_data(1/div)` を呼び、`param.sx/sy *= div` でオブジェクトの四角形は元サイズのまま描画 —— ジオメトリ、3D カメラ、Z 順、下流のエフェクトチェーンがすべて保たれます。
3. `EDIT_SECTION` は現在のシーンしか対象にならないため、`enum_scene_name()` + `select_scene()` でシーンごとに処理し、最後にユーザーが元いたシーンへ戻します。
4. `select_scene()` は参照ロック／編集ロック中に呼べないため、走査全体を UI スレッドの隠しウィンドウ `WM_TIMER` で実行し、メニューコールバックはメッセージを投げるだけにしています。
5. 出力／保存状態では強制的に L0 とし、元画像で描画。

実測値とハマりどころは [`TECHNICAL.md`](TECHNICAL.md)、保守目線の評価と改善提案は [`REVIEW.md`](REVIEW.md)（中国語）を参照してください。

## 既知の制限

- AviUtl2 SDK ではプロキシテクスチャ使用時に `obj.w/obj.h` を不変にできません（Lua 側実測 2048 → 1024）。**オプションの `luaJIT.dll` 転送プロキシなら元サイズへ写像できます**（A/B 実測 1024 → 2048）。OpenGlow / 光束_H のように `obj.w/h` から作用範囲を計算するスクリプトはこれに依存するため省略できません。スクリプト側で `原始宽/原始高` を読む方法もあります。転送プロキシは `getpixeldata()` のような**バッファサイズ**読み取りには無効で、その場合はレイヤー間サンプリング保護で対応します。
- 他レイヤーからサンプリングされるレイヤーは原寸維持のため高速化の対象外です。
- 静的画像のみ対象です。プレビューは元画像よりぼやけますが、出力には影響しません。
- 初回再生時にはキャッシュ構築時間がかかります。
- プラグインのメニュー項目と設定ウィンドウの文言はコンパイル時文字列（日本語／中国語混在）で、`.aul2` が対象とするのはエフェクト名とパラメータ名のみです。完全なローカライズには再ビルドが必要です。
- 一部のサードパーティ製 Rust/Egui プラグインはホスト終了時に panic しますが、本プラグインとは無関係です。

## ビルド

Windows + Visual Studio 2022 BuildTools（MSVC x64）+ Windows SDK 10 + Git Bash が必要です。

**公式 AviUtl2 SDK のヘッダーは本リポジトリに含まれません**（著作権は作者 ＫＥＮくんに帰属）。公式配布物から以下を取得し `include/` へ配置してください。

```
filter2.h  cache2.h  logger2.h  plugin2.h  input2.h  output2.h  config2.h  module2.h
```

その後：

```bash
bash build_msvc.sh
```

スクリプトは既定で `14.39.33519` / `10.0.22621.0` を使用します。バージョンが違う場合は上書きしてください：

```bash
VS_BASE="D:/VS2022" MSVC_VER=14.42.34433 SDKV=10.0.26100.0 bash build_msvc.sh
```

出力は `dist/ProxyLayer.aux2`。`build_luaproxy.sh` はオプションの LuaJIT 転送プロキシ DLL、`build_exportprobe.sh` は出力テスト用プローブをビルドします（いずれも既定のパッケージには含まれません）。

> 言語パック／ドキュメント／パッケージスクリプトだけの変更なら**再ビルド不要**です。`.aul2` は純粋なデータで、ホストが起動時と切替時に読み込みます。

## リリース（ワンクリック）

ワークフローは `.github/workflows/release.yml`。**`v*` タグを 1 つ push すれば完了**です。

```bash
# 1. ローカルでビルドし、新しい dist/ProxyLayer.aux2 をコミット
git add dist/ && git commit -m "build: refresh binaries for v0.2.1"
# 2. タグを push すると CI が Release 作成 + パッケージ + アップロードを行う
git tag v0.2.1 && git push origin main && git push origin v0.2.1
```

CI は該当タグを `checkout` し、`packaging/build_release.py --tag <tag>` で 2 つの zip（`Plugin/` と `Language/` を含む）を生成、`gh release create`（未作成なら）と `gh release upload --clobber` で公開し、workflow summary に SHA-256 を出力します。Web で先に Release を作ってから起動する運用や、Actions からの `workflow_dispatch` 再実行にも対応しています。

> ワークフローは DLL を**ビルドしません**。公式 SDK ヘッダーが本リポジトリに無いため、コミット済みの `dist/` バイナリをそのまま使います。リリース前に更新を忘れずに。

ローカルでパッケージ内容を確認：

```bash
python3 packaging/build_release.py --tag v0.2.1 --out-dir dist
```

## 更新と互換性（DLL の再ビルドは必要か）

まず「どの種類の更新か」を切り分けてください。

| 変更内容 | `ProxyLayer.aux2` | `luaJIT.dll` 転送プロキシ |
| --- | --- | --- |
| ドキュメント／言語パック／インストールスクリプト／Release ワークフローのみ | **再ビルド不要**。`dist/` のバイナリをそのまま使い、再パッケージすれば言語パックが zip に入ります | 不要 |
| `src/*.cpp` を変更（新版として公開） | **再ビルド必須**。新しい `dist/ProxyLayer.aux2` もコミットしてください。CI はビルドせずコミット済みバイナリを固めるだけなので、忘れると Release に旧 DLL が入ります | 通常不要。ホストの LuaJIT エクスポート表が変わった場合のみ `tools/gen_luaproxy_def.py` + `build_luaproxy.sh` で再生成 |
| AviUtl2 本体の更新 | 概ね**再ビルド不要**。ただし SDK 構造体に非互換変更があれば新しいヘッダーで再ビルド必須 | **高リスク**。下記参照 |

### AviUtl2 本体の更新時

本体プラグインは公式 SDK のみを使用します：`GetFilterPluginTable()` / `InitializePlugin(DWORD version)` / `RegisterPlugin(HOST_APP_TABLE*)`。関数ポインタはホストが渡す `HOST_APP_TABLE` から取得し、構造体は `sizeof()` 付きで渡します（例 `get_edit_info(&info, sizeof(info))`）。**`aviutl2.exe` のアドレスやバージョン番号のハードコードはありません**。`InitializePlugin` は version をログに残すだけで検証はしません。したがって AviUtl2 のマイナー更新後も DLL はそのまま使えるのが通常です。

ただし構造体のフィールドオフセットは sdk-2.1.16 のヘッダーでコンパイル時に固定されています。`FILTER_PROC_VIDEO` / `EDIT_SECTION` / `FILTER_PLUGIN_TABLE` のフィールド順が変わったり挿入されたりすると、旧 DLL は誤ったオフセットを読みます。その場合は新しい SDK ヘッダーでの再ビルドが必要です。更新後はホストログの `InitializePlugin host=0x...` を確認し、公式の更新履歴にプラグイン API の変更が無いかを見てください。読み込み失敗やクラッシュが出たら、まず `ProxyLayer.aux2` を退避して切り分けます。

**`luaJIT.dll` 転送プロキシだけはホストのバージョンに強く依存します**：322 個の PE forwarder は当時の `luaJIT.dll` のエクスポート表から生成されています。AviUtl2 の更新で LuaJIT のビルドが変わると、ホストフォルダの `luaJIT.dll` は公式版で上書きされます。

- `install.ps1` は `luaJIT.dll` と `luaJIT_real.dll` の不一致を検出し、**現在の公式 DLL で転送先を更新**します（旧 real は `.bak-<タイムスタンプ>` に退避）。新旧 ABI の混在を避けるためです。アンインストール時も正しい公式 DLL へ戻します。
- ただし旧プロキシのエクスポート名集合が新しい LuaJIT と一致するとは限りません。安全な手順は「ホスト更新 → `uninstall.bat` で復元 → AviUtl2 の正常起動を確認 → 新しいホストに対応する Release を導入」です。ログに `GetProcAddress` のシンボル不足が出る場合は、新しい `luaJIT.dll` から `.def` を再生成してプロキシを再ビルドしてください。
- `.au2pkg.zip`（本体のみ）を入れただけの利用者はこの影響を受けません。exe と同じフォルダには一切触れません。

## ディレクトリ構成

```
src/ProxyLayer.cpp      本体プラグイン
src/ProxyLuaJIT.cpp     オプション：同名 luaJIT.dll 転送プロキシ（実験）
src/png_writer.h        依存ゼロの RGBA8 PNG エンコーダ
src/ExportProbe.cpp     テスト専用：公式 output_file() で実出力を起動
Language/*.aul2         3 言語 UI 表示パック（日本語 / 简体中文 / English）
build_*.sh              ビルドスクリプト
tools/gen_luaproxy_def.py  luaJIT.dll のエクスポート表から 322 個の PE forwarder を生成
tools/dump_stack.py     標準ライブラリのみの minidump 解析ツール（例外／スタック／モジュール）
tools/read_perf.py      ProxyLayer-perf.log のテレメトリ集計
dist/ProxyLayer.aux2    ビルド済みバイナリ
docs/TECHNICAL.md       実装詳細と実測データ（中国語）
docs/LOCALIZATION.md    言語パックの語条一覧と新言語追加手順（中国語）
docs/REVIEW.md          リポジトリ評価と改善提案（中国語）
docs/README_ja.md       本ファイル
docs/README_en.md       README English version
```

## 謝辞

- **天国の HitFilm** —— 「スイッチを入れれば自動で軽くなる」という目標は、最初にあちらから持ち帰ったものです。別の次元でもリアルタイムプレビューを続けられますように。
- **WorkBuddy の AI 相棒（Tencent Hunyuan Hy）** —— 4 本の minidump を読み切り、`find_object` の本当の意味を突き止め、21 シーンを 1 つずつ踏破し、ついでにこの README とパッケージスクリプト、CI まで書いてくれました。
- **Tencent の無料計算資源の多大な支援**（笑）—— このトークンが無ければ、上の「もう一度試す」「もう一度照合する」は起きませんでした。

そして AviUtl2 SDK の作者 ＫＥＮくん、スクリプトを書き、不具合を報告し、実機で辛抱強く検証してくださるすべての方々へ。参考リポジトリ [AviUtl2_VolumetricLight3D](https://github.com/CylnaNangong/AviUtl2_VolumetricLight3D) には `.au2pkg.zip` パッケージング、Release ワークフロー、多言語化（`.aul2` 言語パック + 多言語 README）の範を示していただきました。

## ライセンス

MIT。[LICENSE](../LICENSE) を参照。AviUtl2 SDK のヘッダーは元のライセンスに従い、本リポジトリでは配布しません。
