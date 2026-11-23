# Changelog

## v0.3.8 — 2026-11-23（P3-1：设置面板自选转码后端 + 工具链随包分发）

承接上一版的「解除 QSVEnc 绑定」：把后端选择从命令行搬进**插件设置面板**，
并把离线工具链正式随 Release 分发。

### 新增

- **设置面板新增「转码后端」下拉**：`auto` / `QSVEncC` / `NVEncC` /
  `ffmpeg` / `custom`。选择即刻写入全局配置 `proxy-encoder.txt`
  （DLL 同目录，跨工程生效，不影响自动代理开关）。
- **「测试」按钮**：按所选后端探测 exe（环境变量 → PATH → 已知安装路径），
  启动 `--version` 并在面板显示「可用 / 未找到 + 路径 + 退出码」。
  **只做可用性探测，不转码、不改工程、不生成文件。**
- 路径覆盖：`PROXYLAYER_QSVENCC` / `PROXYLAYER_NVENCC` / `PROXYLAYER_FFMPEG` /
  `PROXYLAYER_ENCODER_EXE`。
- 语言包（Default / English / 简体中文）新增 `[Panel]` 4 键
  `encoder / test / encoder_ok / encoder_missing`，`tools/check_aul2.py` 同步
  为 **16 键全包强制检查**（CI 已验证 3 包 0 failure）。
- **工具链随包分发**：`Tools/make_video_proxy.py` 与
  `Tools/test_encoder_backends.py` 打进 `.au2pkg.zip` 与 `Installer.zip`，
  用户不必从源码仓库取脚本；脚本支持前述四种后端与 `--dry-run` /
  `list-backends` / 自定义命令模板。

### 明确未做（P3-2，下一阶段）

- **插件内在线转码与在线改工程**：目前仍需用 `Tools/` 里的脚本离线生成代理 +
  派生工程，再打开派生工程。原因是后台转码线程、编辑锁与「宿主丢弃未知
  `.aup2` item」三件事需要单独设计与真机验证，不与本次面板改动混做。
- 面板的下拉只决定**配套工具**用哪个后端，不会自动对当前工程生成代理；
  「生成视频代理」按钮尚未加入。
- 图片代理路径、A′ 导出硬闸、语言包既有键、ini 均未改动。

### 验证

- 本地：`check_aul2.py` 3 包 16/16 键 0 failure；`py_compile` 全部工具脚本通过；
  `build_release.py --tag v0.3.8` 两个 zip 均含 `Tools/` 与全部语言包；
  `test_encoder_backends.py` 在仓库原位 18 项自检 ALL PASS。
- 上一版已验证的零回归结论继续成立：默认 `auto` 在本机仍选中 QSVEncC，
  生成的代理与 P1 实测文件逐字节一致（SHA256 `233fe9ca…82b`）。

## v0.3.7 — 2026-11-21（P2 / A′：视频代理的自动无损导出硬闸）

### 要解决的问题

P1 的视频代理是离线脚本改进工程：把 `動画ファイル` 的 `ファイル` 指向 1080p 代理，
并把 `拡大率` ×2 补偿尺寸。预览因此提速 56%，但导出时若沿用代理，画质就受损；
若直接切回 4K 原片而 `拡大率` 仍是 ×2，画面又会放大两倍。

**为什么图片代理不需要这套机制**：图片代理是「渲染时换数据源」——工程零改动，
滤镜逐帧读 `get_edit_state()` 就能回 L0 原图，一行 `if (saving) div = 1;` 即可。
视频代理是「工程里换了文件」，必须把工程参数写回；而输出中 `call_edit_section()`
会失败（`plugin2.h:734`），所以只能抢在输出开始前的时间窗。

### A′ 方案

真机探针（`host-test/p2-probe/P2_PROBE_REPORT.md`）证明：F9 的 `WM_KEYDOWN` 到达
UI 线程时宿主仍是 `EDIT_STATE_EDIT(0)`，`call_edit_section()` 返回 1、回调执行、
`EDIT_SECTION::info` 可用。于是：

