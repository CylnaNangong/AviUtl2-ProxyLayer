# 实现细节与实测记录

本文从开发期的五篇报告提炼，已去掉本机路径与用户工程信息。

## 1. 档位决策（AUTO v3）

```
need = sqrt(srcPixels / effectiveScenePixels)
div  = 2^ceil(log2(need))，上限 8；need <= 1 时 div = 1
```

目标是「代理纹素数 ≈ 观众最终能看到的像素数」。因为向上取 2 的幂，实际代理通常比屏幕更小。

### 1.1 为什么需要 effectiveScenePixels（v0.3.3）

v2 直接用 `FILTER_PROC_VIDEO::scene->width/height`。但 `SCENE_INFO` 描述的是
**当前正在渲染的场景**，而 PV 工程常把立绘放在超大子场景里再缩小合成进主画面：

| 场景 | 分辨率 |
| --- | --- |
| 主合成 | 2048×1080（2.21 MPx） |
| 子场景 `寺倾花` / `南宫颜` | 10000×6250（62.5 MPx） |
| 子场景 `枫叶` / `星海` | 3000×2000（6.0 MPx） |

素材正好有 8 张 10000×6250（62.5 MPx）。v2 算出 `need = sqrt(62.5/62.5) = 1.00 → div=1`，
**最重的 8 张图一张都没被代理**，每帧重新解码 62.5 MPx 原图。真机遥测：

```
L0:gate=0 keep=0 auto1=754   (69.6% 的调用是 AUTO 自己判 div=1)
fast=28.4%  decode_avg=56ms  decode_max=313ms  div[1]=70.9%
```

`div≥2` 帧占 29.1%，与 `fast=28.4%` 几乎吻合 —— 证明只要 div≥2 就能命中快路径。

### 1.2 v3 规则

1. `origin_id != id` → 该对象位于**被合成的子场景**，其画布不是最终画面。
2. 子场景，或画布 > ~12.44 MPx（超大工作画布）→ 把有效屏幕钳到
   **1080p 级预览预算 ~2.77 MPx**（`2048×1080×1.25`）。
3. **1080p / 4K 主合成完全不钳制** —— 逐项复算确认 v2/v3 在 2048×1080 与
   3840×2160 主场景下的 `div` 完全一致，v2 刻意的「4K 更清晰」保持不变。
4. 绝对兜底：`srcPixels ≥ 16 MPx` 时，无论如何至少 `div=2`。

用该工程 23 个真实素材复算（子场景 10000×6250）：

| 素材 | MPx | v2 div | v3 div |
| --- | --- | --- | --- |
| 10000×6250 ×8 | 62.5 | **1**（零代理） | 8 |
| 9455×6212 | 58.7 | 1 | 8 |
| 5651×6250 | 35.3 | 1 | 4 |
| 5661×2912 | 16.5 | 1 | 4 |
| 4501×2314 | 10.4 | 1 | 2 |

手动档位（1/2 … 1/32）、`このレイヤーは元画像を維持` 与导出闸门均绕过 AUTO，
导出始终强制 L0，画质不受影响。

实测（12 层 7000×8000，VramCacheSize=1024）：`div=1` ~1216 ms/帧，`div=4` ~64 ms，`div=8` ~19 ms。

## 2. 渲染路径与快路径

`func_proc_video()` 分两条路径：

- **慢路径**：首帧为某个 `(文件, div)` 建代理，或 L0 / 导出 / 「本图层保持原图」。此时向宿主 `get_image_file_cache()` 取完整原图，降采样后写入 `CACHE_HANDLE` 与磁盘 PNG。
- **快路径**：`g_src_dims` 记住该文件的原图尺寸，`FindProxy()` 命中已建好的代理后**完全不索取原图**，直接用缓存尺寸绘制；只有纹理被拒等极少数情况才通过 `LazyL0()` 惰性补解码。

隔离沙箱遥测（12 层 7000×8000，AUTO div=8）：

```
EXIT calls=36 win=83.11s fast=35 slow=1
filter_avg=13.050ms  decode_avg=95.563ms  decode_max=95.6ms
div[8]=36
```

