# 转码后端（Encoder Backends）

> 适用对象：`Tools/make_video_proxy.py`（随 Release 分发）与插件设置面板（v0.3.8 起）。
> 状态：**QSVEncC 已真机验证且逐字节零回归；NVEncC / ffmpeg / custom 仅命令构造级验证**。
> 方向 A 解除硬编码；v0.3.8 加入面板后端选择 / 测试按钮并把工具链打进两个
> 发布 zip。**在线转码仍未集成进插件**（P3-2）。

---

## 0. 先厘清「QSVEnc 到底在哪一环」

| 环节 | 是否依赖 QSVEnc | 说明 |
| --- | --- | --- |
| 图片代理（插件主体） | ❌ | 纯 SDK 降采样 + 每文件一张缓存纹理，零外部 exe |
| **导出**（A′ / v0.3.7） | ❌ | F9 时只改工程 `ファイル` / `拡大率`，**不碰输出管线**。用户之后按 F9 选的是自己装的任意输出插件：x264guiEx、内置 AVI、PNG、QSVEnc 等。第五轮真机验证用的正是内置 `AVIファイル出力` |
| **生成视频代理文件** | ✅ 曾经绑死 | 本文件要解决的就是这一环 |

也就是说「大部分人用 x264 导出」从一开始就没问题；真正卡人的是**生成代理**这一步：
旧脚本 `make_video_proxy.py` 把 exe 路径写死成
`C:\ProgramData\aviutl2\Plugin\exe_files\QSVEncC\x64\QSVEncC64.exe`，
命令带 `--avhw`（Intel QSV 硬解硬编），AMD / NVIDIA / 无核显机器都没有这个 exe。

> **v0.3.8 起**：设置面板已有「转码后端」下拉与「测试」按钮（见 §3.1），
> 配置写入全局 `proxy-encoder.txt`，语言包三包同步。但**在线转码 / 在线改工程
> 仍未集成进插件** —— 工具链作为 `Tools/` 随 Release 分发，仍需离线生成代理 +
> 派生工程再打开。「面板选后端 → 插件自动转码当前工程」属于 P3-2。

---

## 1. 可用后端

| 名称 | exe | 验证状态 | 备注 |
| --- | --- | --- | --- |
| `qsvencc` | `QSVEncC64.exe` | **真机验证（P1）+ 逐字节零回归** | 命令与 P1 完全一致 |
| `nvenc` | `NVEncC64.exe` | 命令构造级，未实跑 | rigaya 系列 CLI，选项名与 QSVEncC 相同（推定） |
| `ffmpeg` | `ffmpeg.exe` | 命令构造级，未实跑 | CPU `libx264` 通用兜底，人人可装 |
| `custom` | 用户自带 | 由用户负责 | 命令模板，占位符见 §4 |

自动探测顺序默认 `qsvencc → nvenc → ffmpeg`（硬件优先、通用兜底），
可用 `--encoder-order` 改。

---

## 2. 为什么后端可替换而不牺牲提速

P1 真机四组 A/B 推翻过「GOP / 码率是主瓶颈」的推测：

| 变体 | 结论 |
| --- | --- |
| 4K 全 I 帧 (100 Mbps) | 比原片慢 20% |
| 4K 轻量 (5 Mbps) | 比原片慢 54% |
| **1080p 长 GOP (6.6 Mbps)** | **PLAY 226.4 ms / STEP 109.2 ms，最优** |

**只要像素尺寸不变就没有收益** —— 成本在 4K 帧缓冲（31.6 MB/帧）的解码/转换/上传。
所以后端只需满足三条：

1. 降到 `--res`（默认 `1920x1080`）
2. 音轨直通（`--audio-copy` / `-c:a copy`）
3. GOP ≈ 60

`拡大率` ×2 补偿由 `convert` 在工程侧完成，与编码器无关；
编码器本身不敏感，纯 CPU 转码只是**一次性**慢一点，预览时零影响。

---

## 3. 探测与覆盖

```bash
python make_video_proxy.py list-backends          # 看本机有哪些
python make_video_proxy.py transcode <src> --dry-run   # 只打印命令，不执行、不写文件
python make_video_proxy.py transcode <src> --encoder ffmpeg
python make_video_proxy.py transcode <src> --encoder-order ffmpeg,nvenc,qsvencc
```

路径覆盖（环境变量优先于 PATH 与内置已知路径）：

| 变量 | 用途 |
| --- | --- |
| `PROXYLAYER_QSVENCC` | 指定 QSVEncC64.exe |
| `PROXYLAYER_NVENCC` | 指定 NVEncC64.exe |
| `PROXYLAYER_FFMPEG` | 指定 ffmpeg.exe |
| `PROXYLAYER_ENCODER_EXE` | `custom` 模板里 `{exe}` 的值 |