```
RegisterPlugin → 线程级 WH_GETMESSAGE 钩子
  └─ VK_F9 → 全场景扫描 → 还原 ファイル / 拡大率 → set_edited_state()
      → 用户按 Enter 导出（工程已是原素材 + 原倍率）
      → CHANGE_EDIT_STATE(SAVE→EDIT) → WM_TIMER 自动切回代理
      → 取消导出：120 s 超时回滚
```

**不需要自定义输入插件**，`register_input_plugin()` / `lwinput` 转发 / VFR 帧映射
等原 P2 设想全部绕开 —— 导出读的就是原始文件，音频、色彩标签、旋转矩阵、
`mebx` 元数据轨全部自动无损。

### 关键坑：宿主丢弃未知 .aup2 item

第二轮诊断日志 `objects=1 video=1 file=1 scale=1` 但 `metaFile=0 metaScale=0`
证明宿主加载工程时**丢弃了 P1 写进效果块的 `元の動画ファイル` / `元の拡大率`**，
`get_effect_item_value()` 永远读不到。改为在 edit 回调里用
`PROJECT_FILE::get_project_file_path()`（`plugin2.h:1001`）拿到工程路径，
直接解析 UTF-8 `.aup2` 文本（`[scene.N]` / `[N]` / `[N.M]`），按 `(layer, frame)`
用 `find_object()` 定位对象写回。拿到的是用户真实原素材路径。

### 真机验证（AviUtl2 2.1.18b，5 轮，每轮 DLL 均逐字节恢复）

| 轮次 | 结果 |
| --- | --- |
| 1 | `found=0`，原因未知 → 不猜，先加诊断日志 |
| 2 | 定位为宿主丢弃未知 item |
| 3 | 工程文本解析：`found=1 changed=1`（16 ms） |
| 4 | 写后读回：`verified=1 mismatch=0 changed=1` |
| 5 | **3 帧短工程双变体真导出，两个 AVI 逐字节一致** |

第五轮决定性数据（内置 `AVIファイル出力`，`output_file()` headless 触发）：

```
control    sha256=1512727C2FA0CD3532FE99BB16ABD6D86175B374055A40042BB97CAE57A31FF60  size=18675468
experiment sha256=1512727C2FA0CD3532FE99BB16ABD6D86175B374055A40042BB97CAE57A31FF60  size=18675468
BYTE-IDENTICAL
```

同时验证：对照组 `found=0`（F9 完全 no-op）；实验组
`output_file -> 1` → `EDIT_STATE_SAVE observed` → `re-apply verified=1 mismatch=0`
→ `re-applied proxy on 1 object(s)`（输出结束自动切回代理）。

### 已知限制（不宣称全部场景完成）

- 菜单点「ファイル出力」的路径未覆盖（`WH_GETMESSAGE` 抓不到 `WM_COMMAND`，需补
  `WH_CALLWNDPROC`）；目前覆盖 F9。
- 全量 308 个对象的批量写入耗时、撤销栈合并、多场景切换尚未在 `aps.aup2` 上实测；
  本次用 1 对象 / 1 场景的 3 帧短工程做逐字节验证。
- 若用户在宿主里改过工程但未保存，磁盘文本与内存状态可能不同步。
- 切回代理依赖进程内缓存；宿主中途重启会停在「原素材」状态（安全，但需重新转换）。
- 图片代理路径、语言包、ini 均未改动。

## v0.3.6 — 2026-11-18（两个真机 bugfix：Default 面板文案、OFF 还原链接损坏）

用户报告两个问题，都在真机复现并有决定性证据：

### ① Default（日语）环境下设置面板只显示占位符

截图显示窗口标题 `title`、复选框 `switch`、状态 `status`、按钮 `ok` / `cancel` ——
全是 ASCII 键名，而不是日语。

**根因**：SDK `config2.h:36` 明确规定
`get_language_text()` 在键未定义时返回**参数 text 的指针本身**，不是 `nullptr`：

```
戻り値 : 定義されているテキストへのポインタ
         (未定義の場合は引数のテキストのポインタが返却されます)
```

`Default.ProxyLayer.aul2` 只有 `[Tips.ProxyLayer]`，刻意没有 `[Panel]` 段
（Default 就是日语，显示名无需翻译）。于是 `T()` 里旧的 `if (t && *t) return t;`
把键名当成译文返回，C++ 里写好的**日语硬编码回退永远走不到**。
中文 / 英文包因为自带 `[Panel]` 段，一直正常，所以这个 bug 只在 Default 下暴露。

