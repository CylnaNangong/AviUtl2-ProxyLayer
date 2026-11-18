<div align="center">

# ProxyLayer

**A native "preview proxy layer" plugin for AviUtl ExEdit2.**  
Flip one switch and every large still image in the whole project is swapped for a downscaled proxy, so playback becomes smooth immediately. **Exports always restore the original image**, so the delivered video is bit-for-bit unaffected. After conversion the timeline still shows the source file name, so it feels exactly like the built-in “Image File” object.

`ProxyLayer.aux2 v0.3.5` &nbsp;|&nbsp; `AviUtl ExEdit2 v2.1.x` &nbsp;|&nbsp; `MIT License`

**🌐 Language:** &nbsp; **[简体中文 (Chinese)](../README.md)** &nbsp;|&nbsp; **[日本語 (Japanese)](README_ja.md)** &nbsp;|&nbsp; **English**

*(Three language packs ship: `Default.*` Japanese tooltips, plus 简体中文 / English display names, [Panel] strings and tooltips)*

<sub>Note: the Japanese and English documents are translations; the Chinese README is authoritative if they ever disagree.</sub>

</div>

---

## Results

| Test | Original | AUTO proxy |
| --- | --- | --- |
| 12 layers of 7000×8000 @ 1920×1080 | ~1216 ms / frame | **~19 ms / frame** |
| Real 21-scene project (114 image objects of 8000×5000) | playback stutters | smooth after one-click conversion |

The proxy level is derived from `sqrt(source pixels / scene pixels)`, rounded up to a power of two and capped at 1/8. An 8000×5000 asset in a 2048×1080 scene gets `div=8`, i.e. a 1000×625 proxy — smaller than the screen.

## Features