97.2% 的调用跳过原图解码，而唯一一次解码花了 95.6 ms。修复前这是每个大图对象每帧都要付的代价 —— 这正是「合成基准 19 ms」与真实多场景工程「只快一点点」的落差来源。稳态帧时间 14–24 ms。

遥测文件 `ProxyLayer-perf.log` 写在 DLL 同目录，每 60 次调用一行，卸载时强制刷盘一行；格式说明见 `tools/read_perf.py`。

## 3. 全工程遍历

`EDIT_SECTION` 只对应**当前场景**。一个 21 场景工程里，主合成可能一个 `画像ファイル` 都没有，114 个图像对象全在别的场景 —— 只转当前场景会得到 `converted 0 object(s)`，这曾是真机上最容易被误判为「插件失效」的现象。

解决：

```
enum_scene_name()  -> 场景 ID 列表
当前场景先用 call_edit_section_param() 转换（兜底）
for 其它场景: select_scene(id) + call_edit_section_param()
select_scene(original) 还原用户所在场景
```

`plugin2.h` 明确：`select_scene()` 在参照锁/编辑锁状态下不可用。因此整段必须在 UI 线程、**任何 `call_edit_section_param()` 回调之外**执行：菜单回调只 `PostMessage(kMsgAutoProxyAll)`，由隐藏消息窗口的 `WM_TIMER` 驱动实际遍历。

## 4. 编辑节 API 必须主线程调用（踩坑记录）

`plugin2.h:731` 写明 *コールバック関数はメインスレッドから呼ばれます*。早期版本用 `std::thread` 后台 watcher 调 `call_edit_section_param()`，在真实安装上**每次启动约 2 秒必崩**：

- 4 份 minidump 完全一致：`0xc0000005`、`aviutl2.exe+0x215c50`、读地址 `0x888`。
- 反汇编该处：`cmp byte ptr [rcx+0x888], al; setne al; ret` —— 宿主在空编辑节对象上取状态位。
- 栈：`std::thread::_Invoke -> WatchWorker+0x6f -> aviutl2.exe+0x215c50`（用 `link /MAP` 符号表定位）。

修复：删除后台 watcher，改为 UI 线程隐藏窗口 + 2 秒 `WM_TIMER`；所有编辑节调用经唯一网关 `CallEditSection()`，非 UI 线程一律 `PostMessage` 转发（5 秒超时）。`tools/dump_stack.py` 是当时写的纯标准库 minidump 分析器。

另有一条相关语义：`find_object(layer, frame)` 返回**该帧及之后**的第一个对象，不是「覆盖该帧的对象」。据此把逐帧扫描改成按 `get_object_layer_frame().end + 1` 跳跃枚举，避免 21 个场景 × 200 层 × 20000 帧的宿主调用把 UI 卡死。

## 5. 跨图层取样保护

体积光、体积雾、3D 桥接等脚本通过 `obj.load("layer", ...)` / `getpixeldata()` 读取**别的图层**的真实像素缓冲，光束范围由缓冲的 `pw/ph` 推导。代理纹理是 1/div，所以无论 `OBJECT_INFO` 写回还是 LuaJIT 代理都无法补偿**缓冲尺寸** —— 唯一正确做法是让被取样的图层保持全尺寸。（这只针对缓冲；对 `obj.w/h` 这个**数值**的读取，LuaJIT 代理是有效的，见下节。）

检测按**参数语义**而非脚本名黑名单：扫描 `光源レイヤー` / `対象レイヤー指定` / `target_layer` / `source_layer` 等条目，把数值引用的图层（同时按 1-based UI 与 0-based SDK 编号）标记，给这些图层上已有的 ProxyLayer 置 `本图层保持原图=1`。新增第三方脚本因此自动被覆盖。

### 5.1 同对象绝对像素锚点保护（v0.3.4）

AUTO v3 修好性能后，3.63 MPx 级素材第一次真正被降采样，暴露出第二条几何耦合：
`SoftAnchorBone.anm2` 的 `bone` 是 **6 个绝对像素坐标**，脚本用 `obj.w/obj.h` 与这些常量
共同生成 `drawpoly()` 网格（`w2 = w/2`、`y0 = -h2 + h*yy/iyN`）。纹理缩小后常量与缓冲区
不再匹配，连 `obj.w/h` 补偿也救不回来 —— 必须让该对象保持全分辨率。