修复（两层，冗余保险）：

- `T()` 同时按**指针身份**和**内容**判定「返回值 == key」即视为未定义，回退日语。
  这样即便某个第三方语言包缺键、或语言包未更新，也不会再把键名显示出来。
- **`Default.ProxyLayer.aul2` 新增 `[Panel]` 日语段**（12 键，文案与 C++ 回退逐字一致），
  让宿主直接查到日语译文。`tools/check_aul2.py` 相应改为**对所有包（含 Default）强制检查
  `[Panel]` 12 键**，缺失判为 failure 而非 warning，防止回归。

### ② 打开开关转换、关掉开关还原后，图像文件链接损坏

用户提供的 `aps.aup2` 里，还原后的 `effect.name=画像ファイル` 正确回来了，
但 `ファイル=` 成了乱码（`\x08bip\x7f`、`q8*\x14\x02` 等堆内存字节），共 5 处。
GitHub diff 同时显示时间轴物件名 `Arona_02.png` 被正确清除 —— 一正一反正好锁定位置。

**根因**：SDK `plugin2.h:542` 明确规定 `get_effect_item_value()` 返回的 `LPCSTR` 有效期：

```
※次に同一スレッドで文字列返却の関数を使うかコールバック処理の終了まで有効
```

OFF 还原分支取到 `v = get_effect_item_value(list[i], L"画像ファイル")` 后，
又连续调用两次 `get_effect_item_value`（读 `元のエフェクト名` / `元のファイル項目名`）、
再 `create_effect()`，最后才把 `v` 传给 `set_effect_item_value(ne, ..., v)` ——
此时 `v` 指向的宿主临时缓冲早已被复用，写进工程的自然是被覆盖后的堆字节。
`base`（时间轴文件名）因为在 `v` 刚取得时就立即算好，所以不受影响，这解释了 diff 的不对称。

**系统性修复**（不只补一处）：新增 `CopyItem()` / `CopyEffectName()` / `CopyObjectName()`
三个 owning-copy helper，把全文件里所有跨 SDK 调用使用的字符串返回值都改为**取得即复制**：

| 位置 | 旧问题 |
| --- | --- |
| `ConvertObject` OFF（本次 bug） | `v` 跨 2 次读取 + `create_effect` 后使用 |
| `ConvertObject` ON | `nm` 在 `ReadSourcePath()` 之后仍用于白名单判定 / 写元数据 |
| `SafetyCheckThunk` | `元の幅` 与 `元の高さ` 连取两个裸指针，第二次让第一个失效 |
| `ObjectHasAbsoluteAnchor` / `CollectProtectedLayers` | `en` / `nm` / `v` 在循环内多次读取后复用 |
| `EnsureDisplayName` / `SetKeepOriginal` / `RegisterNativeImageSize` | 同类短生命周期指针 |

现在这类 bug 由 helper 在结构上不可能发生。`画像ファイル` 效果名与
`元のエフェクト名` / `元のファイル項目名` 元数据格式未变，旧工程兼容。

**验证**：`ProxyLayer.aux2` 重编译（141K，9 个导出齐全，含 `ProxyLayer_MapSizePair` /
`InitializeConfig`）；真机隔离 runtime 跑 ON→OFF 往返 + 保存，逐字节比对 `ファイル=`。
`luaJIT.dll` 转发代理**未改动**，无需替换。

> ⚠️ 已损坏的工程无法自动修复：乱码字节写进 `.aup2` 后原路径信息已丢失。
> 需要从备份 / Git 历史恢复，或手动重新指定素材。v0.3.6 只保证**之后**的还原不再损坏。

## 文档补充 — 2026-11-18（无代码改动，DLL 不变）

用户实测发现：`OpenGlow` / `光束_H` 在参数完全相同时，**代理态与原图态的预览有细微差异**
（光晕扩散范围、光束长度不同；但已不再是早期「只有中心亮一小块」的老问题）。
只读排查后确认这**不是 bug、也不是 `obj.w/h` 补偿失效**：

