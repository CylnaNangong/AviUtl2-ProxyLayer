# 本地化（多语言）设计与词条清单

本文说明 `Language/*.aul2` 是怎么做的、哪些词条可以改、哪些绝对不行，以及「再加一门语言」要改哪几个地方。
实现侧的踩坑记录见 [`TECHNICAL.md`](TECHNICAL.md)，仓库整体评估见 [`REVIEW.md`](REVIEW.md)。

## 1. 为什么是 `.aul2`，而不是改 C++ 字符串

`ProxyLayer` 是原生插件，参数名写在 `src/ProxyLayer.cpp` 的 `FILTER_ITEM_*` 里，编译进 DLL。但 AviUtl2 自带一套官方语言机制：宿主界面使用哪种语言，是「设定 → 语言设定」里选的 `.aul2` 决定的；**插件注册的效果名与参数名会按该文件里同名 section 的键值替换显示文本**，而 `.aup2` 工程里保存的仍是原始键名。

由此得到本项目本地化的两条铁律：

1. `.aul2` 只改**显示**，不改数据 → 旧工程零风险，也不需要重新编译 DLL（`dist/` 里的二进制可以原样复用）。
2. 宿主**不会**翻译插件自己 `register_*_menu()` 注册的菜单项、以及自绘设置窗口里的文字（那些是运行期 C++ 字符串，宿主看不到键）。这部分必须走代码级 i18n，见第 6 节。

官方 SDK 也提供了取词条的接口，供插件自己做运行期翻译（`config2.h`）：

```c
LPCWSTR (*translate)(CONFIG_HANDLE* handle, LPCWSTR text);                  // section = 插件文件名
LPCWSTR (*get_language_text)(CONFIG_HANDLE*, LPCWSTR section, LPCWSTR text); // 任意 section
```

## 2. 文件放置与命名

