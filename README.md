# AviUtl2 ProxyLayer

给 **AviUtl ExEdit2** 做的「预览代理图层」原生插件。

打开一个开关，把整个工程里的大尺寸静态图像自动换成低分辨率代理，播放立刻变流畅；**导出时自动还原原图**，成片画质完全不受影响。转换后时间轴仍显示源文件名，使用体验和原来的「画像ファイル」一致。

## 效果

| 测试 | 原图 | AUTO 代理 |
| --- | --- | --- |
| 12 层 7000×8000 @ 1920×1080 | ~1216 ms / 帧 | **~19 ms / 帧** |
| 真实 21 场景工程（114 个 8000×5000 图像对象） | 播放卡顿 | 一键转换后流畅播放 |

代理档位按 `sqrt(源像素 / 场景像素)` 自动选，向上取 2 的幂、上限 1/8。8000×5000 的素材在 2048×1080 场景里得到 `div=8`，代理只有 1000×625 —— 比屏幕还小。

## 功能

- **一键全工程自动代理**：遍历所有场景，不只是当前场景（AviUtl2 的 `EDIT_SECTION` 只对应当前场景）。
- **AUTO 档位** + 手动 1/2 / 1/4 / 1/8 / 1/16 / 1/32。
- **导出闸门**：检测到保存/输出态（`EDIT_STATE_SAVE`）强制回到 L0 原图，0 像素差异。
- **跨图层取样保护**：体积光、体积雾、3D 桥接等脚本读取别的图层像素时，被取样图层自动保持全分辨率，避免「光束只亮中间一小块」。
- **不重复解码原图**：代理命中后不再向宿主索取 40 MPx 原图（实测单次解码 95.6 ms，快路径命中率 97%）。
- **时间轴显示源文件名**：转换后不再是一片 `ProxyLayer`；点 OFF 还原时自动恢复标准名。
- **原始尺寸元数据**：下游脚本可读 `obj.getvalue("ProxyLayer","原始宽"/"原始高")` 自行补偿。
- **严格白名单**：只转换静态图像输入（`画像ファイル` 等）与图片扩展名；音频、视频、OBJ、VRM、3D 模型不会被误转。
- **全局开关 + 设置窗口**：「确定」保存、「取消」不保存；跨工程、跨重启长期生效。
- **可选实验组件**：同名 `luaJIT.dll` 转发代理，把 `obj.w/obj.h` 映射回原尺寸（见 `src/ProxyLuaJIT.cpp`，默认不安装）。

## 安装

### 从 Release 一键安装（推荐）

每个 Release 提供两个包，按需选一个：

| 包 | 内容 | 适用 |
| --- | --- | --- |
| `ProxyLayer-<tag>.au2pkg.zip` | 只含 `Plugin/ProxyLayer.aux2`，拖进 AviUtl2 窗口即装 | 绝大多数用户；**不碰** `aviutl2.exe` 同目录 |
| `ProxyLayer-<tag>-Installer.zip` | 主插件 + 可选 `luaJIT.dll` 转发代理 + `install.bat` | 需要 `obj.w/obj.h` 尺寸补偿的用户 |

**Installer 包的一键流程**（这就是「把原 DLL 改名、放进自己的 DLL」那一步）：

1. 解压整个 zip → 双击 `install.bat`。
2. 脚本自动定位 `aviutl2.exe` 和数据目录，装好主插件。
3. 自动把原来的 `luaJIT.dll` 备份为 `luaJIT.dll.bak-<时间戳>`，改名成 `luaJIT_real.dll`，再把本包的转发代理复制成 `luaJIT.dll`。代理里的 PE forwarder 会把 322 个 LuaJIT 导出解析到 `luaJIT_real.dll`。
4. 重启 AviUtl2 生效。双击 `uninstall.bat` 可完整还原。

`install.ps1` 支持 `-ExePath "D:/AviUtl2/aviutl2.exe"`、`-SkipLuaProxy`、`-Uninstall`；幂等，重复运行不会重复改名。若宿主目录本来没有 `luaJIT.dll`，会跳过代理安装并提示，主插件照常可用。

### 手动安装