真机工程的两个后发素材恰好跨在阈值两侧，形成干净的 A/B：

| 图层 | 素材 | need | div | 结果 |
| --- | --- | --- | --- | --- |
| 寺倾花 后发 | 1724×2104（3.63 MPx） | 1.15 | 2 | 被代理 → 错位 |
| 南宫颜 后发 | 628×856（0.54 MPx） | 0.44 | 1 | L0 → 正常 |

检测按**值形状**而非脚本名：恰好 6 个数值、同时含 ≤ −64 与 ≥ +64 成员的绝对坐标元组。
先查 `bone` / `anchors` / `アンカー` 一类条目名，再用 `get_object_alias()` 扫描所有键值
兜底第三方脚本（跳过 `display.*`，并要求存在正数以排除 `display.camera=<负,...,0,0,0>`）。
命中的对象逐对象置 `このレイヤーは元画像を維持=1`，不影响调色/抠图等普通效果。

### 5.2 LuaProxy 单维度映射的碰撞与 veto（v0.3.4）

`luaJIT.dll` 转发的 `lua_setfield` 钩子每次只看见**一个**维度，因此
`ProxyLayer_MapSize(cur, isW, out)` 原本只按数值匹配、无对象身份。真机出现唯一一处碰撞：

```
5140x1712 @ div=2 -> proxy 2570x856   注册 (856 -> 1712)
南宫颜后发 628x856 @ div=1           宿主写 obj.h = 856
=> MapSize(856, "h") 误返回 1712，h2 从 428 变 856，网格上移约 428 px
```

因为 `5140×1712` 素材属于 2048×1080 的主歌/副歌场景，这条映射**只在主合成渲染时**才注册，
所以单独打开南宫颜次合成正常、主合成里向上平移。全工程 57 个代理物件 × 13 个 L0 物件
逐项复算，碰撞恰好只有这一处。

修复：新增全分辨率尺寸登记表（`RegisterNativeSize`，30 s TTL 防工程切换残留）。
每个映射带 `allowW` / `allowH`，与已登记原生尺寸冲突的那一侧被禁用；原生尺寸晚于映射
登记时回溯禁用。另一侧补偿照常（`obj.w=2570 → 5140`、`1250 → 10000` 均保留）。
修复位于 `ProxyLayer.aux2` 导出的 `ProxyLayer_MapSize()`，**无需重编译 `luaJIT.dll`**。
遥测 `veto=` 只统计「登记映射时」拦截的次数；若原生尺寸晚于映射登记，回溯禁用路径同样生效但不计数，
所以 `veto=0` 不能当作修复未生效的判据。

## 6. 时间轴物件名

`画像ファイル` 的「标准名」就是文件名；`FLAG_INPUT` 滤镜的标准名是效果名 `ProxyLayer`。SDK 约定 `get_object_name()` 返回 `nullptr` 表示标准名，`set_object_name(o, nullptr)` 恢复标准名。

因此转换后用 `EnsureDisplayName()` 把物件名设回源文件名（幂等，用户自定义名绝不覆盖），点 OFF 还原时若当前名仍是该文件名就清空，让宿主的 `画像ファイル` 重新显示文件名。补名计入 `changed` 以正确标记工程 dirty。

## 7. 导出闸门

`EDIT_STATE_SAVE` 或模拟导出态 + `导出时强制原图` → `mode=0, div=1`，走完整原图路径。真机导出验证：闸门开启的导出帧与 L0 金标准 **0.00% 像素差**；QSVEnc MP4 正常。`src/ExportProbe.cpp` 用官方 `EDIT_HANDLE::output_file()` 直启真实导出，绕开 F9 对话框，用于自动化测试。

## 8. 其它已证伪的路线