`--dry-run` 在后端未安装时也会构造命令（标注 `dry-run (backend not installed)`），
方便在没有 ffmpeg 的机器上先检查命令。

---

### 3.1 设置面板（v0.3.8）

`ProxyLayer 设置窗口` 里新增一行：

| 控件 | 作用 |
| --- | --- |
| 转码后端下拉 | `auto` / `QSVEncC` / `NVEncC` / `ffmpeg` / `custom`；选择即写入全局 `proxy-encoder.txt` |
| 「测试」按钮 | 探测所选后端 exe 并启动 `--version`，在面板显示「可用 / 未找到 + 路径 + 退出码」 |

探测顺序：环境变量 → PATH（`SearchPathW`）→ 已知安装路径（`auto` 按
`qsvencc → nvenc → ffmpeg` 依次尝试）。测试按钮**不做任何转码、不改工程**。

> 面板里的选择目前供配套工具使用；插件自身还不会据此自动转码当前工程（P3-2）。

---

## 4. 命令与 custom 模板

```
# qsvencc / nvenc（QSVEncC 这条与 P1 逐字一致）
<exe> --avhw -i <src> -o <out> -c h264 --gop-len 60 --cqp 32 \
      --output-res 1920x1080 --audio-copy

# ffmpeg（libx264 CPU 兜底）
ffmpeg -y -i <src> -vf scale=1920:1080 -c:v libx264 -preset veryfast \
       -crf 23 -g 60 -vsync 0 -c:a copy <out>
```

`-vsync 0` 用于保留源 VFR 时间戳（P1 素材 2141 帧）；新版 ffmpeg 更推荐
`-fps_mode passthrough`，但 `-vsync 0` 仍可用（会有 deprecated 警告）。

`custom` 模板占位符：

| 占位符 | 值 |
| --- | --- |
| `{exe}` `{input}` `{output}` | 编码器路径 / 源文件 / 代理输出 |
| `{res}` `{width}` `{height}` | `1920x1080` / `1920` / `1080` |
| `{gop}` `{cqp}` `{crf}` | `60` / `32` / `23` |

示例（路径含空格请自行加引号）：

```bash
python make_video_proxy.py transcode <src> --encoder custom \
  --template '"{exe}" -i "{input}" -vf scale={width}:{height} -g {gop} -crf {crf} -c:a copy "{output}"' \
  --encoder-exe /path/to/myencoder.exe
```

sidecar `.plv.json` 新增 `encoder_backend` 与 `encoder_command` 字段，
`encoder` 改为记录**实际执行的命令**；`convert` / `export` / `restore` / `verify`
的行为不变。

---

## 5. 验证记录（2026-11-22，真机 AviUtl2 2.1.18b 环境）

- `test_encoder_backends.py`：**18 项离线自检 ALL PASS**（命令构造、占位符、
  env 覆盖、`--encoder-order`、缺模板拒绝、`list-backends` 无依赖运行）。
- `list-backends` 本机结果：`qsvencc FOUND`，`nvenc / ffmpeg missing`（PATH 无 ffmpeg）。
- 四个后端 `--dry-run` 命令均正确构造，未写任何文件。
- **实跑 QSVEncC**：2141 帧 / 167.58 fps / 6576.92 kbps / 28.00 MB，
  `3840×2160 → 1920×1080`、`scale_ratio=2.0`，sidecar 写入 `encoder_backend=qsvencc`。
- **零回归**：新生成的 `IMG_2329.proxy.mp4` 与 P1 真机验证过的
  `proxy-1080-gop.mp4` **逐字节一致**
  （SHA256 `233fe9ca34473e1f6faceb53c6be726480c594421241632465cced5ef32e382b`，
  30,287,891 B）⇒ 默认路径产物不变，P1 的 56.5–56.8% 提速结论继续成立。
- P1 夹具 sidecar 在实跑后已从备份逐字节恢复（`cmp` 通过）。

---

## 6. 未验证与边界（不宣称全部后端可用）

- **NVEncC / ffmpeg / custom 未在本机实跑**（本机没有对应 exe），只验证了命令构造；
  在装有这些工具的机器上仍需各跑一次 `transcode` + `convert` + 预览 A/B。
- ffmpeg 的 VFR / 音轨拷贝行为、NVEncC 的选项兼容性均属**推定**，以实跑为准。
- 视频代理仍是**离线工具链**（v0.3.8 起随 `Tools/` 分发），插件已能选择 /
  测试后端，但还不会自动对当前工程在线转码与改工程（P3-2）。
- v0.3.8 改动了 `ProxyLayer.aux2`（面板）、三份语言包（新增 4 个 `[Panel]` 键）、
  `tools/check_aul2.py` 与 `packaging/build_release.py`；图片代理路径与
  A′ 导出硬闸未改；用户工程与 `IMG_2329.MOV` 全程只读。