1. 取 `dist/ProxyLayer.aux2`（或按下方自行编译）。
2. 放到 AviUtl2 数据目录的 `Plugin\` 下：
   - 普通安装：`C:/ProgramData/aviutl2/Plugin/`
   - 便携模式：`<aviutl2.exe 同目录>\data\Plugin\`
3. 重启 AviUtl2，确认宿主已信任该插件（`module.ini`）。
4. 编辑菜单 → `代理图层` → `整个工程自动代理 (开/关)`，或 `设定` → `ProxyLayer 自动代理` 打开设置窗口。
5. 播放 10–20 秒再判断流畅度：首次播放需要为每张大图构建代理缓存，开头几秒反而偏慢。

> ⚠️ 插件**绝不会**在 `aviutl2.exe` 同目录创建 `data` 目录。AviUtl2 一旦检测到该目录就会切换到便携模式并读取空配置，导致语言、插件、布局全部「恢复初始化」。这是本项目早期踩过的坑，已彻底移除所有相关目录创建逻辑。

预览时画质下降是设计使然；导出始终使用原图。

## 参数

| 项目 | 说明 |
| --- | --- |
| `画像ファイル` | 源图像路径（转换时自动写入） |
| `モード` | 3 = 生产模式（`set_image_data` + 缩放补偿，保留下游效果链与相机） |
| `档位` | AUTO / 1/2 / 1/4 / 1/8 / 1/16 / 1/32 |
| `导出时强制原图` | 默认开，导出闸门 |
| `本图层保持原图` | 单层逃生开关，被跨图层取样时自动置 1 |
| `原始宽` / `原始高` / `原始效果名` / `原始文件名项` | 转换时写入的元数据，供还原与下游补偿 |
| `补偿 obj.w/h (实验)` | 默认关，SDK 层面无效，保留作实验 |

## 工作原理（简述）

`ProxyLayer` 是一个 `FLAG_INPUT` 媒体对象效果，替换原来的 `画像ファイル`：

1. 读源图，按 `div` 降采样成 1/div 的代理纹理，缓存到内存 + 磁盘 PNG（`image:` VRAM 常驻）。
2. `mode 3` 调 `set_image_data(1/div)`，再把 `param.sx/sy *= div`，使对象四边形仍按原尺寸绘制 —— 几何、3D 相机、Z 排序、下游效果链全部保持。
3. `EDIT_SECTION` 只覆盖当前场景，所以用 `enum_scene_name()` + `select_scene()` 逐场景转换，最后还原用户原来所在的场景。
4. `select_scene()` 明确禁止在参照锁/编辑锁内调用，因此整个遍历放在 UI 线程的隐藏窗口 `WM_TIMER` 里执行，菜单回调只投递消息。
5. 导出态 / 保存态强制 L0，用完整原图重绘。

更多实测数据与踩坑记录见 [`docs/TECHNICAL.md`](docs/TECHNICAL.md)。

## 已知限制

- AviUtl2 SDK 无法在代理纹理下让 `obj.w/obj.h` 保持不变（Lua 侧实测 2048 → 1024）。下游脚本请改用 `原始宽/原始高`；可选的 LuaJIT 代理 DLL 能补偿一部分，但对 `getpixeldata()` 这类缓冲读取无效。
- 被跨图层取样的图层会保持全分辨率，因此不享受加速。
- 只处理静态图像；预览画面会比原图糊，导出不受影响。
- 首次播放有缓存构建期。
- 某些第三方 Rust/Egui 插件在宿主退出时会 panic，与本插件无关。

## 构建

需要 Windows + Visual Studio 2022 BuildTools（MSVC x64）+ Windows SDK 10 + Git Bash。

**官方 AviUtl2 SDK 头文件不在本仓库**（版权归原作者 ＫＥＮくん）。请从官方发布包取得以下文件并放入 `include/`：

```
filter2.h  cache2.h  logger2.h  plugin2.h  input2.h  output2.h  config2.h  module2.h
```

然后：

```bash
bash build_msvc.sh
```

脚本默认使用本机 `14.39.33519` / `10.0.22621.0`；版本不同可覆盖：

```bash
VS_BASE="D:/VS2022" MSVC_VER=14.42.34433 SDKV=10.0.26100.0 bash build_msvc.sh
```

产物在 `dist/ProxyLayer.aux2`。`build_luaproxy.sh` 构建可选的 LuaJIT 同名代理 DLL，`build_exportprobe.sh` 构建导出测试探针（都不进默认安装包）。

## 发版（一键 Release）

工作流在 `.github/workflows/release.yml`。**推一个 `v*` tag 就够了**：

```bash
# 1. 本地编译好，把新的 dist/ProxyLayer.aux2（必要时还有 dist/luaJIT.dll）提交
git add dist/ && git commit -m "build: refresh binaries for v0.2.1"
# 2. 打 tag 并推送，CI 自动建 Release + 打包 + 上传资产
git tag v0.2.1 && git push origin main && git push origin v0.2.1
```

CI 会 `checkout` 该 tag、跑 `packaging/build_release.py --tag <tag>` 生成两个 zip、用 `gh release create`（若尚无 Release）和 `gh release upload --clobber` 发布，并在 workflow summary 里列出 SHA-256。也支持先在网页上发 Release 再触发，或在 Actions 里手动 `workflow_dispatch` 指定 tag 重跑。

> 工作流**不编译** DLL：官方 SDK 头文件不在本仓库，CI 直接使用已提交的 `dist/` 二进制。发版前记得刷新它们。

本地预览打包结果：

```bash
python3 packaging/build_release.py --tag v0.2.0 --out-dir dist
```

## 目录

```
src/ProxyLayer.cpp      主插件
src/ProxyLuaJIT.cpp     可选：同名 luaJIT.dll 转发代理（实验）
src/png_writer.h        零依赖 RGBA8 PNG 编码器
src/ExportProbe.cpp     测试专用：用官方 output_file() 直启真实导出
build_*.sh              构建脚本
tools/gen_luaproxy_def.py  从 luaJIT.dll 导出表生成 322 条 PE forwarder
tools/dump_stack.py     纯标准库 minidump 分析器（异常/栈/模块直方图）
tools/read_perf.py      汇总 ProxyLayer-perf.log 遥测
dist/ProxyLayer.aux2    预编译二进制
docs/TECHNICAL.md       实现细节与实测数据
```

## 鸣谢

- **天国的 HitFilm** —— 「打开开关就自动流畅」这个目标，最初就是从它那儿搬来的。愿它在另一个次元继续实时预览。
- **WorkBuddy 里的 AI 搭子（腾讯混元 Hy）** —— 一起啃完四份 minidump、把 `find_object` 的真语义翻出来、把 21 个场景逐个走完，顺手写下这份 README、打包脚本和 CI。
- **腾讯公司提供的免费算力大力支持**（笑）—— 没有这些 token，上面那些「再试一次」「再对一遍」都不会发生。

以及 AviUtl2 SDK 的原作者 ＫＥＮくん，和所有写脚本、报问题、耐心反复试真机的人。参考仓库 [AviUtl2_VolumetricLight3D](https://github.com/CylnaNangong/AviUtl2_VolumetricLight3D) 提供了 `.au2pkg.zip` 打包与 Release 工作流的范式。

## 许可

MIT，见 [LICENSE](LICENSE)。AviUtl2 SDK 头文件遵循其原始许可，本仓库不分发。