- **原生 `OBJECT_INFO` 写回 `obj.w/h`**：Lua 侧读到的仍是纹理尺寸，无效。对应的 `补偿 obj.w/h (实验)` 参数项、`DrawProxy()` 的 `compensate` 分支与 `OBJCOMP` 日志已在 **v0.2.1 移除**（`.aup2` 参数按名字保存，删项不会让旧工程参数错位）。
  - **注意区分**：可选的 `luaJIT.dll` 转发代理**不是**这条。它拦截宿主写入 `obj.w/h` 的 `lua_setfield` 并映射回原尺寸，A/B 实测 1024 → 2048 有效。OpenGlow / 光束_H 等按 `obj.w/h` 算作用范围的脚本依赖它，**必须保留**；主插件的 `ProxyLayer_MapSize` 导出与 `RegisterSizeMap()` 也因它而保留。
- **修改 Lua metatable / 在效果链间传递补偿值**：Lua 探针三次证伪。
- **mode 5（只在首帧上传纹理）**：宿主每帧重建对象图像，第 2 帧起全空白，已移除。
- **CPU 拷贝优化**：实测无收益。
- **盲目提高 div**：AUTO 在 40 MPx 素材上已达上限 8（代理比屏幕还小），再压只糊不快。

## 9. 本地化与显示文本（`.aul2`）

**v0.3 起内部键名统一为日语**（AviUtl2 默认语言 `Default`）。这是与参考仓库 VolumetricLight3D 一致的社区惯例：插件编译进去的 `FILTER_ITEM_*` 名字就是宿主默认语言，其他语言靠 `.aul2` 差异包把显示名翻过去。

- 宿主按当前语言加载 `Language/<语言名>.*.aul2` 差异文件；真机语言列表只有 `Default` / `简体中文` / `English` / `Deutsch` / `community_*`，**没有「日本語」** —— `Default` 就是日语，且只读取 `Default.*.aul2`。官方规则写在宿主自带 `简体中文.aul2` 的头部注释里。
- 当前三个语言包：`Default.ProxyLayer.aul2`（只放日语 `[Tips.ProxyLayer]` 悬浮提示，默认语言无需翻译显示名）、`简体中文.*` 与 `English.*`（显示名 + `[Panel]` + TIPS）。
- **v0.3.1**：删掉参数面板顶部那行 `FILTER_ITEM_SEPARATOR` 长提示，改用官方 `[Tips.ProxyLayer]` 悬浮气泡（规则见宿主 `简体中文.aul2` 头注释 / docs.aviutl2.jp）。`effect.name` 写效果说明，其余 12 个键写参数说明，值里 `\n` 换行；真机沙箱实测中文气泡正常弹出。
- **实测**（隔离沙箱，三轮）：`日本語.ProxyLayer.aul2` 放在 `Language=`（Default）下完全不生效，参数仍显示旧中文；同一文件改名 `Default.ProxyLayer.aul2` 后 `档位→プロキシ倍率` 等全部生效；三包并存且 `Language=简体中文` 时中文包生效、不被 Default 日文包污染。这促成了"直接把内部键改成日语、不再需要日文包"的决定。
- `.aul2` 只改**显示**，不改 `.aup2` 存储键。键必须是源码字符串的逐字拷贝；差一个字符静默回退，所以 `tools/check_aul2.py` 从 `src/ProxyLayer.cpp` 解析词条并在 CI 校验覆盖率（当前 12 键 × 2 包）。
- **绝不翻译内部名 `ProxyLayer`**：`create_effect()` / `get_effect_name()` 以及 9 处 `wcscmp(..., L"ProxyLayer")` 依赖它；效果名与菜单根名固定英语。
- **菜单 / 设置窗口跟随语言**：导出 `InitializeConfig(CONFIG_HANDLE*)`（SDK 保证在 `InitializePlugin()` 之前调用）保存句柄，用 `get_language_text(handle, L"Panel", key)` 读取各语言包的 `[Panel]` 段；缺包或缺键时回退日语。菜单在启动时注册，所以切换语言需重开宿主；面板每次显示时重新取，立即生效。

> **破坏性变更**：v0.2.x 保存的转换态工程含中文键（`档位` / `原始宽` / `原始效果名` / `原始文件名项`），v0.3 不再识别。元数据丢失会让 OFF 还原退回 `画像ファイル`（音频 / 3D 对象可能被错误还原）。因为当时还没有真实用户工程保存转换态，未在 DLL 内做旧键别名兼容；升级前先 OFF 还原或重新转换。

