> **存档说明（v0.3）**：本文是 2026-11-15 对 **v0.2.1** 的评估，保留作历史记录。第 4 节的三语 `.aul2` 方案与第 5 节的路线在 v0.3 已被更彻底的方案取代：内部键名统一日语（宿主默认语言）、删除 `モード`、只保留中/英两个显示包、`information` 版本串修正。另经真机沙箱实测确认，`日本語.ProxyLayer.aul2` 因前缀不匹配宿主语言设置而**永远不会加载**（宿主只有 `Default` = 日语）。当前状态见 [CHANGELOG.md](../CHANGELOG.md) 与 [LOCALIZATION.md](LOCALIZATION.md)。
>

# 仓库评估与改进建议（2026-11-15）

本文是对 `AviUtl2-ProxyLayer` 的一次完整体检：先核对事实，再按严重度列问题，最后给可执行的路线。
这次同时按参考仓库 [`AviUtl2_VolumetricLight3D`](https://github.com/CylnaNangong/AviUtl2_VolumetricLight3D) 的多语言范式做了本地化，改动清单在第 6 节。

## 0. 结论摘要

**总体判断：工程质量明显高于同类个人插件，核心机制（代理档位、导出闸门、UI 线程遍历、跨图层取样保护）设计扎实且有实测数据支撑；主要短板集中在「对外呈现」与「工程自动化」，而不是渲染逻辑。**

| 维度 | 评价 |
| --- | --- |
| 核心功能与稳定性 | ★★★★☆ 有 minidump 归因、有遥测、有反例记录（已证伪路线写在文档里），这在这类插件里很少见 |
| 文档 | ★★★★☆ 中文 README + TECHNICAL 信息密度高；缺多语言、缺 CHANGELOG、注释引用了仓库里不存在的内部报告 |
| 国际化（本次处理） | ★★☆☆☆ → ★★★★☆ 参数名中/日混排且无语言包；现已提供三语 `.aul2`，菜单/设置窗口仍待代码级 i18n |
| 构建与 CI | ★★☆☆☆ CI 只打包不编译、无任何源码/资源级检查，二进制靠人工刷新 |
| 打包与发布 | ★★★★☆ 双包策略（拖入式 / 安装器）设计正确、幂等；`packaging/package.ini` 是死文件，有漂移风险 |
| 可维护性 | ★★★☆☆ 单文件 2140 行、9 处硬编码效果名比较、生产 DLL 内含自测钩子 |

## 1. 事实核对

- 代码规模：`src/ProxyLayer.cpp` 2140 行、`ProxyLuaJIT.cpp` 169 行、`png_writer.h` 106 行、`ExportProbe.cpp` 122 行；打包脚本 90 行；CI 122 行。
- 产物：`dist/ProxyLayer.aux2`（113,664 B，sha256 `5ad74a15…`）、`dist/luaJIT.dll`（28,672 B，sha256 `1df3e79d…`）；二进制里确实含有源码中的中文字符串（`档位`、`导出时强制原图`、`本图层保持原图`），即 `dist/` 与当前 `src/` 一致。
- 文件编码：源码/文档为 UTF-8 无 BOM + LF；`.aul2` 为 UTF-8 无 BOM + CRLF（与官方样例一致）；`.bat`/`.ps1` 在仓库内是 LF，打包时 `build_release.py` 会转成 CRLF（正确）。
- 名称匹配：`L"ProxyLayer"` 字面量出现 **9 次**；自测钩子（`proxy-autotest.on` / `proxy-paneltest.on` / `proxy-safety-test.on` / `proxy-autoproxy.on`）相关调用 **5 处**。

## 2. 做得好、值得保留的地方

1. **踩坑有记录、被证伪的路线不删历史**：`TECHNICAL.md` 第 8 节把 `mode 5`、`OBJECT_INFO` 写回、CPU 拷贝优化都写成「已证伪」，这比只写成功路径有用得多。
2. **线程模型的教训被固化进代码**：后台 `std::thread` 调 `call_edit_section_param()` 的崩溃（`0xc0000005` @ `aviutl2.exe+0x215c50`）改成 UI 线程隐藏窗口 `WM_TIMER`，并统一走 `CallEditSection()` 网关。
3. **导出闸门有像素级验收**（0.00% 差异，QSVEnc 实测），不是「看起来没问题」。
4. **不重复解码原图**这条优化有遥测数字（97.2% 快路径、95.6 ms/次解码），并把遥测格式写成 `tools/read_perf.py`，可复现。
5. **打包的两次事故防范**：绝不创建 exe 同目录 `data/`（会导致宿主配置「恢复初始化」）、luaJIT 代理必须先把官方 DLL 改名为 `luaJIT_real.dll` 再放转发器。
6. **`-Installer.zip` 幂等 + 只改名不删除**，卸载永远可恢复。

## 3. 问题清单

### P0（会影响用户，建议下个版本修）

1. **插件信息字符串还是 v0.1 的实验名**  
   `FILTER_PLUGIN_TABLE::information` = `ProxyLayer v0.1 — preview proxy layer / 3D-camera alignment A-B probe`。用户在「插件信息」里看到的是 v0.1 和一个内部 A/B 实验的描述，与 v0.2.1 的实际形态不符。属编译期字符串，改完需重编。
2. **参数名中/日混排且无语言包**  
   `画像ファイル`、`モード`（日）与 `档位`、`导出时强制原图`、`本图层保持原图`、`原始宽`（中）混在一起；AviUtl2 用户主体是日语环境。已在本次修复（`.aul2` 三语包），但**发布时必须把 `Language/` 真的打进 zip**，否则等于没做。
3. **生产 DLL 里带着自测代码路径**  
   `proxy-autotest.on`、`proxy-paneltest.on`、`proxy-safety-test.on` 三个标记文件可以把插件切进自测流程（自动建对象、翻转开关、模拟导出态）。风险不是「会被误触发」（标记文件要恰好放在 DLL 目录），而是一旦有人误放/残留，行为会变得难以解释。建议加编译期开关（如 `PROXYLAYER_SELFTEST`）在发布构建里剔除。
4. **效果名硬编码比较 9 处**  
   `wcscmp(name, L"ProxyLayer")` 散落在转换、还原、显示名补全、跨图层保护、安全自测里。任何一次重命名（或将来做「用户可改效果名」）都会静默失效。建议收敛成 `IsProxyEffect(LPCWSTR)` 单一入口，并为旧名留兼容集合。

### P1（工程效率，影响长期维护）

5. **CI 只打包，不验证任何东西**  
   现在的工作流能保证「有 zip」，但保证不了「语言包词条齐全」「Python 工具能跑」「源码至少语法正确」。本次已加入 `tools/check_aul2.py`，建议继续加：`python3 -m py_compile tools/*.py`、`bash -n build_*.sh`。真正编译 DLL 需要官方 SDK 头文件（不可再分发），可行方案是 Windows job 里从官方地址下载 SDK（或用 secret 提供），仅做 `cl /c` 语法检查、不发布产物。
6. **`packaging/package.ini` 是死文件**  
   `build_release.py` 内联生成 `package.ini`，仓库里那份 `packaging/package.ini` 没人读，且内容已过期（`name=ProxyLayer / 代理图层`）。要么让脚本读它并替换 `{{TAG}}`（推荐），要么删掉，避免「改了没生效」。
7. **没有 CHANGELOG**  
   变更历史目前散在 README 正文与提交信息里。参考仓库有 `docs/CHANGELOG_zh.md` / `CHANGELOG_ja.md`，建议照搬结构（含「本次是否重编译 DLL」这一行，正好对应 README 里那张兼容性表）。
8. **注释/文档引用不可追溯的内部报告**  
   `TECHNICAL.md` 开头写「从开发期的五篇报告提炼」，代码里出现 `See report #5 section 10`。外部读者（以及半年后的自己）无从查证。建议把关键 A/B 数据、minidump 结论直接内联或落到 `docs/evidence/`。
9. **二进制无校验清单**  
   `dist/` 里的 DLL 是发布内容的核心，但仓库内没有 `dist/SHA256SUMS`（只有 CI summary 里有）。建议入库一份，并在 `install.ps1` 里可选校验。
10. **`install.ps1` 无 `-WhatIf` / `-DryRun`**  
    脚本会改名、备份、覆盖文件，却没有演练模式，也没有 `[CmdletBinding(SupportsShouldProcess)]`。建议补 `-WhatIf`，并加一个 `-Lang` 之外的最小自测（例如 `-VerifyOnly` 打印检测结果）。

### P2（打磨项）

11. **开关开启时每 5 秒全场景扫描一次**（`RunPeriodicPass`：`g_watch_dirty || now-s_lastScan>=5000`）。在 21 场景 × 200 层的工程上这是可感知的周期性开销。建议改成「只在 dirty 时扫描 + 长周期（30 s）兜底」，或按对象数自适应。
12. **单文件 2140 行**  
    `ProxyLayer.cpp` 已经包含：效果定义、场景遍历、跨层保护、显示名、设置窗口、自测、缓存、遥测。建议按下节顺序拆成 `proxy_cache.cpp` / `proxy_autopass.cpp` / `proxy_panel.cpp`，但不急。
13. **`FILTER_ITEM_SEPARATOR` 被当成提示文字用**  
    依赖「分隔行的名字会被显示」这一宿主行为，长文本（含全角括号）在不同语言设置下的换行/截断未验证。语言包里对应词条已给出，但值得在真机上各语言各看一眼。
14. **无 SPDX 头**  
    UTF-8 + LF/CRLF 的约定已由新增的 `.editorconfig` + `.gitattributes` 兜住（`.aul2`/`.bat`/`.ps1` 走 CRLF），但源文件仍没有 SPDX 许可标识，建议补 `// SPDX-License-Identifier: MIT`。
15. **`README.md` 提到 `module.ini` 信任机制**  
    说法本身合理，但仓库内没解释该文件由谁生成、格式如何；官方 SDK 的 README（本机核对的是 2026-11 版）也没有展开。建议指向官方文档或加一句「由宿主自动维护，无需手动编辑」，避免用户去翻改。
16. **文档缺截图/动图**  
    参考仓库用 GIF 对比 OFF/ON，本仓库纯文字。一张「转换前卡顿 / 转换后流畅」的时间轴截图 + 一张设置窗口截图，能省掉大量解释。

## 4. 本地化专项（本次改动）

参照 `AviUtl2_VolumetricLight3D` 的做法（多语 README + `.aul2` 语言包 + 多语 `package.txt`），补齐了：

| 项目 | 内容 |
| --- | --- |
| 语言包 | `Language/日本語.ProxyLayer.aul2`、`Language/简体中文.ProxyLayer.aul2`、`Language/English.ProxyLayer.aul2`（16 条词条 ×3，UTF-8/CRLF） |
| 文档 | 新增 `docs/README_ja.md`、`docs/README_en.md`；`README.md` 顶部加语言切换；新增 `docs/LOCALIZATION.md`（词条清单 + 硬规则 + 新增语言流程 + 代码级 i18n 方案） |
| 打包 | 两个 zip 都带上 `Language/`；安装器把语言包复制到 `<数据目录>\Language\`，卸载改名保留 |
| 安装器 | `install.ps1` 增加 `-Lang ja\|zh\|en` 与自动判定（系统 UI 语言），全部提示三语化；`install.bat` / `uninstall.bat` 三语标题 |
| 校验 | 新增 `tools/check_aul2.py`：从源码解析词条，检查覆盖率、禁用键（插件名）、编码、陈旧键，已接入 CI |

关键设计判断（详见 `LOCALIZATION.md`）：**插件名 `ProxyLayer` 绝不翻译**（9 处 `wcscmp` 依赖它），显示标签 `代理图层` 可以翻译；`.aul2` 只改显示、不改 `.aup2` 存储键，因此不需要重编 DLL、旧工程零风险。

## 5. 建议路线

- **v0.2.2（只动文档/语言包/打包，无需重编译）**：合并本次本地化；修 `packaging/package.ini` 漂移；补 CHANGELOG；`dist/SHA256SUMS`。发布后用真机各语言截图验证 `[ProxyLayer]` 词条确实生效。
- **v0.2.3（需重编译）**：修 `information` 版本串；加 `IsProxyEffect()` 收敛名称比较；`InitializeConfig` + `translate()` 让菜单与设置窗口跟随宿主语言（配合 `RELOAD_LANGUAGE`）；给自测代码加编译期开关。
- **v0.3（可考虑）**：内部键名英文化 + `.aup2` 迁移（按 `原始效果名` 的做法记旧键），彻底解决中/日混排；拆分 `ProxyLayer.cpp`；CI 做真语法编译。

## 6. 本次改动清单

```
Language/日本語.ProxyLayer.aul2     新增 16 词条
Language/简体中文.ProxyLayer.aul2   新增 16 词条
Language/English.ProxyLayer.aul2    新增 16 词条
README.md                           语言切换头、界面语言章节、语言包/目录/兼容性说明
docs/README_ja.md                   新增（日本語）
docs/README_en.md                   新增（English）
docs/LOCALIZATION.md                新增（机制、词条表、硬规则、扩展流程）
docs/REVIEW.md                      新增（本文）
docs/TECHNICAL.md                   新增第 9 节「本地化与显示文本」
tools/check_aul2.py                 新增（词条覆盖率校验，接入 CI）
packaging/package.txt               三语重写（日/中/英，含语言包说明）
packaging/README-INSTALL.txt        三语重写
packaging/install.ps1               -Lang + 自动语言判定、语言包安装/卸载、三语提示
packaging/install.bat               三语标题与提示
packaging/uninstall.bat             三语标题与提示
packaging/build_release.py          两个 zip 均包含 Language/；package.ini 从模板生成
packaging/package.ini               与生成逻辑同源（{{TAG}} 占位）
.github/workflows/release.yml       增加语言包校验、Python/Shell 语法检查、zip 内语言包存在性检查
.editorconfig / .gitattributes      新增（.aul2/.bat/.ps1 强制 CRLF，其余 LF）
.gitignore                          忽略 Python 缓存
```
