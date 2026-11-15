# 本地化（`.aul2`）设计与词条清单

> 本文描述 **v0.3.1** 的当前机制。v0.2.1 时期"三语显示包"的评估存档见 [`REVIEW.md`](REVIEW.md)。

## 1. 三条铁律

1. **内部键名 = 日语**（AviUtl2 默认语言 `Default`）。`FILTER_ITEM_*` 的名字就是 `.aup2` 里保存的键，社区惯例如此；默认日语用户不需要任何显示包就能看到日语参数名。
2. **效果名 = 英语 `ProxyLayer`**。`create_effect()` / `get_effect_name()` 与 9 处 `wcscmp(..., L"ProxyLayer")` 依赖它，绝不翻译。菜单根名同样固定为 `ProxyLayer`。
3. **`.aul2` 只改显示，不改 `.aup2` 存储键**。任何内部键名改动都是破坏性变更，必须重编译并写进 CHANGELOG。

## 2. 宿主的语言文件规则（实测）

宿主自带 `简体中文.aul2` 的头注释写明（[docs.aviutl2.jp](https://docs.aviutl2.jp/) 同源）：

- 文件名多句点 = **差异文件**，如 `English.xxx.aul2` 会并入 `English` 设置；
- **`Default` 语言只读取 `Default.xxx.aul2` 差异文件**；
- 存在以滤镜名 / 脚本名命名的 section 时优先引用该 section（我们用 `[ProxyLayer]`）；
- **「若定义如 `[Tips.ぼかし]` 这样在滤镜名称前加上 "Tips." 的区段，则可定义物件设置中设置项工具提示中的文字」**。

真机 `Language\` 前缀统计：`English(30)` / `简体中文(19)` / `Default(15)` / `Deutsch(3)` / `community_*`，**没有「日本語」** —— `Default` 就是日语。

隔离沙箱三轮实测：只放 `日本語.ProxyLayer.aul2` 完全不生效；改名 `Default.ProxyLayer.aul2` 后 `档位→プロキシ倍率` 等全部生效；三包并存且 `Language=简体中文` 时中文包生效、不被 Default 污染。

## 3. 三个语言包

| 文件 | `[ProxyLayer]` 显示名 | `[Panel]` 面板文案 | `[Tips.ProxyLayer]` 悬浮提示 |
| --- | --- | --- | --- |
| `Default.ProxyLayer.aul2` | 不需要（键已是日语） | 不需要（DLL 回退日语） | 日语 |
| `简体中文.ProxyLayer.aul2` | 12 键 → 中文 | 12 键 → 中文 | 中文 |
| `English.ProxyLayer.aul2` | 12 键 → 英文 | 12 键 → 英文 | 英文 |

格式：UTF-8 **无 BOM** + **CRLF**。`[Language]` 段的 `SubLanguage` 必须等于宿主语言设置值（Default 包不写）。三个包都打进 `.au2pkg.zip` 与 `-Installer.zip` 的 `Language/`，安装器复制到 `<数据目录>\Language\`，卸载时改名 `.removed-<时间戳>` 保留。

### 3.1 `[Tips.ProxyLayer]` —— 官方悬浮气泡

v0.3.1 删掉了参数面板顶部那一整行 `FILTER_ITEM_SEPARATOR` 长提示（又长又被截断），改成鼠标悬停才出现的 TIPS：

```ini
[Tips.ProxyLayer]
effect.name=把整个工程里的大尺寸静态图像一键换成低分辨率代理 ... \n 第二行 ...
画像ファイル=指定作为代理来源的图像文件。
プロキシ倍率=AUTO 按素材与场景的像素数自动选档。
...（12 个参数键各一行）
```

- 段名 = `Tips.` + 效果名（这里是 `ProxyLayer`）。
- `effect.name` 是效果整体的说明；其余键必须与源码内部键名（日语）逐字一致。
- 值里 `\n` 表示换行（与宿主自带 `简体中文.VolumetricFog3D.aul2` 等一致）。
- 真机沙箱实测：中文环境悬停参数名，气泡正常弹出中文说明。

### 3.2 `[Panel]` —— 设置窗口与菜单

菜单（`register_edit_menu`）和自绘设置窗口（`PanelWndProc`）是运行期 C++ 字符串，普通翻译段覆盖不到。SDK 的 `config2.h` 提供：

```c
// 設定関連機能初期化関数 ※InitializePlugin() より先に呼ばれます
void InitializeConfig(CONFIG_HANDLE* config);
LPCWSTR (*get_language_text)(CONFIG_HANDLE*, LPCWSTR section, LPCWSTR text);
```

插件导出 `InitializeConfig()` 保存句柄，再用 `get_language_text(handle, L"Panel", key)` 查 `[Panel]` 段。键是 **ASCII 短标识符**，这样改文案不会让翻译失效；未定义时 DLL 回退到日语（宿主默认语言）。

| 键 | 用途 | 日语 fallback | 简体中文 |
| --- | --- | --- | --- |
| `title` | 窗口标题 / 配置菜单 | `ProxyLayer 自動プロキシ` | `ProxyLayer 自动代理` |
| `switch` | 开关 checkbox | `自動プロキシ（全シーン、設定を保存）` | `自动代理（整个工程·所有场景，长期生效）` |
| `ok` / `cancel` | 按钮 | `OK` / `キャンセル` | `确定` / `取消` |
| `on` / `off` / `unsaved` | 状态片段 | `ON` / `OFF` / `  (未保存)` | `已开启` / `已关闭` / `  （未保存）` |
| `status` | 状态段落（`%s×3` + `\n`） | 日语 | 中文 |
| `menu.auto` / `menu.settings` / `menu.keepall` / `menu.restore` | 四个编辑菜单子项 | 日语 | 中文 |

> `status` 里必须保留 3 个 `%s` 占位符。菜单在宿主启动时注册，切换语言后需重开 AviUtl2 才会刷新菜单文字（设置窗口每次显示时重新取，会立即生效）。

## 4. 词条清单（12 键）

`tools/check_aul2.py` 从 `src/ProxyLayer.cpp` 解析 `FILTER_ITEM_*`，只校验含非 ASCII 的键。ASCII 的 `ProxyLayer`（label）与 `AUTO`（零值提示）不需要翻译。

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

## 5. 校验与新增语言

`python3 tools/check_aul2.py` 逐 section 校验：每个包必须有 `[Tips.ProxyLayer]`（`effect.name` + 12 键）；非 Default 包还须有 12/12 显示名和 `[Panel]`；另外检查 UTF-8 无 BOM、CRLF、陈旧键与插件名误翻译。

新增语言：复制 `English.ProxyLayer.aul2` → `Language/<语言名>.ProxyLayer.aul2`（`<语言名>` 必须与宿主语言设置值一致），改 `[Language]`，翻译 `[ProxyLayer]` 的 12 个值、`[Panel]` 的 12 个键和 `[Tips.ProxyLayer]` 的 13 行，跑 `check_aul2.py` 应为 `0 failure(s)`。`build_release.py` / `install.ps1` 用 glob 自动打包，CI 也会自动检查 zip 内容。