- **One-click whole-project auto proxy**: walks every scene, not just the current one (AviUtl2's `EDIT_SECTION` only covers the current scene).
- **AUTO level** plus manual 1/2, 1/4, 1/8, 1/16, 1/32.
- **Export gate**: entering save/output state (`EDIT_STATE_SAVE`) forces L0 (the original image) back, with zero pixel difference.
- **Cross-layer sampling protection**: when volumetric light, volumetric fog, 3D bridge or similar scripts read another layer's pixels, the sampled layer automatically stays at full resolution (prevents "the light beam only lights the middle").
- **No duplicate source decoding**: once a proxy hits, the 40 MPx original is no longer requested from the host (measured: 95.6 ms per decode, 97% fast-path hit rate).
- **Timeline keeps the source file name**: no wall of `ProxyLayer` entries; turning the switch OFF restores the standard name too.
- **Original size metadata**: downstream scripts can read `obj.getvalue("ProxyLayer","元の幅"/"元の高さ")` and compensate themselves.
- **Strict allowlist**: only still-image inputs (`画像ファイル` etc.) and image extensions are converted; audio, video, OBJ, VRM and 3D models are never touched.
- **Global switch + settings window**: "OK" saves, "Cancel" discards; the state survives project changes and restarts.
- **Japanese internal keys + official hover tooltips**: `.aup2` keys use the host default language; parameter help lives in `[Tips.ProxyLayer]` tooltips instead of a long separator row.
- **Optional experimental component**: a same-name `luaJIT.dll` forwarding proxy that maps `obj.w/obj.h` back to the original size (see `src/ProxyLuaJIT.cpp`, not installed by default).

## Installation

### One-click install from Releases (recommended)

Each release provides two packages — pick one:

| Package | Contents | For |
| --- | --- | --- |
| `ProxyLayer-<tag>.au2pkg.zip` | `Plugin/ProxyLayer.aux2` + the `Language/*.aul2` display packs; drag it into the AviUtl2 window | Almost everyone; **never** touches the folder next to `aviutl2.exe` |
| `ProxyLayer-<tag>-Installer.zip` | Main plugin + display packs + optional `luaJIT.dll` forwarding proxy + `install.bat` (Japanese/Chinese/English output) | Users who need `obj.w/obj.h` size compensation |

**Installer flow** (this is the "rename the original DLL, drop in our own" step):

1. Extract the whole zip → double-click `install.bat`.
2. The script picks its messages from the OS display language (Japanese / Simplified Chinese / anything else → English); force it with `-Lang ja|zh|en`.
3. It locates `aviutl2.exe` and the data folder, then installs the main plugin and the display packs under `Language\`.
4. It backs up the existing `luaJIT.dll` as `luaJIT.dll.bak-<timestamp>`, renames it to `luaJIT_real.dll`, and copies our forwarding proxy in as `luaJIT.dll`. The PE forwarders inside resolve all 322 LuaJIT exports against `luaJIT_real.dll`.
5. Restart AviUtl2. `uninstall.bat` restores everything (display packs are moved aside, never deleted).

`install.ps1` supports `-ExePath "D:/AviUtl2/aviutl2.exe"`, `-SkipLuaProxy`, `-Uninstall` and `-Lang`. It is idempotent — re-running never renames or backs up twice. If the host folder has no `luaJIT.dll`, only the proxy step is skipped and the main plugin still works.

### Manual install

1. Get `dist/ProxyLayer.aux2` (or build it yourself, see below).
2. Copy it into the AviUtl2 data folder:
   - Normal install: `C:/ProgramData/aviutl2/Plugin/`
   - Portable mode: `<folder of aviutl2.exe>\data\Plugin\`
3. For a localized UI, drop `Language/*.ProxyLayer.aul2` into the data folder's `Language\` and pick the language under Setup → Language (this switches the whole host UI as well).
4. Restart AviUtl2 and make sure the host trusts the plugin (`module.ini`).
5. Edit menu → `代理图层` → `整个工程自动代理 (开/关)`, or Setup → `ProxyLayer 自动代理` to open the settings window.
6. Play for 10–20 seconds before judging smoothness: the first pass has to build a proxy cache for every large image, so the first seconds are actually slower.

> ⚠️ The plugin **never** creates a `data` folder next to `aviutl2.exe`. As soon as AviUtl2 sees that folder it switches to portable mode and reads an empty configuration, resetting language, plugins and layout. This was an early trap in this project; all related folder-creation code has been removed.

Softer preview quality is by design; exports always use the original image.

## UI languages (localization)

Since **v0.3.0 all internal item keys are Japanese** (the AviUtl2 default language, `Default`), matching community convention. The effect name and menu root stay English `ProxyLayer`.

| Host language | Parameters / settings window / menus | Pack |
| --- | --- | --- |
| `Default` (Japanese) | Japanese | `Default.ProxyLayer.aul2` (Japanese tooltips only) |
| `简体中文` | Chinese | `简体中文.ProxyLayer.aul2` |
| `English` | English | `English.ProxyLayer.aul2` |

**v0.3.1 removed the long `ProxyLayer | ...` separator row** from the object settings panel. Its content now lives in the official **`[Tips.ProxyLayer]`** hover tooltips: `effect.name` describes the effect and 12 item keys describe the parameters. See [docs.aviutl2.jp](https://docs.aviutl2.jp/) and the header comment of the host's own `简体中文.aul2`.

The settings window and menus are runtime C++ strings, so ordinary `.aul2` translation cannot reach them. They now use the official `CONFIG_HANDLE::get_language_text()` with a **`[Panel]`** section, so they follow the host language too; without a pack they fall back to Japanese.

The filename prefix must match the host language setting exactly: real installations only have `Default.*`, `简体中文.*`, `English.*`, `Deutsch.*` and `community_*`. There is **no「日本語」language** — `Default` IS Japanese.

> ⚠️ **v0.3.0 is a breaking change**: projects saved with v0.2.x contain old Chinese keys. Restore them with OFF (or re-run auto proxy) before upgrading. See [CHANGELOG.md](../CHANGELOG.md).

Full inventory: [`LOCALIZATION.md`](LOCALIZATION.md).

## Parameters

| Item (internal key / Japanese) | Meaning |
| --- | --- |
| `画像ファイル` | Source image path (written at conversion) |
| `プロキシ倍率` | AUTO / 1/2 / 1/4 / 1/8 / 1/16 / 1/32 |
| `出力時は元画像を強制` | Export gate, on by default |
| `このレイヤーは元画像を維持` | Per-layer escape hatch, set automatically for cross-layer sampled layers |
| `元の幅` / `元の高さ` / `元のエフェクト名` / `元のファイル項目名` | Metadata for restore and downstream compensation |
| `プローブログを出力` / `プロキシマーカーを表示` / `出力状態をシミュレート` | Diagnostics / self-test switches |

> The experimental `モード` track was **removed in v0.3.0**: modes 0/1/2/4 were A/B measurement variants; the production path is now a compile-time constant. Size compensation is provided by the optional `luaJIT.dll` forwarding proxy (scripts such as OpenGlow / 光束_H depend on it — keep it), or by reading `obj.getvalue("ProxyLayer","元の幅"/"元の高さ")`.

## How it works (short version)

`ProxyLayer` is a `FLAG_INPUT` media-object effect that replaces the original `画像ファイル`:

1. Read the source image, downsample it by `div` into a 1/div proxy texture, cache it in memory + disk PNG (persistent in VRAM as `image:`).
2. `mode 3` calls `set_image_data(1/div)` and then multiplies `param.sx/sy` by `div`, so the object quad is still drawn at the original size — geometry, 3D camera, Z sorting and the downstream effect chain all survive.
3. `EDIT_SECTION` only covers the current scene, so `enum_scene_name()` + `select_scene()` convert scene by scene and finally restore the scene the user was in.
4. `select_scene()` must not be called inside a reference/edit lock, so the whole walk runs on the UI thread from a hidden `WM_TIMER` window; menu callbacks only post a message.
5. Export/save states force L0 and redraw with the full original image.

Measured data and war stories: [`TECHNICAL.md`](TECHNICAL.md); maintainer-oriented assessment: [`REVIEW.md`](REVIEW.md) (Chinese).

## Known limitations

> **⚠️ If you use glow/beam scripts, read [Scripts that read their own pixel buffer](#preview-scripts-that-read-their-own-pixel-buffer-openglow-光束_h-etc) first.** A proxied layer reports the original `obj.w/obj.h`, but its **real pixel buffer is still 1/div**. Scripts that read that buffer can look slightly different in preview; exports are always lossless.

- The AviUtl2 SDK cannot keep `obj.w/obj.h` unchanged while a proxy texture is used (measured 2048 → 1024 on the Lua side). **The optional `luaJIT.dll` forwarding proxy maps it back to the original size** (A/B measured 1024 → 2048); scripts like OpenGlow / 光束_H that compute their range from `obj.w/h` depend on it, so it cannot be dropped. Scripts may also read `元の幅/元の高さ`. The proxy cannot help with **buffer-size** reads such as `getpixeldata()`; that is what cross-layer sampling protection is for.
- Layers sampled by other layers stay at full resolution and therefore gain no speedup.
- Still images only; previews look softer than the original, exports are unaffected.
- The first playback builds caches.
- Menus and the settings window read the `[Panel]` section via `CONFIG_HANDLE::get_language_text()`, so they follow the host language (Japanese fallback).
- Some third-party Rust/Egui plugins panic when the host exits; unrelated to this plugin.

### Preview: scripts that read their own pixel buffer (OpenGlow / 光束_H etc.)

**This is an inherent limit of proxying, not a bug and not a failed compensation.**
The `luaJIT.dll` forwarding proxy can only fix the two **numbers** `obj.w` and `obj.h`;
under a proxy the host still hands the script an object buffer containing **1/div of the pixels**.
When a script samples that small buffer using the compensated (large) dimensions, the result
differs slightly from the full-resolution render.

| | Full resolution (original) | Proxied (preview) |
| --- | --- | --- |
| `obj.w` / `obj.h` | original, e.g. 6251×4501 | still 6251×4501 (pair matching fixed this) |
| object pixel buffer | 6251×4501 | 1563×1126 (1/div) |
| buffer reads | complete shape | out-of-bounds zeros / different pyramid levels and padding base |

**Affected scripts**: anything that reads the **current object's own** buffer — typically
`obj.pixelshader(..., "object", ...)`, `getpixeldata()` / `putpixeldata()`, or re-reading
`obj.w/obj.h` after `obj.effect("領域拡張")`. Measured examples:

- `OpenGlow`: identical parameters, slightly different glow spread in preview.
- `光束_H`: identical parameters, different beam length in preview.
  (**Note**: "only a small bright patch in the centre" was the old *uncompensated* `obj.w/h`
  problem, fixed in v0.3.5. What remains is a much smaller difference.)

**Not affected**: scripts that only use the `obj.w/h` numbers without reading the buffer;
plain colour grading, keying and transform effects.

**Exports are lossless**: the export/save gate forces L0, restoring the full-size buffer,
so rendered output matches the unproxied project. The difference exists only while editing.

**For a pixel-identical preview**: tick **`このレイヤーは元画像を維持`**
(`Keep this layer full-res` in the English pack, 「本图层保持原图」 in Chinese) on that layer.
That layer stays full-resolution while every other layer keeps its speedup.

**Why this is not automatic**: existing protection covers two cases that can be identified
precisely by parameter semantics — cross-layer sampling (`obj.load("layer", ...)` etc.) and
same-object absolute pixel anchors (`bone`-style coordinate tuples). Scripts reading their own
buffer vary widely; a blanket rule would also skip colour grading / keying and similar everyday
effects, which conflicts with the "protect only what is necessary" principle. See
[`TECHNICAL.md` §5.3](TECHNICAL.md) for the full analysis.

## Building

Requires Windows + Visual Studio 2022 BuildTools (MSVC x64) + Windows SDK 10 + Git Bash.

**The official AviUtl2 SDK headers are not part of this repository** (copyright ＫＥＮくん). Fetch the following from the official SDK package and put them in `include/`:

```
filter2.h  cache2.h  logger2.h  plugin2.h  input2.h  output2.h  config2.h  module2.h
```

Then:

```bash
bash build_msvc.sh
```

The script defaults to `14.39.33519` / `10.0.22621.0`; override for other versions:

```bash
VS_BASE="D:/VS2022" MSVC_VER=14.42.34433 SDKV=10.0.26100.0 bash build_msvc.sh
```

The output is `dist/ProxyLayer.aux2`. `build_luaproxy.sh` builds the optional LuaJIT forwarding DLL, `build_exportprobe.sh` builds the export test probe (neither goes into the default packages).

> Changing only language packs / docs / packaging scripts needs **no rebuild**: `.aul2` is pure data that the host reads at startup and on language switches.

## Releasing (one click)

The workflow lives in `.github/workflows/release.yml`. **Pushing one `v*` tag is enough**:

```bash
# 1. Build locally and commit the fresh dist/ProxyLayer.aux2
git add dist/ && git commit -m "build: refresh binaries for v0.3.1"
# 2. Tag and push; CI creates the Release, packages and uploads the assets
git tag v0.3.1 && git push origin main && git push origin v0.3.1
```

CI checks out the tag, runs `packaging/build_release.py --tag <tag>` to build both zips (including `Plugin/` and `Language/`), publishes them with `gh release create` (if needed) and `gh release upload --clobber`, and lists SHA-256 digests in the workflow summary. Publishing a Release in the web UI first, or re-running via `workflow_dispatch` with a tag, both work.

> The workflow does **not** compile the DLL: the official SDK headers are not in this repo, so CI ships the committed `dist/` binaries. Remember to refresh them before a release.

Preview the packaging output locally:

```bash
python3 packaging/build_release.py --tag v0.3.0 --out-dir dist
```

## Updates and compatibility (does the DLL need a rebuild?)

First decide **which kind** of update this is:

| What changed | `ProxyLayer.aux2` | `luaJIT.dll` forwarding proxy |
| --- | --- | --- |
| Docs / language packs / install scripts / release workflow only | **No rebuild**; reuse the `dist/` binary (re-packaging puts the language packs into the zips) | Not needed |
| `src/*.cpp` changed (new release) | **Rebuild required**, and commit the new `dist/ProxyLayer.aux2`. CI does **not** compile — it only packages committed binaries, otherwise the release still ships the old DLL | Usually not; only when the host's LuaJIT export table changes, regenerate with `tools/gen_luaproxy_def.py` + `build_luaproxy.sh` |
| Host AviUtl2 updated | Generally **no rebuild**, unless the SDK structs changed incompatibly | **High risk**, see below |

### Updating the host

The main plugin only uses the official SDK: `GetFilterPluginTable()` / `InitializePlugin(DWORD version)` / `RegisterPlugin(HOST_APP_TABLE*)`. Function pointers come from the host-provided `HOST_APP_TABLE`, structs are passed with `sizeof()` (e.g. `get_edit_info(&info, sizeof(info))`), and there is **no hardcoded `aviutl2.exe` address or version number**; `InitializePlugin` only logs the version and does not validate it. Minor AviUtl2 updates therefore usually keep working with the existing DLL.

However, struct field offsets are fixed at compile time against the sdk-2.1.16 headers. If the official SDK changes the order of `FILTER_PROC_VIDEO` / `EDIT_SECTION` / `FILTER_PLUGIN_TABLE` fields or inserts new ones, the old DLL reads the wrong offsets and must be rebuilt with the new headers. After an update, check `InitializePlugin host=0x...` in the host log and the official changelog for plugin API changes; if loading fails or it crashes, move `ProxyLayer.aux2` aside first to isolate it.

**The `luaJIT.dll` forwarding proxy is the only component truly pinned to a host version**: its 322 PE forwarders were generated from that particular `luaJIT.dll` export table. If an AviUtl2 update replaces the LuaJIT build, the host folder's `luaJIT.dll` gets overwritten by the official one:

- `install.ps1` detects that `luaJIT.dll` and `luaJIT_real.dll` differ and **refreshes the forwarding target from the current official DLL** (the old real DLL is backed up as `.bak-<timestamp>`), avoiding mixed ABIs; uninstall restores the correct official DLL as well.
- The old proxy's export name set may still not match the new LuaJIT. The safe procedure: update the host → run `uninstall.bat` → confirm AviUtl2 starts → install the Release that matches the new host. If the log shows missing `GetProcAddress` symbols, regenerate the `.def` from the new `luaJIT.dll` and rebuild the proxy.
- Users who installed only the `.au2pkg.zip` are not affected — that package never touches the exe folder.

## Repository layout

```
src/ProxyLayer.cpp      main plugin
src/ProxyLuaJIT.cpp     optional: same-name luaJIT.dll forwarding proxy (experimental)
src/png_writer.h        dependency-free RGBA8 PNG encoder
src/ExportProbe.cpp     test-only: drives a real export through the official output_file()
Language/*.aul2         Default / 简体中文 / English language packs (tooltips + display names)
build_*.sh              build scripts
tools/gen_luaproxy_def.py  generates the 322 PE forwarders from the luaJIT.dll export table
tools/dump_stack.py     stdlib-only minidump analyzer (exceptions/stacks/module histogram)
tools/read_perf.py      summarizes ProxyLayer-perf.log telemetry
dist/ProxyLayer.aux2    prebuilt binary
docs/TECHNICAL.md       implementation details and measurements (Chinese)
docs/LOCALIZATION.md    language-pack key inventory and how to add a language (Chinese)
docs/REVIEW.md          repository review and improvement suggestions (Chinese)
docs/README_ja.md       README 日本語版
docs/README_en.md       this file
```

## Credits

- **HitFilm, rest in peace** — the whole "flip a switch and it gets smooth" goal was carried over from it. May it keep previewing in real time somewhere else.
- **The AI partner in WorkBuddy (Tencent Hunyuan Hy)** — chewed through four minidumps, dug out the real semantics of `find_object`, walked all 21 scenes, and wrote this README, the packaging scripts and the CI along the way.
- **Tencent's generous free compute** (笑) — without those tokens none of the "try again" and "compare once more" rounds above would have happened.

And ＫＥＮくん, author of the AviUtl2 SDK, plus everyone who writes scripts, reports issues and patiently retests on real machines. The reference repository [AviUtl2_VolumetricLight3D](https://github.com/CylnaNangong/AviUtl2_VolumetricLight3D) set the pattern for `.au2pkg.zip` packaging, the release workflow and multilingual delivery (`.aul2` language packs + multi-language READMEs).

## License

MIT, see [LICENSE](../LICENSE). The AviUtl2 SDK headers keep their original license and are not redistributed here.
