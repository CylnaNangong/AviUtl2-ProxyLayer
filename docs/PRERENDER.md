# 次合成预渲染（Composite Pre-Render）技术设计

> 状态：**设计 / 待验证**，尚未实现，未改任何插件代码。
> 目标：复刻 HitFilm「编辑好一个 comp → 预渲染成可复用产物 → 一旦改动就失效」的体验，
> 解决 AviUtl2 里 `シーン` 引用**每帧层层重画、没有跨帧复用**的播放瓶颈。
> 关联：[`TECHNICAL.md`](TECHNICAL.md)（现有 ProxyLayer 实现）、
> 工作区报告 `AviUtl2_大图层瓶颈与次合成预渲染迁移评估.html`（HitFilm 契约考证）。

---

## 0. 一句话结论与边界

- **不做「预渲染成视频文件」**：AviUtl2 的 `シーン` 是宿主内建效果，插件无法替换它的取帧来源；
  引入外部编码器还会带来 alpha、色彩、帧精度与失效重写成本。
- **做「跨帧缓存 + 内容指纹失效」**：用官方 `CACHE_HANDLE` 把某个子场景在指定帧的渲染结果
  存成主存 `PIXEL_RGBA` 帧，后续帧/后续引用命中则直接复用；comp 内容一变，
  指纹变化 → 整段缓存失效 → 重新预渲染。语义与 HitFilm 一致，全程官方 API，不 hook 宿主。
- **导出硬闸**：`EDIT_STATE_SAVE` / `obj.getinfo("saving")` 时一律绕过缓存、回原场景渲染。
  HitFilm 的 *Use Proxies For Export* 是可选画质妥协，本设计**不照搬**，出片必须无损。
- **与 ProxyLayer 正交**：ProxyLayer 缩小的是「图像输入纹理」，缩小不了「场景画布本身」。
  本功能砍的是后者，两者互补，不互相替代。

---

## 1. 证据链（2026-11-18 真机实测，工程文件全程只读）

### 1.1 整帧耗时分层

宿主 `[VERBOSE] rendered video frame [frame](scene) X ms` 是 QPC 实测的**整帧**耗时。
`FileLogLevel=1` 临时开启采集，用完还原为 4。只取**连续播放段**（帧号递增、相邻 ≤5 s），
单帧跳转含缓存预热，不作为稳态结论。

| 会话 / 工程 | 连续播放段 | 帧耗时 median | 备注 |
| --- | --- | --- | --- |
| 11/18 02:26 · **寄明月 PV 主合成** | 02:26:49–02:29:27，frame 0→4691，93 帧 | **1608 ms** | 约 0.6 fps |
| 11/17 09:06 · 单个 10000×6250 次合成（立绘） | 5 段，20–51 帧 | **259–271 ms** | 只渲染立绘本身 |
| 11/18 02:40 · Proxy 沙盒（轻量） | 109 帧 | **10.4 ms** | 基线 |

同期 `ProxyLayer-perf.log`：`filter_avg ≈ 2.6 ms`（v0.3.2 为 55.6 ms），
即**插件只占整帧 0.18%**。继续压 `div` 对体感无意义，瓶颈已完全在宿主侧。

> 更正旧引用：此前报告 §10.24 写的「median 1415 ms」是 11/17 07:13 段；
> 本轮用同一份日志重新严格分段后，11/18 主合成段为 **1608 ms**。两者都是真数据，
> 但 1415 ms 那段混入了单帧跳转，本设计以 1608 ms 这个纯连续段为准。

### 1.2 决定性结构：引用的层层展开

只读解析 `PV工程.aup2` 的 `シーン` 引用图（36 个引用，全部 `再生速度=100.00`、`ループ再生=0`）：

```
主合成 scene.0 (2048x1080)  ── 8 个 シーン ──►  前奏/主歌1/副歌1/主歌2/间奏/尾奏
                                                 （副歌1 被引用 3 次，再生位置各异）
每个一级子场景              ── 4 个 シーン ──►  星海 / 枫叶 / 寺倾花(10000x6250) / 南宫颜(10000x6250)
```

