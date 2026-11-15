# 本地化（`.aul2`）设计与词条清单

> **v0.3 起内部键名统一为日语**（AviUtl2 默认语言 `Default`）。本文件描述当前机制；v0.2.1 时期"三语包 + 中/日混排键"的评估存档见 [`REVIEW.md`](REVIEW.md)。

## 1. 为什么内部键是日语

AviUtl2 的默认语言是 `Default`（日语），社区惯例是插件把 `FILTER_ITEM_*` 名字写成日语，其他语言再用 `.aul2` 差异包把**显示名**翻过去。这样：

- 默认日语用户**不需要任何语言包**，直接看到日语参数名；
- 中文 / 英文用户各装一个显示包；
- `.aup2` 里保存的键名永远是同一套日语键，不会再出现"中文键翻成日文"这种反向补丁。

插件名、菜单项与自绘设置窗口文案统一用英语 `ProxyLayer`——`.aul2` 本来就覆盖不到 `register_*_menu()` 与 `PanelWndProc`，用英语可保证任何语言的用户都读得懂。

## 2. 宿主的语言文件规则（实测）

宿主自带 `简体中文.aul2` 的头部注释写明：

- 文件名多句点 = **差异文件**，如 `English.xxx.aul2` 会并入 `English` 设置；
- **`Default` 语言只读取 `Default.xxx.aul2` 差异文件**；
- 存在以滤镜名 / 脚本名命名的 section 时优先引用该 section（我们用 `[ProxyLayer]`）。

真机 `Language\` 目录前缀统计：`English(30)` / `简体中文(19)` / `Default(15)` / `Deutsch(3)` / `community_*`，**没有「日本語」**。所以早期那个 `日本語.ProxyLayer.aul2` 永远不会加载。

隔离沙箱三轮实测（`aviutl2.ini` 控制 `Language`）：

| 轮 | 配置 | 结果 |
| --- | --- | --- |
| A | 只放 `日本語.ProxyLayer.aul2`，`Language=`（Default） | 参数仍显示中文，**完全没生效** |
| B | 同一文件改名 `Default.ProxyLayer.aul2` | `档位→プロキシ倍率`、`导出时强制原图→出力時は元画像を強制` 等**全部生效** |
| C | 三包并存、`Language=简体中文` | 中文包生效，**未被 Default 日文包污染** |

结论：内容与 section 都对，唯一问题是文件名前缀。最终方案不是补 `Default` 包，而是**把内部键直接改成日语**，日文包整个删掉。

## 3. 当前文件

| 文件 | 作用 |
| --- | --- |
| `Language/简体中文.ProxyLayer.aul2` | 日语键 → 简体中文显示 |
| `Language/English.ProxyLayer.aul2` | 日语键 → 英文显示 |

两个包都打成 `Language/` 放进 `.au2pkg.zip` 与 `-Installer.zip`；安装器复制到 `<数据目录>\Language\`，卸载时改名 `.removed-<时间戳>` 保留（不删除）。

格式：UTF-8 **无 BOM** + **CRLF**，section 固定为 `[ProxyLayer]`（等于插件主名）。`[Language]` 段的 `SubLanguage` 必须等于宿主语言设置值。

## 4. 词条清单（12 键，必须与源码逐字一致）

`tools/check_aul2.py` 从 `src/ProxyLayer.cpp` 解析 `FILTER_ITEM_*` 与 `FILTER_PLUGIN_TABLE`，只校验含非 ASCII 的键。ASCII 的 `ProxyLayer`（label）、英语 separator 提示、`AUTO`（零值提示）都不需要翻译。

| # | 键（源码 / 日语） | 简体中文 | English |
| --- | --- | --- | --- |
| 1 | `画像ファイル` | 图像文件 | Image File |
| 2 | `プロキシ倍率` | 代理档位 | Proxy scale |
| 3 | `出力時は元画像を強制` | 导出时强制原图 | Force original on export |
| 4 | `プローブログを出力` | 输出探针日志 | Write probe log |
| 5 | `プロキシマーカーを表示` | 显示代理标记 | Show proxy marker |
| 6 | `出力状態をシミュレート` | 模拟导出态 | Simulate export state |
| 7 | `このレイヤーは元画像を維持` | 本图层保持原图 | Keep this layer full-res |
| 8 | `元の幅` | 原始宽 | Original width |
| 9 | `元の高さ` | 原始高 | Original height |
| 10 | `元のエフェクト名` | 原始效果名 | Original effect name |
| 11 | `元のファイル項目名` | 原始文件名项 | Original file item name |
| 12 | `未設定` | 未设置 | Not set |

## 5. 硬规则

1. **绝不翻译内部名 `ProxyLayer`**：`create_effect()` / `get_effect_name()` 与 9 处 `wcscmp(..., L"ProxyLayer")` 依赖它，改了会让全工程转换、OFF 还原、跨图层保护、`EnsureDisplayName` 一起失效。显示标签本来就是英语，无需翻译。
2. **键必须逐字一致**（含全角字符与空格）。差一个字符会静默回退原文，不报错 —— 所以 CI 里跑 `check_aul2.py`。
3. **不要用 `日本語.` 前缀**。`Default` 才是日语；要用差异包也必须叫 `Default.ProxyLayer.aul2`（当前不需要）。
4. `.aul2` 只改显示，不改 `.aup2` 存储键；任何改内部键名的操作都是**破坏性变更**，需要重新编译并写进 CHANGELOG。

## 6. 新增一门语言

1. 复制 `Language/English.ProxyLayer.aul2` → `Language/<语言名>.ProxyLayer.aul2`，`<语言名>` 必须与宿主语言设置里显示的值一致（`Deutsch` / `简体中文` / `community_xx` …）。
2. 改 `[Language]` 段的 `Information` 与 `SubLanguage`。
3. 翻译 `[ProxyLayer]` 段的 12 个**值**，键不动。保持 UTF-8 无 BOM + CRLF。
4. 跑 `python3 tools/check_aul2.py -v`，应显示 `12/12 translated, 0 failure(s)`。
5. 提交即可：`packaging/build_release.py` 用 glob 自动把 `Language/*.ProxyLayer.aul2` 打进两个 zip；CI 会校验 zip 内确实有这些文件。

## 7. 为什么菜单 / 设置窗口不做代码级 i18n

AviUtl2 的 `.aul2` 只覆盖效果名与参数名。菜单项（`register_edit_menu`）与自绘设置窗口（`PanelWndProc`）是运行期 C++ 字符串，宿主看不到键。SDK 提供 `CONFIG_HANDLE::translate()` / `get_language_text()` + `EVENT_TYPE::RELOAD_LANGUAGE`（v2.1.17 起）可做代码级 i18n，但那需要重编 DLL、维护三套字符串表，收益有限。**本项目选择把这些文案统一写成英语 `ProxyLayer`**，简洁且对所有语言可读。
