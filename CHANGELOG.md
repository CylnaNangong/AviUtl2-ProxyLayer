# Changelog

## v0.3.4 — 2026-11-17（图层错位修复：绝对像素锚点 + LuaProxy 尺寸误映射；Panel 置前）

v0.3.3 修好性能后，第一次让 3.63 MPx 级素材真正被降采样，把两条此前被 `div=1`
遮蔽的几何耦合暴露出来。用户真机（`PV工程.aup2`）报告两个角色后发图层错位：
寺倾花后发飘离头部、南宫颜后发在主合成里向上平移，而**单独打开南宫颜次合成却正常**。

- **修复 1：同对象尺寸依赖脚本自动保护（寺倾花）**
  - 根因：两个后发都挂 `SoftAnchorBone.anm2`（`髪の毛(長め)`）。脚本的 `bone` 是
    **6 个绝对像素坐标**（如 `372.00,-992.00,0.00,-100.00,-356.00,858.00`），
    并用 `obj.w/obj.h` 生成 `obj.drawpoly()` 网格（`y0 = -h/2 + h*yy/iyN`）。
    纹理缩成 1/div 后，锚点常量与缓冲区尺寸不一致，`obj.w/h` 补偿与 LuaProxy 都救不回来。
  - 尺寸差正好解释不对称：`花-后发.png` 1724×2104（3.63 MPx）→ `need=1.15 → div=2`（被代理，错位）；
    `颜-后发.png` 628×856（0.54 MPx）→ `need=0.44 → div=1`（L0，正常）。
    v0.3.2 及更早所有图都判 `div=1`，所以从未出现。
  - 用户手动勾选 `このレイヤーは元画像を維持` 后错位立即消失，确认根因。
  - 现改为**自动保护**：按参数值形状检测（6 个数值、同时含 ≤ −64 与 ≥ +64 的绝对坐标元组），
    命中即让该对象保持 L0；并扫描对象 alias 兜底第三方脚本的其它项目名。
    **不使用脚本名黑名单**，也不影响调色/抠图等常规效果。工程内 453 个效果实例扫描结果
    只命中 `bone` 5 处，`display.camera=<负,...,0,0,0>` 等已被排除。
- **修复 2：LuaProxy 尺寸映射误命中（南宫颜，主合成专属）**
  - 根因：`ProxyLayer_MapSize(cur, isW, out)` 只按**单个维度数值**匹配、无对象身份。
    `5140×1712 @ div=2` 的代理高度是 **856**，而南宫颜后发是原生 **628×856**；
    宿主给后发写 `obj.h=856` 时被误映射成 `1712`，`h2` 从 428 变 856，
    网格整体上移约 428 px。全工程 57 个代理物件 × 13 个未代理物件逐项复算，
    **碰撞恰好只有这一处，且正好命中所报图层**。
  - 为什么只在主合成出错：`5140×1712` 素材属于 2048×1080 的主歌/副歌场景，
    只有主合成渲染时才会注册 `proxyH=856 → srcH=1712` 这条映射；
    单独打开 10000×6250 的南宫颜次合成时该映射根本不存在。
  - 修复：新增全分辨率尺寸登记表。注册代理映射时若某个维度与已登记的原生尺寸冲突，
    只禁用**该维度**的映射（`allowW` / `allowH`）；原生尺寸晚于映射登记时也会回溯禁用。
    因此 `obj.h=856` 不再被改写，而 `obj.w=2570 → 5140`、`1250 → 10000` 等正常补偿全部保留。
    **不需要重编译或替换 `luaJIT.dll`** —— 修复在 `ProxyLayer.aux2` 导出的 `ProxyLayer_MapSize()` 内。
- **修复 3：设置面板首次弹出不置前**
  - 日志有 `Panel: shown` 但窗口不在前台（打开 GCMZDrops 面板后才出现）。
    现在每次显示都重新居中到宿主所在显示器，并走 `HWND_TOPMOST → HWND_NOTOPMOST`
    抬升 + `BringWindowToTop` / `SetForegroundWindow`，前台锁拒绝时用
    `AttachThreadInput` 兜底。日志新增 `fg=... visible=...` 便于验证。
- 遥测新增 `veto=`（**登记映射时**被拦截的维度碰撞次数），`tools/read_perf.py` 支持全部四代日志格式。
  注意：若宿主先渲染 5140×1712 素材、后渲染南宫颜后发，走的是「原生尺寸登记后回溯禁用」路径，
  同样会修好但不会计入 `veto`；因此 **`veto=0` 不代表修复失效，以画面是否贴合为准**。

**是否需要重编译 DLL**：是。`dist/ProxyLayer.aux2` 已重编译（约 125 KB，MD5 `eecd1723…`）。
内部键名、语言包、**LuaJIT 转发代理**、安装脚本均未改动。升级后重启 AviUtl2 即可，
**无需重新执行「自动代理」**——保护逻辑由每 5 秒的 UI 线程扫描自动落到既有对象上。
用户工程文件全程只读，未被修改。

## v0.3.3 — 2026-11-16（AUTO 子场景判定修复：真机「提速不明显」的根因）

- **问题**：真机 `PV工程.aup2` 上半透明变灰已修好、播放也确实变快（L0 零拷贝让
  `filter_avg` 从 221 ms 降到 55.6 ms），但用户仍感觉「变流畅的效果不甚明显」。