| 场景 | 分辨率 | 效果实例 | `シーン` 引用 | LyricMotioN | ドロップシャドウ | 発光+グロー |
| --- | --- | --- | --- | --- | --- | --- |
| 主合成 | 2048×1080 | 20 | **8** | 0 | 0 | 0 |
| 南宫颜 / 寺倾花 | 10000×6250 | 14 / 21 | 0 | 0 | 0 | 0 |
| 场景 | 2048×1080 | 29 | 4 | 0 | 2 | 1 |
| 前奏 | 2048×1080 | 65 | 4 | 0 | 8 | 2 |
| 主歌1 / 主歌2 | 2048×1080 | 67 | 4 | 9 | 9 | 2 |
| 副歌1 | 2048×1080 | 74 | 4 | 8 | 8 | 2 |
| 间奏 / 尾奏 | 2048×1080 | 33 | 4 | 0 | 0 | 2 |

**两个此前没算清的事实**：

1. **主合成本身几乎没有效果链** —— 只有 8 个 `シーン` + 9 个 `標準描画` + 1 个 `周辺ボケ光量`
   + 1 组音频。LyricMotioN、ドロップシャドウ、発光 全在**被引用的一级子场景**里。
   所以「重效果链」与「次合成重绘」不是两个并列嫌疑，而是**同一条链的两段**：
   主合成慢，是因为它把那些重效果链场景整棵展开并重画。
2. **每帧展开约 32 次子场景渲染**：8 个一级引用 × 各 4 个二级引用；
   其中约 16 次命中 10000×6250（62.5 MPx）画布。8 个一级引用里还有 3 个指向同一子场景
   但 `再生位置` 不同（28.134 / 27.267 / 28.400 秒），**缓存键必须含帧号，不能按场景去重**。

帧时间也吻合这个结构：无引用的基础场景单帧 `星海 18 ms`、`副歌1 11 ms`、
`枫叶 51 ms`；带 4 个引用的场景 `前奏 2.5 s`、`主歌1 2.4 s`、`尾奏 2.5 s`（单帧跳转，
含预热，仅作量级参考）。

### 1.3 待坐实的因果：受控 A/B

仅凭观察性数据还不能排除「一级子场景自身的效果链才是大头」。
已在 `host-test/prerender-ab/`（本地，不进仓库，工程文件不写回原路径）派生 4 份纯文本变体，
用 `effect.disable=1` 只改 `シーン` 效果块，其余逐字节不变：

| 变体 | 改动 | 用于测 |
| --- | --- | --- |
| `A-baseline` | 无（与原工程逐字节相同） | 基线 1608 ms |
| `B-no-subscenes` | 禁用全部 36 个 `シーン` | 只剩各场景自身内容 → 次合成总开销 = A−B |
| `C-master-only` | 禁用 28 个二级引用，保留主合成 8 个 | 隔离「二级大画布展开」的增量 |
| `D-no-master-refs` | 只禁用主合成 8 个引用 | 隔离「一级引用」的增量 |

播放同一段（建议 frame 600–660，主歌段），每组采 ≥30 帧连续播放的 VERBOSE 帧时间。
**验收门槛**：`A−B` 应占 A 的 70% 以上；若 <40%，说明瓶颈主要在一级子场景自身效果链，
预渲染收益有限，方向要改（应转向效果链级别缓存，而非次合成预渲染）。

> 这一步需要启动宿主播放，属于**只读测试**：不改插件 DLL、不改语言包、不改用户原工程，
> 只临时切 `FileLogLevel`，测完还原。等用户确认时机再执行。

---

## 2. HitFilm 契约与 AviUtl2 对策

| HitFilm 契约 | 官方原文要点 | AviUtl2 对策 |
| --- | --- | --- |
| ① 对 comp 后台预渲染一次 | *Proxying is carried out in the background*；产物落盘、Processor 面板显示进度 | `EDIT_HANDLE::rendering_scene_video()` 异步离屏渲染指定帧，回调里写入 `CACHE_IMAGE` |
| ② 全时间线复用产物 | 所有引用该 comp 的片段改用 proxy 文件 | 缓存按 `(scene_id, frame, fingerprint)` 索引，任何引用命中同一帧即复用 |
| ③ 改动即失效 | *Making any changes in a proxied composite shot will cause the proxy to be invalidated* | 内容指纹（见 §4）；`EVENT_TYPE::UPDATE_OBJECT` / `register_clear_cache_handler` / `register_project_load_handler` 触发失效 |
| ④ 导出可选使用代理 | *Use Proxies For Export*（用户可选，代价是画质） | **不照搬**：`EDIT_STATE_SAVE` + `obj.getinfo("saving")` 双闸强制回原场景，出片无损 |

