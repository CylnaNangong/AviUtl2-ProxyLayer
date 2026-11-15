# 实现细节与实测记录

本文从开发期的五篇报告提炼，已去掉本机路径与用户工程信息。

## 1. 档位决策（AUTO v2）

```
need = sqrt(srcPixels / scenePixels)
div  = 2^ceil(log2(need))，上限 8；need <= 1 时 div = 1
```

目标是「代理纹素数 ≈ 屏幕像素数」。因为向上取 2 的幂，实际代理通常比屏幕更小。

| 素材 | 场景 | need | div |
| --- | --- | --- | --- |
| 7000×8000 | 1920×1080 | 5.20 | 8 |
| 7000×8000 | 3840×2160 | 2.60 | 4 |
| 2048×2048 | 1920×1080 | 1.42 | 2 |
| 1920×1080 | 1920×1080 | 1.00 | 1 |

实测（12 层 7000×8000，VramCacheSize=1024）：`div=1` ~1216 ms/帧，`div=4` ~64 ms，`div=8` ~19 ms。导出闸门始终强制 `div=1`。

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