- 日志同时存在 `pair(1563,1126) MATCH -> (6251,4501)` 与 `pair(6251,4501) no-match`，
  证明两种状态下脚本读到的 `obj.w/obj.h` **数值完全一致**，补偿确实生效。
- 差异来自**真实像素缓冲**：代理态 object 缓冲只有 1/div 像素，而 LuaProxy 只能修数值。
  `光束_H` L421 `pixelshader("ps_lb_pool", ..., "object", {w0,h0})` 的 `src.Load()` 越界补零、
  L565 `領域拡張` 后重读的 `W,H` 基准不同 → 光束长度变化；`OpenGlow` L232/L236 的重读与
  L319 的金字塔同理 → 光晕范围变化。
- **输出无损**（导出闸门强制 L0，缓冲恢复全尺寸）；差异只在预览编辑期。
- 需要预览完全一致时，给该层勾 `このレイヤーは元画像を維持`（中文「本图层保持原图」）。
- 现有自动保护只覆盖跨图层取样与绝对像素锚点；读自身缓冲的脚本写法太杂，
  自动一刀切会误伤调色 / 抠图，故**保持现状、文档告知**（与用户一致决定「先放着」）。

同步更新：三语 README「已知限制」新增专节与表格、`docs/TECHNICAL.md` 新增 §5.3、
安装包内 `README-INSTALL.txt` 三语注意事项。**此项仅为文档，v0.3.5 的 DLL 与语言包均无需更换。**

### 同日：次合成预渲染进入设计 / 归因阶段（仍无代码改动）

按用户决定「先放着」预览期的光效差异后，开始推进 HitFilm 式 comp 预渲染。
本轮完成**只读归因 + 技术设计**，不写插件代码、不改用户工程：

- 重新严格分段 VERBOSE 日志（只取连续播放段）：寄明月 PV 主合成
  **1608 ms/帧**（11/18 02:26，93 帧），单个 10000×6250 立绘次合成 ~260 ms/帧，
  Proxy 沙盒 10.4 ms/帧。插件 `filter_avg≈2.6 ms`，只占整帧 **0.18%**。
  更正旧报告 §10.24 引用的 1415 ms —— 那批混了单帧跳转，以 1608 ms 为准。
- **决定性结构发现**：只读解析引用图，主合成每帧 8 个 `シーン` 引用，
  每个一级子场景又各引用 4 个基础场景（含 2 个 62.5 MPx 画布），
  即每帧约 **32 次子场景展开 / 16 次大画布重绘**；主合成本身几乎没有效果链，
  LyricMotioN / ドロップシャドウ / 発光 全在被引用的一级子场景里。
  「效果链」与「次合成重绘」不是两个并列嫌疑，而是同一条链的两段。
- 派生 4 组受控 A/B 对照工程（`A-baseline` 与用户原工程逐字节相同，
  `B/C/D` 只加 `effect.disable=1`），放在本地 `host-test/prerender-ab/`，
  **不写回原工程、不进仓库**；用于下一步坐实因果，验收门槛 `A−B ≥ 70%×A`。
- 新增 [`docs/PRERENDER.md`](docs/PRERENDER.md)：HitFilm 四段契约对照、
  为何不做视频文件、跨帧缓存 + 递归内容指纹架构、SDK 零件逐个核对的签名、
  7 条风险（首位是 `select_scene` 改编辑场景 / 撤销栈，需复用 UI 线程网关）、
  P0–P4 里程碑与逐像素验收条件、6 个未决问题。
- `global.PQ_LEVEL` **归档不实现**：AUTO 侧已无空间，全局档位会重新打开
  刚修好的尺寸 / 锚点兼容面，且与「无感」目标相反。

## v0.3.5 — 2026-11-17（LuaProxy 改成对 (w,h) 匹配：彻底消除尺寸误映射）

用户重启后确认：**寺倾花后发已修好，南宫颜后发在主合成仍向上平移**。日志给出了决定性证据：

```
10:43 会话：628x856 在 ACTIVE/PROBE 中出现 0 次
           keep=0、veto=0、"layer opted out" 0 次
           但有 5 条 "absolute-pixel anchor detected"
```