另有 *Pre-Render*（时间线段缓存）与 *RAM Preview*（内存配额）：AviUtl2 只有
`[Player] BufferTime=500` 这种毫秒级预缓冲，只能平滑抖动、不提高平均帧率，
不能当作跨帧缓存用。这一点旧报告已判定，本设计不重复。

---

## 3. 架构：跨帧缓存 + 内容指纹（而非视频文件）

### 3.1 为什么不落视频文件

| 问题 | 说明 |
| --- | --- |
| 无法替换取帧来源 | `シーン` 是宿主内建效果，插件拿不到「把它的输入换成我的文件」的接口；只能通过缓存让它第二次取同一帧时变快 |
| alpha / 色彩 | 编码器引入 YUV 抽样、色彩空间与 alpha 丢失；AviUtl2 全链路是预乘 `PIXEL_RGBA` / HF64 |
| 帧精度 | comp 支持 `再生位置` 偏移与变速，视频文件要维护帧映射表，反而更脆 |
| 失效成本 | 改一个参数就要重编码整段；内存缓存只需丢弃对应帧槽 |

### 3.2 三层结构

```
┌─ Layer 0  内容指纹    comp 内所有对象 alias + 参数 + 场景尺寸 + 引用目标指纹
│                       → 64-bit hash，变化即整段失效
├─ Layer 1  跨帧帧槽    CACHE_HANDLE::create_image_cache(identifier, name, w, h)
│                       key = (scene_id, frame_abs, fingerprint, 输出规格)
│                       value = PIXEL_RGBA* (主存，宿主共享缓存区)
└─ Layer 2  取帧适配    func_proc_rendering_video 回调写入帧槽；
                        命中时 set_image_data / 直接返回缓存指针
```

**关键取舍**：`rendering_scene_video()` 渲染的是**当前选中场景**，要渲染别的场景必须先
`select_scene(scene_id)`。这是全案最大交互风险，见 §6。

**免编码器路线（备选）**：注册 `INPUT_PLUGIN_TABLE`，令 `func_read_video(ih, frame, buf)`
从帧槽读取，把预渲染结果伪装成一个「视频文件」供 `動画ファイル` 使用。
优点是完全不需要 `select_scene` 切换编辑场景；缺点是**要求用户把 `シーン` 对象换成
自定义输入对象**，不再是「无感」。因此定位为 Phase 3 可选形态，不作为首版。

### 3.3 数据流

```
播放帧 F，遇到 シーン 引用 (target=S, offset=T)
  │
  ├─ 计算 comp 内绝对帧  f = floor((F - T) * speed)   // 本工程 speed=100%，线性
  ├─ 计算指纹             fp = Fingerprint(S)
  ├─ 导出态？ ───────── yes ─► 绕过缓存，回原路径（强制无损）
  ├─ 查帧槽 (S, f, fp, spec)
  │     └─ hit  ─► 直接取 PIXEL_RGBA，零渲染
  └─ miss ─► UI 线程网关排队：
               select_scene(S) → rendering_scene_video(f, ..., cb)
               cb 里写入 CACHE_IMAGE → select_scene(restore)
               （渲染请求失败/输出中时降级为原路径，绝不阻塞播放）
```

---

## 4. SDK 零件清单（已逐个核对签名，2.1.18b / sdk-2.1.16）

| 零件 | 位置 | 用途 |
| --- | --- | --- |
| `EDIT_HANDLE::rendering_scene_video(int frame, void* param, cb)` | `plugin2.h:830` | 异步离屏渲染；**输出中返回 false**；回调给 `buffer/width/height/pitch`，`PIXEL_RGBA` |
| `EDIT_HANDLE::wait_rendering_task()` | `plugin2.h:846` | 等待全部任务；**参照锁/编辑锁内调用会死锁** |
| `CACHE_HANDLE::create_image_cache(void* id, LPCWSTR name, int w, int h)` | `cache2.h:137` | 主存跨帧帧槽（「与脚本 `cache:xxxx` 不同，分配在主存」） |
| `CACHE_HANDLE::get_image_cache` / `clear_image_cache` | `cache2.h:129` | 命中 / 失效；`CACHE_IMAGE{ PIXEL_RGBA* buffer; int width,height; }` |
| `CACHE_HANDLE` 初始化 | `cache2.h:11` | 导出 `InitializeCache(CACHE_HANDLE*)`，**在 `InitializePlugin()` 之前**调用 |
| `EDIT_HANDLE::enum_scene_name` / `select_scene(int)` | `plugin2.h:902 / 908` | 枚举场景、切换当前场景 |
| `EDIT_SECTION::get_object_alias(OBJECT_HANDLE)` | `plugin2.h:208` | 对象 alias INI 文本 → 内容指纹 |
| `EVENT_TYPE::{UPDATE_OBJECT=1, CHANGE_EDIT_SCENE=3, CHANGE_EDIT_STATE=5}` | `plugin2.h` | 失效触发源 |
| `HOST_APP_TABLE::register_clear_cache_handler` / `register_change_scene_handler` / `register_project_load_handler` / `register_project_save_handler` | `plugin2.h:1118 / 1122 / 1089 / 1093` | 宿主级失效与生命周期钩子 |
| `EDIT_STATE_SAVE = 2` | `plugin2.h:778` | 导出硬闸 |
| `INPUT_PLUGIN_TABLE::func_read_video(INPUT_HANDLE, int frame, void* buf)` | `input2.h` | Phase 3 免编码器帧源（可选） |
| `SCENE_INFO::{origin_id, id, width, height, rendering_id}` | `filter2.h` | 判断「处于被合成的子场景」、渲染去重 |