| 场景 | 位置 |
| --- | --- |
| 手动安装 | `<数据目录>\Language\`（普通安装即 `C:\ProgramData\aviutl2\Language\`；便携模式为 `<exe 目录>\data\Language\`） |
| `.au2pkg.zip` 安装 | 包内 `Language/*.aul2`，宿主拖入时自动展开到数据目录 |
| `-Installer.zip` | `install.bat` → `install.ps1` 复制到 `<数据目录>\Language\`；卸载时改名为 `.removed-<时间戳>` 保留 |

文件名格式 `<语言名>.ProxyLayer.aul2`，前缀只是给人看的，宿主的语言列表显示的是文件里 `SubLanguage` 的值。

## 3. 词条清单（16 条，必须与源码逐字一致）

section 固定为 `[ProxyLayer]`（**必须**等于插件文件名 `ProxyLayer.aux2` 的主名）。键 = 源码里的原始字符串，值 = 目标语言显示文本。

| # | 键（源码原文） | 类型 | 日本語 | English | 简体中文 |
| --- | --- | --- | --- | --- | --- |
| 1 | `代理图层` | 效果显示标签（`FILTER_PLUGIN_TABLE::label`） | プロキシレイヤー | Proxy Layer | 代理图层 |
| 2 | `ProxyLayer 代理图层 \| 请在下方可选「画像ファイル」指定素材（替换原图像输入），不是叠加在画像ファイル之后` | 分隔行提示（`FILTER_ITEM_SEPARATOR`） | 逐句意译 | 逐句意译 | 逐句意译 |
| 3 | `画像ファイル` | 源图文件项 | （原文即日文） | Image File | 图像文件 |
| 4 | `モード` | 模式（0–4） | モード | Mode | 模式 |
| 5 | `原图直通` | `モード` 的零值提示 | 元画像をそのまま | Original image passthrough | 原图直通 |
| 6 | `档位` | 代理档位（0–5） | プロキシ倍率 | Proxy scale | 代理档位 |
| 7 | `导出时强制原图` | 导出闸门开关 | 出力時は元画像を強制 | Force original on export | 导出时强制原图 |
| 8 | `输出探针日志` | 调试日志开关 | プローブログを出力 | Write probe log | 输出探针日志 |
| 9 | `显示代理标记` | 代理确认标记 | プロキシ確認マーカーを表示 | Show proxy marker | 显示代理标记 |
| 10 | `模拟导出态` | 自测用 | 出力状態をシミュレート | Simulate export state | 模拟导出态 |
| 11 | `本图层保持原图` | 单层逃生开关 | このレイヤーは元画像を維持 | Keep this layer at full resolution | 本图层保持原图 |
| 12 | `原始宽` | 元数据 | 元の幅 | Original width | 原始宽 |
| 13 | `原始高` | 元数据 | 元の高さ | Original height | 原始高 |
| 14 | `未设置` | 12/13 的零值提示 | 未設定 | Not set | 未设置 |
| 15 | `原始效果名` | 元数据（还原用） | 元のエフェクト名 | Original effect name | 原始效果名 |
| 16 | `原始文件名项` | 元数据（还原用） | 元のファイル項目名 | Original file item name | 原始文件名项 |

> `档位` 的零值提示是 `AUTO`，三种语言都保留原样，因此不必写词条。

## 4. 硬规则（改语言包前请务必遵守）

1. **绝不翻译插件名 `ProxyLayer`。** 插件用 `wcscmp(name, L"ProxyLayer")` 识别自己的效果（全工程转换、OFF 还原、跨图层保护、`EnsureDisplayName` 都依赖它）。一旦 `name` 变了，功能会整体失效。可以翻译显示标签 `代理图层`，因为 `name` 与 `label` 是两个字段。
2. **键必须逐字一致**，包括全角括号、`｜`、空格。差一个字符就静默回退原文，不报错。
3. 内部键名（`画像ファイル`/`档位`/`原始宽`…）是 `.aup2` 的存储键，**不能借本地化改名**；要改名只能走带迁移逻辑的大版本。
4. 值里不要出现 `=`（会被当作新的分隔符），也不要用换行。
5. 文件为 **UTF-8 无 BOM、CRLF**（与官方 `English.MediaFilter.aul2` 样例一致）；`;` 开头是注释。
6. `[Language]` 段里 `SubLanguage` 决定「语言设定」列表中显示的名字，`Information` 是描述。

## 5. 校验与新增语言

```bash
python3 tools/check_aul2.py      # 覆盖率 / 禁用键 / 编码 / 多余旧键，CI 里也会跑
python3 tools/check_aul2.py -v   # 顺带打印全部词条
```

该脚本直接从 `src/ProxyLayer.cpp` 解析 `FILTER_ITEM_*` 与 `FILTER_PLUGIN_TABLE`，因此**源码加了新参数、忘了补语言包时 CI 会失败**。

新增一门语言（例如 한국어）：

1. 复制 `Language/English.ProxyLayer.aul2` 为 `Language/한국어.ProxyLayer.aul2`；
2. 改 `[Language]` 的 `Information` / `SubLanguage`；
3. 按第 3 节表格逐条填入译文，文件名、section 名保持 `[ProxyLayer]`；
4. `python3 tools/check_aul2.py` 通过；
5. 在 `README.md` 顶部语言切换行加一行链接（可选），并在 `docs/README_xx.md` 里加译版（参考 `docs/README_ja.md`）；
6. 重新打包：`python3 packaging/build_release.py --tag <tag>`，两个 zip 会自动带上 `Language/`。

## 6. 还没本地化的部分（代码级 i18n 方案）

以下字符串由 C++ 运行期写入，`.aul2` 覆盖不到，当前仍是中文（或中日混排）：

| 位置 | 源码 | 示例 |
| --- | --- | --- |
| 编辑/对象菜单 | `register_edit_menu` / `register_object_menu` | `代理图层\整个工程自动代理 (开/关)` 等 4 项 |
| 设定菜单 | `register_config_menu` | `ProxyLayer 自动代理` |
| 设置窗口 | `PanelWndProc` | 标题、复选框、状态块、`确定` / `取消` |
| 日志 | `LogF` | 仅开发可见，可不译 |

推荐做法（官方 SDK 已支持，需要重新编译 DLL）：

```cpp
static CONFIG_HANDLE* g_config = nullptr;
EXTERN_C __declspec(dllexport) void InitializeConfig(CONFIG_HANDLE* handle) { g_config = handle; }
static LPCWSTR Tr(LPCWSTR s) {
    return (g_config && g_config->translate) ? g_config->translate(g_config, s) : s;
}
// 注册时：host->register_edit_menu(Tr(L"代理图层\\整个工程自动代理 (开/关)"), &OnMenuAutoProxy);
```

配合 `EVENT_TYPE::RELOAD_LANGUAGE`（`plugin2.h`，v2.1.17 起可用）在用户切换语言后重建设置窗口文本；菜单项若要跟着变，需要在事件里注销/重注册，或提示用户重启宿主。对应的 `.aul2` 键就是这些完整字符串本身（含 `\` 与括号），值写译文即可。

> 注意：这一步只影响显示，**不要**顺手把 `FILTER_ITEM_*` 里的键名改成英文——那会破坏 `.aup2` 兼容性。