- **v0.3.4 的实现漏洞**：自动保护对命中的对象**只调 `SetKeepOriginal()` 然后 `return`**，跳过了
  `ConvertObject()`。而 `SetKeepOriginal()` 只在对象已有 ProxyLayer 效果时才有效 —— 于是这些对象
  **保持原生 `画像ファイル`，完全不进插件渲染路径**。寺倾花因此修好（根本没被缩小），但：
  `RegisterNativeSize()` 挂在渲染路径上，从未为 `628x856` 调用 → **veto 机制完全没机会执行**。
  而 `luaJIT.dll` 的 hook 是**全局**的（拦所有对象的 `obj.w/h`），主合成里 `5140x1712 @div=2`
  照常注册 `proxyH=856 → srcH=1712`，南宫颜后发写 `obj.h=856` 时仍被误映射 → 后发上移。
  次合成没渲染那个 5140x1712 素材，所以正常。
- **治本修复（改 DLL，用户明确同意）**：`ProxyLuaJIT.cpp` 不再按**单维度数值**匹配。
  hook 在 `obj.w` 到来时缓冲原始 w（按线程存于 `TlsAlloc`，避免动态加载 DLL 的隐式 TLS 崩溃风险），
  等 `obj.h` 到来时用**完整 `(w,h)` 对**查询新导出 `ProxyLayer_MapSizePair()`；两个维度都必须相等才补偿。
  命中时先用真实 `lua_setfield` 修正已提交的 `w` 字段，再替换栈顶的 `h` 值。
  单维度的尺寸巧合从此不可能误伤：`(628,856)` 与代理 `(2570,856)` 只共享 h，不再匹配。
  - 保留 `ProxyLayer_MapSize()` 供旧版 `luaJIT.dll` 使用；新版 DLL 检测不到 `MapSizePair` 时自动回退到旧逻辑。
    因此**两个 DLL 可以分别升级**，任意组合都不会崩。
  - 项目 23 个真实素材复算：`(628,856)`/`(578,858)` 不命中任何代理对 → 保持原值；
    `(2570,856)→(5140,1712)`、`(1250,782)→(10000,6250)`、`(862,1052)→(1724,2104)` 等正常补偿，
    OpenGlow / 光束_H 不受影响。
- **插件侧同步修正**：
  1. 受保护对象现在**先 `ConvertObject()` 再 `SetKeepOriginal(true)`**（与体积光跨图层保护语义一致），
    使其进入渲染路径、强制 L0 全尺寸，并让 `RegisterNativeSize()` 真正登记其真实尺寸。
  2. 扫描阶段新增 `RegisterNativeImageSize()`：对所有**未转换**的图像对象登记真实尺寸（带路径缓存，
     只登记 `ファイル` 项、明确排除 ProxyLayer 效果、跳过音频/OBJ/视频），刷新 30 s TTL。
     这给旧版单维度 hook 一个精确的兜底 veto，不再依赖渲染顺序。
  3. 锚点检测放宽为 **4 / 6 / 8 个数值**的绝对坐标元组（原来只认 6 个），并按「键名前缀」
     排除 `display.*` / `frame` 等宿主状态项。工程 453 个效果实例复算仍只命中 5 个 `bone`，零误伤。
     日志现在输出**实际命中的效果/参数名**（`AutoProxy: absolute-pixel anchor detected in [SoftAnchorBone/bone]`），
     证明这是脚本无关的通用检测，不是脚本名黑名单。
- 遥测新增 `pair=`（成功成对补偿次数），`tools/read_perf.py` 支持五代日志格式。

**是否需要重编译 DLL**：是，而且**两个 DLL 都要更新**：
`dist/ProxyLayer.aux2`（137,216 B，MD5 `78b2b4ea…`）与 `dist/luaJIT.dll`（30,208 B，MD5 `dd09050c…`）。
`luaJIT_real.dll` 是官方原版，**不要动**（MD5 `4ace3ea0…`）。两者已同时部署到真机。
一键安装包 `ProxyLayer-v0.3.5-Installer.zip` 会自带新版代理 DLL；宿主更新覆盖 `luaJIT.dll` 后
重跑 `install.bat` 即可恢复。**升级后需重启 AviUtl2**，两个后发图层都应贴合。

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