**已确认不需要**：修改宿主二进制、hook、注入、Lua metatable 改写。

---

## 5. 数据结构（草案）

```cpp
struct CompKey {
    int    scene_id;        // 目标场景
    int    frame;           // comp 内绝对帧（含 offset / speed 换算）
    uint64 fingerprint;     // comp 内容指纹
    int    spec_w, spec_h;  // 缓存规格（预览可用 1/2，导出不走缓存）
};
struct CompFrame {
    CACHE_IMAGE image;      // 主存 PIXEL_RGBA
    ULONGLONG   stamp;      // LRU / TTL
    bool        valid;
};
struct CompFingerprint {
    uint64 hash;            // FNV-1a over sorted (object_id, alias, scene w/h, child fp)
    int    object_count;
    int    ref_count;       // 引用的子场景数（递归）
};
```

**指纹必须递归**：改了 `寺倾花` 里的立绘，所有引用它的一级子场景、以及主合成的帧槽
都要失效。实现上按引用图做一次拓扑排序（本工程是 DAG，无环，深度 2），
自底向上算 `fp(scene) = H(自身对象别名, 场景尺寸, [fp(child)...])`。

**内存预算**：单帧 2048×1080 `PIXEL_RGBA` = 8.4 MiB；按 LRU 上限 60 帧 ≈ 500 MiB。
62.5 MPx 画布**不缓存**（那是源图，不是 comp 输出）；缓存的是压到主合成规格的结果。
配额需可配置，并与 `[Cache] ImageCacheSize` 协调，避免挤爆宿主解码缓存（v0.3.2 踩过）。

---

## 6. 风险与规避（按严重度排序）

| # | 风险 | 证据 / 原因 | 规避 |
| --- | --- | --- | --- |
| R1 | **`select_scene()` 改变用户当前编辑场景、污染撤销栈**；后台线程调用编辑节 API 曾导致真机闪退 | `TECHNICAL.md` §4：minidump `std::thread::_Invoke → WatchWorker`；已改为 UI 线程隐藏窗口 `WM_TIMER` + 唯一 `CallEditSection()` 网关 | **复用现有网关**：所有 `select_scene` / `rendering_scene_video` 只从 UI 线程定时器发起；进入前记录 `get_edit_info()` 的当前场景，渲染完精确还原；绝不 `wait_rendering_task()` 于锁内；失败即降级原路径 |
| R2 | **跨场景语义被压平后不等价**：39 个 `カメラ制御`、组控制、剪裁 / 遮罩、合成モード | 主合成 8 引用 + 二级 4 引用；相机/组控制的作用域跨层 | 逐条 A/B 像素对比（见 §8）；首版**只对无相机/组控制下游的 comp 启用**，命中即自动禁用缓存 |
| R3 | **帧映射非线性**：变速 / 循环 / `再生位置` 偏移 | 本工程 36 个引用全为 100% 速度、无循环，仅 offset 不同 → 线性；但通用工程未必 | 首版仅接受 `speed=100% && loop=0`；其余一律跳过（降级原路径），并计入遥测 |
| R4 | **导出时不回原场景 → 出片糊 / 错帧** | HitFilm 允许 *Use Proxies For Export*，AviUtl2 不允许 | `EDIT_STATE_SAVE` + `obj.getinfo("saving")` 双闸；复用 ProxyLayer 现有导出闸门（真机验证过 0.00% 像素差） |
| R5 | **缓存挤爆宿主解码缓存** | v0.3.2：62.5 MPx PNG 把 `ImageCacheSize` 挤爆，`decode_max 468 ms` | 独立配额 + LRU + 只缓存压缩后的 comp 输出；`register_clear_cache_handler` 时主动释放 |
| R6 | **指纹漏检导致改了 comp 仍用旧帧** | `get_object_alias()` 可能不含所有参数；音频 / 插件参数变化 | 保守策略：任何 `UPDATE_OBJECT` 落在目标场景或其子孙 → 整段失效；宁可多失效，不可漏失效 |
| R7 | **同一 comp 被多处引用但 `再生位置` 不同** | 副歌1 被引用 3 次，offset 28.134 / 27.267 / 28.400 s | 缓存键含**绝对帧**，不按场景去重；`SCENE_INFO::rendering_id` 只用于同帧去重 |