词条清单与新增语言流程见 [`LOCALIZATION.md`](LOCALIZATION.md)。早期 v0.2.1 的三语包评估存档见 [`REVIEW.md`](REVIEW.md)。

## 10. 半透明与 L0 路径（v0.3.2 两个 bug）

### 10.1 alpha 被乘两次

`input2.h` 只给 PA64 / HF64 标了「乗算済みα」，`filter2.h` 则给 `INPUT_PIXEL_FORMAT::RGBA` 标了
`DXGI_FORMAT_R8G8B8A8_UNORM ※PIXEL_RGBA` —— 与 `get_image_data()` / `set_image_data()` 的
`PIXEL_RGBA` 是同一个结构。早期实现把 8bit RGBA / BGRA 当成 **straight alpha**，读像素时做了
`RGB × alpha` 转成 premultiplied，再交给 `set_image_data()`；宿主内部本来就是 premultiplied，
于是半透明像素被乘两次，50% alpha 的青色只剩下约 0.56 倍亮度。之前的几何 A/B 与导出闸门
验收都用**不透明**网格图，所以 0 像素差的设计掩盖了这个 bug。

修法：读取时只做格式转换（BGRA 的 R/B 交换、PA64/HF64 的 16→8），**绝不改 alpha 语义**；
`Downsample()` 继续在宿主 premultiplied 空间做线性 box average（对半透明边缘是正确做法，
不需要 un-premultiply 往返）。`ToStraightRGBA8()` 只在写磁盘 PNG 时做 un-premultiply。

### 10.2 L0 不是直通，而是全尺寸重绘

`FLAG_INPUT` 媒体对象效果必须自己把图像交给 `set_image_data()`；不调用就没有图像，而
`func_proc_video()` 返回 `false` 会**中断后续滤镜与输出**（filter2.h 注释明确）。所以
`div=1` / 导出闸门 / 单层保持原图时的 L0 路径无法"什么都不做"，只能重绘原图。

早期 `FallbackL0()` 逐像素调用 `ReadPremul()` 并分配一份全尺寸副本：对 10000×6250 素材就是
62.5M 次调用 + 250 MB 分配，每个对象每帧一次。真机 `ProxyLayer-perf.log` 显示
`filter_avg≈300ms`（其中解码 72ms，其余约 230ms 全是这次转换）、`fast=0~40%`、
`div[1]` 占 70–100% —— 这解释了"代理开了却没变流畅，甚至更慢"。

修法：`CACHE_FILE_IMAGE::format == RGBA` 且 `pitch == width*4` 时，把宿主缓存 buffer
**直接**传给 `set_image_data()`（零拷贝，缓存引用在函数返回前有效，而 `set_image_data` 是同步上传）；
带 padding 时逐行 `memcpy`；只有 16bit / BGRA 等格式才逐像素转换。

### 10.3 仍然存在的开销

L0 每帧仍要 `get_image_file_cache()`，62.5 MPx 的 PNG 解码约 56 ms（峰值 313 ms），
日志里 `open media file` 反复出现说明宿主图像缓存被这些超大图挤爆。

**v0.3.3 已治本**：`L0:gate/keep/auto1` 计数证实这些 L0 并非导出闸门或手动保持原图，
而是 AUTO 在超大子场景里误判 `div=1`（见第 1 节）。AUTO v3 把子场景的有效屏幕钳到
预览预算后，大图在稳态下应 100% 命中快路径，不再每帧解码原图。
`ProxyLayer-perf.log` 另有 `cap/sub/big/maxw/d1mp` 用于验证修复是否命中：
健康状态下 `cap` 与 `sub` 应接近子场景帧数，`div[1]` 与 `decode_avg` 明显下降，
`d1mp` 只剩下真正的小图。

> 曾经考虑过「在转换阶段跳过 AUTO 判 `div=1` 的对象，让它保持原生 `画像ファイル`」。
> 真机数据推翻了这个方向：这些 `div=1` 帧里恰恰是 **62.5 MPx 的最大素材**，
> 跳过只会让它们绕过代理、继续全分辨率渲染。问题在判据，不在「该不该转换」。