- **定位**：v0.3.2 新增的 `L0:gate=... keep=... auto1=...` 计数把因果钉死了 ——
  `gate=0`、`keep=0`、`auto1=754`（**占全部调用 69.6%**）。不是导出闸门、不是手动保持原图，
  而是 **AUTO 自己判定 `div=1`（完全不降采样）**。`div≥2` 的帧占 29.1%，与 `fast=28.4%`
  几乎完全吻合，证明只要 div≥2 就能命中代理快路径。剩余 `decode_avg=56 ms`、
  `decode_max=313 ms` 全部来自这些每帧重新解码原图的 `div=1` 帧。
- **根因**：`AutoDiv()` 拿 `FILTER_PROC_VIDEO::scene->width/height` 当「屏幕」。
  该工程有 **2 个 10000×6250（62.5 MPx）子场景**（`寺倾花` / `南宫颜`），素材也正好是
  **8 张 10000×6250**，主合成却只有 2048×1080。于是
  `need = sqrt(62.5 / 62.5) = 1.00 → div=1`，最重的 8 张图**一张都没被代理**。
  子场景是被缩小合成进主画面的，其画布分辨率根本不是观众能看到的像素数。
- **修复（AUTO v3）**：
  1. `SCENE_INFO::origin_id != id` 判定「正在渲染被合成的子场景」；子场景与超大工作画布
     （> ~12.44 MPx）把有效「屏幕」钳制到 1080p 级预览预算（~2.77 MPx）。
  2. **1080p / 4K 主合成完全不受影响** —— 逐项复算确认 v2 与 v3 的 `div` 在
     2048×1080 与 3840×2160 主场景下**完全一致**（AUTO v2 刻意保留的「4K 更清晰」不变）。
  3. 绝对兜底：源图 ≥ 16 MPx 时，无论如何都至少 `div=2`，杜绝「大图零代理」这一类退化。
  4. 手动档位（1/2 … 1/32）与 `このレイヤーは元画像を維持` 仍完全绕过 AUTO，
     导出闸门照旧强制 L0，画质不受影响。
- 用工程内 **23 个真实素材**复算：10000×6250 子场景下 8 张 62.5 MPx 素材由
  `div=1 → 8`，9455×6212 由 `1 → 8`，5651×6250 由 `1 → 4`；主合成判定逐项不变。
- 遥测新增 `cap=`（预算抬升 div 的次数）、`sub=`（子场景帧数）、`big=`（超大画布帧数）、
  `maxw=`（窗口内最宽场景画布）、`d1mp=`（`div=1` 帧里最大源图 MPx）。
  `tools/read_perf.py` 同步支持三代格式并直接打印修复验证摘要。

**是否需要重编译 DLL**：是。`dist/ProxyLayer.aux2` 已重编译（120,320 B）。
内部键名、语言包、LuaJIT 转发代理、安装脚本均未改动。升级后**重启 AviUtl2 再播放 10–20 秒**，
看 `ProxyLayer-perf.log` 里 `cap` 是否升高、`div[1]` 与 `decode_avg` 是否明显下降。

## v0.3.2 — 2026-11-16（半透明 bugfix + L0 性能修复）

- **修复半透明图层变灰**（用户真机 `PV工程.aup2` 报告：眼睛与云层边缘发灰，隐藏半透明图层后正常）。
  根因：`ReadPremul()` 对 `INPUT_PIXEL_FORMAT::RGBA` / `BGRA` 多做了一次 `RGB × alpha`，但 `filter2.h` 明确写
  `RGBA = 28 // DXGI_FORMAT_R8G8B8A8_UNORM ※PIXEL_RGBA`，与 `set_image_data()` 的 `PIXEL_RGBA` 是同一结构、
  同一（宿主内部 premultiplied）语义。半透明像素因此被乘两次 alpha 而变暗。
  隔离沙箱 A/B（原生 `画像ファイル` vs ProxyLayer div=2，2000×1000 半透明测试图）像素采样：

  | 区域 | 修复前 R/L | 修复后 R/L |
  | --- | --- | --- |
  | 不透明红 | 1.037 | 1.000 |
  | 50% 半透明青 | **0.577** | 1.117 |
  | alpha 渐变 | **0.407** | 0.883 |

  不透明区域始终一致，只有半透明被压暗 —— 符合"alpha 乘两次"的特征。
  现在改名为 `ReadSourcePixel()`，只做通道交换 / 16→8 / half→8 转换，**不再预乘**。`Downsample()` 仍在宿主
  premultiplied 空间做线性 box average（对半透明边缘数学正确）。
- **修复 L0 路径的性能灾难**：`div=1` / 导出闸门 / 「このレイヤーは元画像を維持」原先走 `FallbackL0()`，
  对**完整原图逐像素转换并再分配一份同尺寸缓冲**。用户工程有 8 张 10000×6250（62.5 MPx）素材，
  即每次调用 62.5M 次虚调用 + 250 MB 分配；真机遥测 `filter_avg≈300ms`、`decode_avg≈72ms`、
  `fast=0~40%`、`div[1]` 占 70–100%。现在当缓存格式已是紧密排列的 `PIXEL_RGBA` 时直接把
  `CACHE_FILE_IMAGE::buffer` 交给 `set_image_data()`（零拷贝）；有行 padding 时改为逐行 `memcpy`，
  其余格式才逐像素转换。
- 遥测新增 `L0:gate=<n> keep=<n> auto1=<n>` 三个计数，下次可直接分辨 L0 是导出闸门、
  单层保持原图、还是 AUTO 判定无需降采样。

**是否需要重编译 DLL**：是。`dist/ProxyLayer.aux2` 已重编译。内部键名、语言包与 LuaJIT 转发代理均未改动。

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