---

## 7. 分阶段里程碑与验收条件

| Phase | 范围 | 验收条件 |
| --- | --- | --- |
| **P0 · 归因验证**（本轮，只读） | §1.3 四组 A/B 播放、VERBOSE 帧时间 | `A−B ≥ 70% × A`；否则重新定方向 |
| **P1 · 单 comp 只读缓存原型** | 沙盒工程，1 个 comp、1 个引用、固定帧、无相机/组控制 | 稳态帧时间下降 ≥50%；缓存命中率 ≥90%；**画面与未缓存逐像素一致**；`select_scene` 后当前场景与撤销栈完全还原 |
| **P2 · 失效 + 帧映射** | 递归指纹、offset/speed 换算、LRU 配额 | 改 comp 内任一参数后 ≤1 帧内全部失效；3 个不同 offset 引用不串帧；内存不超配额 |
| **P3 · 导出闸门 + 语义清单** | R2/R4 逐条验证 | 导出与未缓存 0.00% 像素差；含相机/组控制/剪裁的 comp 自动降级且结果正确 |
| **P4 · 发布形态** | 开关 / 菜单 / 语言包 / 遥测 / 一键安装 | 默认关，手动开；三语；不破坏现有 `ProxyLayer.aux2` 与 `luaJIT.dll` 代理链 |

**每一步都要求真机像素 A/B**（延续本项目既有方法：`PrintWindow` 抓预览 + PIL 差分），
不通过就不进入下一阶段。

---

## 8. 与 `global.PQ_LEVEL` 的取舍（已定）

`global.PQ_LEVEL` **归档，不实现**。理由（2026-11-18 与用户一致）：

- 它要解决的「AUTO 判得不够狠」已被 v0.3.3 推翻：AUTO 在本工程已把 8 张 62.5 MPx
  素材打到 `div=8`，`cap≈20`、`sub=60`，AUTO 侧无可见优化空间。
- `filter_avg` 已降到 2.6 ms、占整帧 0.18%，全局档位能动的只有这一小格。
- 全局档位会覆盖所有工程的 AUTO 判定，等于把刚修好的尺寸 / 锚点兼容面重新打开。
- 已有 `プロキシ倍率` 与「本图层保持原图」做逐层微调，功能重复；再加一个旋钮
  与「打开开关自动流畅」的无感目标相反。

---

## 9. 未决问题

1. `CACHE_IMAGE` 的生命周期由宿主管理，`clear_image_cache` 的粒度能否按 `name` 前缀批量删除？
   若不能，需自己维护帧槽索引并逐个释放。
2. `rendering_scene_video()` 回调发生在「事件通知线程」，写 `CACHE_IMAGE` 是否需要额外同步？
   需真机验证是否可直接在回调里 `memcpy`。
3. `select_scene()` 是否会计入撤销栈？需在项目 P1 用 A/B 工程实测（改前后对比撤销步数）。
4. 缓存规格：按主合成分辨率缓存，还是按 comp 自身分辨率？前者省内存但下游相机推近会糊；
   建议首版按 comp 自身分辨率 × 1/2，并把该项做成配置。
5. 音频：`シーン` 引用也可带音频，首版**只缓存视频**，音频仍走原路径；
   需验证混合后不同步的风险。
6. 若 P0 的 `A−B` 不达门槛，方向应改为「一级子场景效果链缓存」——
   即缓存 `LyricMotioN` / `発光` / `ドロップシャドウ` 链的输出，而非整个 comp。
   这需要重新盘点 SDK 是否能在效果链中间插入缓存点。
