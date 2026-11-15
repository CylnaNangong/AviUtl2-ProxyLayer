# Changelog

## v0.3.1 — 2026-11-15

- **删除参数面板顶部的 `FILTER_ITEM_SEPARATOR` 长提示行**（那行 `ProxyLayer | Pick the source image...` 又长又被截断）。改用官方 **`[Tips.ProxyLayer]`** 悬浮气泡：`effect.name` 写效果说明，12 个参数键写各自说明，鼠标悬停才显示。规则参考 [docs.aviutl2.jp](https://docs.aviutl2.jp/) 与宿主自带 `简体中文.aul2` 头注释「若定义如 `[Tips.ぼかし]` 这样在滤镜名称前加上 "Tips." 的区段，则可定义物件设置中设置项工具提示中的文字」。真机沙箱实测气泡正常弹出。
- **设置窗口与菜单跟随宿主语言**：导出官方 `InitializeConfig(CONFIG_HANDLE*)`（在 `InitializePlugin()` 之前调用）保存句柄，用 `get_language_text(handle, L"Panel", key)` 翻译窗口标题 / 开关 / 按钮 / 状态文本 / 菜单项。语言包新增 **`[Panel]`** 段（12 个 ASCII 标识符键），未装包时回退日语（宿主默认语言）。中文环境实测：`ProxyLayer 自动代理` / `自动代理（整个工程·所有场景，长期生效）` / `确定` / `取消`，菜单亦为中文。
- 语言包由 2 个变回 **3 个**：`Default.ProxyLayer.aul2`（只放日语 TIPS，默认语言无需翻译显示名）、`简体中文.*` 与 `English.*`（显示名 + `[Panel]` + TIPS）。`tools/check_aul2.py` 重写为按 section 校验：每个包必须有 `[Tips.ProxyLayer]`（`effect.name` + 12 键），非 Default 包还须有 12/12 显示名与 `[Panel]`。
- CI 的 `test -f` 与 zip 内语言包检查同步加入 `Default.*`；`build_release.py` / `install.ps1` 用 glob 自动包含。
- 效果名、菜单根名仍为英语 `ProxyLayer`；内部键名、`ProxyLayer_MapSize` 导出与 LuaJIT 转发代理均未改动。

**是否需要重编译 DLL**：是（新增 `InitializeConfig` 导出、删除 separator）。`dist/ProxyLayer.aux2` 119,296 B。

## v0.3.0 — 2026-11-15（**破坏性变更 / breaking**）

**内部键名统一为日语**（AviUtl2 默认语言 `Default`），与社区惯例一致。升级前请先备份工程。

- 所有 `FILTER_ITEM_*` 名字与 `.aup2` 存储键改为日语：`档位→プロキシ倍率`、`原始宽→元の幅`、`原始效果名→元のエフェクト名`、`本图层保持原图→このレイヤーは元画像を維持` 等。
- **删除 `モード` 参数项**：0/1/2/4 都是早期 A/B 测量变体（`draw_image` / `draw_poly` / 无补偿对照），生产路径固定为 `set_image_data` + 相对缩放，现为编译期常量。`DrawProxy()` 从 4 个分支简化到 1 个，`EnsureTextures()` 一并移除。
- 插件显示标签、菜单与设置窗口文案统一用英语 `ProxyLayer`（`.aul2` 覆盖不到菜单与自绘窗口）。`information` 从 v0.1 的实验名改为 `ProxyLayer v0.3 - whole-project preview proxy / full-resolution export`。
- **语言包从 3 个减到 2 个**：删除无效的 `日本語.ProxyLayer.aul2`（宿主默认语言是 `Default`，不存在「日本語」这个语言设置）；`简体中文` / `English` 两个包改用日语键，12/12 词条通过 `check_aul2.py` 校验。默认日语用户零配置。
- 合并 Arena.ai 的 i18n / 文档 / CI 改进：多语 README、`tools/check_aul2.py`、`py_compile` + `bash -n` 校验、zip 内语言包检查、`.editorconfig` / `.gitattributes`、`install.ps1` 三语提示与 `-Lang`、语言包安装/卸载、`package.ini` 改为模板读取。
- `install.ps1` 保留"宿主升级覆盖 `luaJIT.dll` 时用当前官方 DLL 刷新 `luaJIT_real.dll`"的逻辑。

**升级注意**：v0.2.x 保存的转换态工程含中文键，新 DLL 不再识别；请先对工程点一次 OFF 还原，或重新执行自动代理。`画像ファイル` 键名未变，未保存原图态的工程不受影响。

**是否需要重编译 DLL**：是。`dist/ProxyLayer.aux2` 已重新编译（111,616 B）。

## v0.2.1 — 2026-11-15

- 移除已证伪的 `补偿 obj.w/h (实验)` 参数项、`DrawProxy()` 的 `compensate` 分支与 `OBJCOMP` 日志（原生 `OBJECT_INFO` 写回被 Lua 探针证伪）。
- **保留**可选的 `luaJIT.dll` 转发代理、`ProxyLayer_MapSize` 导出与 `RegisterSizeMap()`：A/B 实测 1024→2048 有效，OpenGlow / 光束_H 等读 `obj.w/h` 的脚本依赖它。
- README 参数表 / 已知限制、`docs/TECHNICAL.md` 同步。
- 需要重编译 DLL。

## v0.2.0 — 2026-11-15

首个自动 Release。`.au2pkg.zip`（拖入式）与 `-Installer.zip`（主插件 + 可选 `luaJIT.dll` 转发代理 + 一键改名/还原）。推 `v*` tag 自动打包上传；CI 不编译，直接使用已提交的 `dist/` 二进制。
