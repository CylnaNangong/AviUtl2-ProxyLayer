//==============================================================================
//  ProxyLayer.aux2 - AviUtl2 (ExEdit2) preview proxy media object
//
//  Built entirely on the OFFICIAL sdk-2.1.16 API. No host hooking, no inline
//  patching, no changes to aviutl2.exe or to any project file.
//
//  The object is a FLAG_INPUT media object: it replaces the object's image
//  input with a 1/div downscaled proxy texture, then scales param->sx/sy back
//  up so geometry, camera and the whole downstream effect chain are unchanged.
//  Export/save state forces L0 (the original full-resolution image).
//
//  Internal item keys are Japanese (the host default language). Display packs
//  under Language/ translate labels and provide [Tips.ProxyLayer] tooltips.
//  Menus / the settings window use CONFIG_HANDLE::get_language_text() so they
//  follow the host language too (Japanese fallback when no pack is installed).
//==============================================================================

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "filter2.h"
#include "cache2.h"
#include "logger2.h"
#include "config2.h"
#include "plugin2.h"
#include "png_writer.h"

//------------------------------------------------------------------------------
// Globals supplied by the host
//------------------------------------------------------------------------------
static LOG_HANDLE*       g_logger = nullptr;
static CACHE_HANDLE*     g_cache  = nullptr;
static HOST_APP_TABLE*   g_host   = nullptr;
static EDIT_HANDLE*      g_edit   = nullptr;
static DWORD             g_host_version = 0;
static CONFIG_HANDLE*    g_config = nullptr;   // InitializeConfig(), before InitializePlugin()
static std::atomic<int>  g_event_state{ -1 };
static std::mutex        g_mu;

// Menus and the self-drawn settings window are runtime C++ strings, so .aul2
// display packs cannot reach them. The official CONFIG_HANDLE solves this:
// get_language_text() looks up [Panel] in the current language file.
// Keys are short ASCII identifiers (a wording tweak never breaks a pack);
// the fallback is Japanese, i.e. the host default language. Default-language
// users therefore need NO display pack at all.
static std::wstring T(LPCWSTR key, LPCWSTR ja) {
    if (g_config && g_config->get_language_text) {
        LPCWSTR t = g_config->get_language_text(g_config, L"Panel", key);
        if (t && *t) return t;
    }
    return ja;
}
// .aul2 values may contain "\n" escapes; the host may or may not expand them.
// Normalise both literal escapes and real newlines to CRLF for the STATIC control.
static std::wstring NormNL(const std::wstring& in) {
    std::wstring out;
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == L'\\' && i + 1 < in.size() && in[i + 1] == L'n') {
            out += L"\r\n"; ++i;
        } else if (in[i] == L'\r') {
            continue;
        } else if (in[i] == L'\n') {
            out += L"\r\n";
        } else {
            out += in[i];
        }
    }
    return out;
}
extern "C" __declspec(dllexport) void InitializeConfig(CONFIG_HANDLE* config) {
    g_config = config;
}

// ---- Optional LuaJIT proxy support (experimental, NOT in shipping package) --
// ProxyLuaJIT.dll wraps lua_setfield(). Whenever the host writes obj.w/obj.h
// into the Lua table it asks us whether that dimension belongs to a downscaled
// proxy texture; if so we return the ORIGINAL source dimension instead.
// Entries expire after 5 s so switching projects cannot leak stale mappings.
struct SizeMapEntry {
    double proxyW, proxyH, srcW, srcH;
    ULONGLONG tick;
};
static std::mutex               g_sizemu;
static std::vector<SizeMapEntry> g_sizemap;

static void RegisterSizeMap(int pw, int ph, int sw, int sh) {
    const ULONGLONG now = GetTickCount64();
    std::lock_guard<std::mutex> lk(g_sizemu);
    for (auto& e : g_sizemap) {
        if (e.proxyW == pw && e.proxyH == ph &&
            e.srcW == sw && e.srcH == sh) { e.tick = now; return; }
    }
    if (g_sizemap.size() >= 64) g_sizemap.erase(g_sizemap.begin());
    g_sizemap.push_back({ (double)pw, (double)ph, (double)sw, (double)sh, now });
}

extern "C" __declspec(dllexport) int __cdecl
ProxyLayer_MapSize(double cur, int isW, double* out) {
    if (!out || cur <= 0.0) return 0;
    const ULONGLONG now = GetTickCount64();
    std::lock_guard<std::mutex> lk(g_sizemu);
    for (auto it = g_sizemap.rbegin(); it != g_sizemap.rend(); ++it) {
        if (now - it->tick > 5000) continue;
        const double p = isW ? it->proxyW : it->proxyH;
        if (p == cur) { *out = isW ? it->srcW : it->srcH; return 1; }
    }
    return 0;
}

// "HitFilm-like" one-switch state: ON is remembered across host restarts and
// across every project opened afterwards. A background watcher picks up any
// image object added later, so the user never has to touch a layer again.
static std::atomic<bool> g_auto_proxy{ false };
static std::atomic<bool> g_watch_stop{ false };
static std::atomic<bool> g_watch_dirty{ true };
static std::atomic<bool> g_in_all_scenes{ false };  // suppress watcher while switching scenes
static void PerfFlush(const char* tag);      // defined in the telemetry section
static void LogF(const wchar_t* fmt, ...);   // defined in the Logging section

// ---------------------------------------------------------------------------
// EDIT_SECTION APIs are MAIN-THREAD ONLY.
// plugin2.h:731 says the callback is invoked from the main thread, and four
// WER minidumps (14.11.2026 03:50-04:00) proved that calling
// EDIT_HANDLE::call_edit_section_param() from a std::thread crashes the host
// at aviutl2.exe+0x215c50 with a null dereference (read [rcx+0x888]):
//   std::thread::_Invoke -> WatchWorker+0x6f -> aviutl2.exe+0x215c50
// Periodic passes now run on a WM_TIMER owned by a hidden window created on
// the host UI thread, and every edit-section entry point below is guarded.
// ---------------------------------------------------------------------------
static DWORD g_ui_thread = 0;
static HWND  g_timer_hwnd = nullptr;
static const wchar_t* kTimerClass = L"ProxyLayerTimerWnd";
static const UINT kMsgEditJob      = WM_APP + 1;
static const UINT kMsgAutoProxyAll = WM_APP + 2;   // whole-PROJECT toggle
static inline bool OnHostUiThread() {
    return g_ui_thread == 0 || GetCurrentThreadId() == g_ui_thread;
}
struct EditJob {
    void*  param;
    void (*thunk)(void*, EDIT_SECTION*);
    HANDLE done;
    bool   ok;
};

// The plugin DLL already lives in the host's WRITABLE data folder in both
// layouts, so use ITS directory for private state:
//   isolated runtime : <hostroot>\data\Plugin\ProxyLayer.aux2
//   real installation: %ProgramData%\aviutl2\Plugin\ProxyLayer.aux2
// The old version used GetModuleFileNameW(nullptr,...) (the EXE), which on a
// real install resolves to E:\Program Files\AviUtl2\data\Plugin\ -- a missing,
// read-only directory. CreateFileW then failed silently and the global switch
// was never persisted.
static std::wstring PluginDataFile(const wchar_t* name) {
    HMODULE self = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&g_auto_proxy), &self))
        return L"";
    wchar_t mod[MAX_PATH] = {};
    if (!GetModuleFileNameW(self, mod, MAX_PATH)) return L"";
    const std::wstring m(mod);
    const size_t s = m.find_last_of(L"\\/");
    if (s == std::wstring::npos) return L"";
    const std::wstring dir = m.substr(0, s);
    // Do NOT CreateDirectoryW here: the DLL folder always exists, and creating
    // anything near the EXE could make the host switch to a portable data root.
    return dir + L"\\" + name;
}
static bool PrefSwitchExists() {
    const std::wstring p = PluginDataFile(L"proxy-autoproxy.on");
    return !p.empty() && GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES;
}
static void WritePrefSwitch(bool on) {
    const std::wstring p = PluginDataFile(L"proxy-autoproxy.on");
    if (p.empty()) return;
    if (on) {
        HANDLE h = CreateFileW(p.c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            const char* s = "ProxyLayer auto-proxy = ON\r\n";
            DWORD written = 0;
            WriteFile(h, s, (DWORD)std::strlen(s), &written, nullptr);
            CloseHandle(h);
            LogF(L"AutoProxy: global preference saved to %s", p.c_str());
        } else {
            LogF(L"AutoProxy: FAILED to save global preference (%u)",
                 GetLastError());
        }
    } else {
        DeleteFileW(p.c_str());
    }
}

//------------------------------------------------------------------------------
// Logging
//------------------------------------------------------------------------------
static void Log(const std::wstring& s) {
    if (g_logger && g_logger->info) g_logger->info(g_logger, s.c_str());
    else OutputDebugStringW((L"[ProxyLayer] " + s + L"\n").c_str());
}
static void LogF(const wchar_t* fmt, ...) {
    wchar_t buf[1024];
    va_list ap; va_start(ap, fmt);
    _vsnwprintf_s(buf, _countof(buf), _TRUNCATE, fmt, ap);
    va_end(ap);
    Log(buf);
}

// Small, bounded, rate-limited performance log next to the DLL. The host's
// in-app log panel is not persisted, so without this we cannot quantify where
// playback time goes on a real project. Never creates a directory.
static void PerfLogLine(const char* line) {
    static std::mutex mu;
    std::lock_guard<std::mutex> lk(mu);
    const std::wstring p = PluginDataFile(L"ProxyLayer-perf.log");
    if (p.empty()) return;
    HANDLE h = CreateFileW(p.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD hi = 0;
    const DWORD lo = GetFileSize(h, &hi);
    if (hi == 0 && lo < 1024u * 1024u) {
        DWORD written = 0;
        WriteFile(h, line, (DWORD)std::strlen(line), &written, nullptr);
    }
    CloseHandle(h);
}

//------------------------------------------------------------------------------
// Effect settings
//------------------------------------------------------------------------------
static auto item_file = FILTER_ITEM_FILE(
    L"画像ファイル", L"",
    L"Image (*.png;*.jpg;*.jpeg;*.bmp;*.tif;*.tiff)\0"
    L"*.png;*.jpg;*.jpeg;*.bmp;*.tif;*.tiff\0");

// The production render path is FIXED since v0.3. Early builds exposed an
// experimental 「モード」 track (L0 / draw_image / draw_poly / uncompensated
// control) purely for A/B measurements; only set_image_data() + relative scale
// survived (docs/TECHNICAL.md section 8). Shipping dead variants only confused
// users, so the item was removed and mode is now a compile-time constant.
//   0 = AUTO by source/scene pixel ratio, 1 = 1/2, 2 = 1/4, 3 = 1/8,
//   4 = 1/16, 5 = 1/32 (preview-only; export gate always restores L0).
// AUTO caps at 1/8, which already yields a proxy SMALLER than the screen for
// 40 MPx sources. Levels 4/5 are manual escape hatches for users who want to
// trade even more preview sharpness for throughput.
// zero_display is an official SDK field: value 0 shows "AUTO" on the slider.
static auto item_level = FILTER_ITEM_TRACK(L"プロキシ倍率",  0, 0, 5, 1.0, L"AUTO");

static auto item_force_l0 = FILTER_ITEM_CHECK(L"出力時は元画像を強制", true);
static auto item_probe    = FILTER_ITEM_CHECK(L"プローブログを出力", true);
// Visible proof that the proxy is really being used: paints a thin magenta bar
// along the top edge of the proxy texture. OFF by default (never pollutes a
// real render); turn it on once to confirm the plugin is live.
static auto item_mark     = FILTER_ITEM_CHECK(L"プロキシマーカーを表示", false);
// SELF-TEST ONLY: lets the harness exercise the SAVE branch without a real
// encoder. Default off; never enable in a shipping build / user project.
static auto item_sim_save = FILTER_ITEM_CHECK(L"出力状態をシミュレート", false);
// Per-layer escape hatch: keep the ORIGINAL image for this one object. Useful
// when a manually-added ProxyLayer sits in front of a size-dependent script.
// FallbackL0 calls set_image_data(..., W, H), so obj.w/obj.h stay untouched.
static auto item_keep_original = FILTER_ITEM_CHECK(L"このレイヤーは元画像を維持", false);
// NOTE: the experimental 「补偿 obj.w/h」 item was REMOVED. Writing back
// OBJECT_INFO::width/height was DISPROVED by a real Lua probe: the host feeds
// Lua from the current texture size, not from OBJECT_INFO (2048 -> 1024).
// Size compensation now lives in the OPTIONAL same-name luaJIT.dll proxy
// (ProxyLayer_MapSize + a lua_setfield wrapper), and scripts can always read
// 「元の幅」/「元の高さ」 via obj.getvalue().

// Compensation data for DOWNSTREAM filters/scripts.
// set_image_data() shrinks the object image, so obj.w/obj.h become 1/div of
// the source and are ReadOnly from Lua. Two supported ways out:
//   - scripts read 「元の幅」/「元の高さ」 via obj.getvalue() and scale themselves;
//   - the OPTIONAL same-name luaJIT.dll proxy maps obj.w/h back transparently
//     (ProxyLayer_MapSize + a lua_setfield wrapper, see src/ProxyLuaJIT.cpp).
// Writing OBJECT_INFO::width/height does NOT work (disproved); that path was
// removed. Both values are written once, at conversion time, from the host's
// own image cache.
static auto item_src_w = FILTER_ITEM_TRACK(L"元の幅", 0, 0, 65536, 1.0, L"未設定");
static auto item_src_h = FILTER_ITEM_TRACK(L"元の高さ", 0, 0, 65536, 1.0, L"未設定");
// Remembers what was replaced so the OFF switch restores the EXACT original
// effect (audio / 3D model / video objects must never become 画像ファイル).
static auto item_origin_effect = FILTER_ITEM_STRING(L"元のエフェクト名", L"");
static auto item_origin_item   = FILTER_ITEM_STRING(L"元のファイル項目名", L"");

static void* items[] = {
    &item_file, &item_level,
    &item_force_l0, &item_probe, &item_mark, &item_sim_save,
    &item_keep_original,
    &item_src_w, &item_src_h, &item_origin_effect, &item_origin_item, nullptr
};

bool func_proc_video(FILTER_PROC_VIDEO* v);

static FILTER_PLUGIN_TABLE filter_plugin_table = {
    FILTER_PLUGIN_TABLE::FLAG_VIDEO | FILTER_PLUGIN_TABLE::FLAG_INPUT,
    L"ProxyLayer",
    L"ProxyLayer",
    L"ProxyLayer v0.3 - whole-project preview proxy / full-resolution export",
    items,
    func_proc_video,
    nullptr
};

//------------------------------------------------------------------------------
// Host callbacks
//------------------------------------------------------------------------------
static void OnClearCache(EDIT_SECTION*) {
    std::lock_guard<std::mutex> lk(g_mu);
    Log(L"OnClearCache: invalidating proxy set");
    // Proxy files are content-addressed (path+mtime+size+div), so stale files
    // can never be hit again. We leave them on disk for the OS temp cleaner.
}
static void OnStateChanged(void*) {
    // Event callbacks only set a flag. Never call call_edit_section() here:
    // the SDK explicitly forbids it during output.
    if (g_edit && g_edit->get_edit_state) g_event_state.store(g_edit->get_edit_state());
}
static void OnObjectsUpdated(void*) {
    // Event callbacks must NOT touch EDIT_SECTION. Just wake the watcher.
    g_watch_dirty.store(true);
}
static void OnProjectLoad(PROJECT_FILE*) {
    // The switch is GLOBAL by design: one click covers every project, present
    // and future. Older builds wrote a per-project "autoproxy" key on every
    // save, which then OVERRODE the global value and made users re-enable the
    // switch for each project. We deliberately ignore that legacy key now.
    const bool on = PrefSwitchExists();
    g_auto_proxy.store(on);
    g_watch_dirty.store(true);
    LogF(L"OnProjectLoad: auto-proxy=%s (global preference)",
         on ? L"ON" : L"OFF");
}
static void OnProjectSave(PROJECT_FILE*) {
    // Intentionally empty. Writing the switch into the project would defeat
    // the global preference ("不用每个工程开一次").
}

//------------------------------------------------------------------------------
// "One switch, whole scene" auto-proxy mode.
//
// ProxyLayer is a FLAG_INPUT media object, so normally every large layer has
// to be replaced by hand. Enabling this mode walks the ENTIRE current scene
// and converts every 画像ファイル effect into ProxyLayer in place, keeping the
// original file path and the effect order (so 標準描画 and later effects keep
// working). Disabling it converts them back. No per-layer parameter tuning:
// AUTO proxy scale and the export gate are already the defaults, and the export gate restores
// full resolution automatically.
//------------------------------------------------------------------------------
// (g_auto_proxy is declared with the other globals above.)

// ---- Safety filter: STRICT WHITELIST --------------------------------------
// v0.2 converted ANY effect that happened to expose a "ファイル" item. That
// included 音声ファイル (.wav), 動画ファイル and 3D model/OBJ inputs - they
// were replaced by an image filter and silently stopped working.
//
// A blacklist would never keep up with future object types (VRM bridges, new
// 3D model bridges, ...). So we use a STRICT PREFIX WHITELIST instead: only
// known static-image input effects are ever proxied; EVERYTHING ELSE is left
// untouched by default. Two independent gates:
//   (1) effect name starts with a known image-input name
//   (2) the file extension is a raster image we can actually decode
static std::wstring Utf8ToWide(const char* s) {
    if (!s || !*s) return L"";
    const int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    std::wstring r(n > 1 ? n - 1 : 0, L'\0');
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s, -1, &r[0], n);
    return r;
}
static std::string WideToUtf8(const wchar_t* s) {
    if (!s || !*s) return "";
    const int n = WideCharToMultiByte(CP_UTF8, 0, s, -1, nullptr, 0, nullptr, nullptr);
    std::string r(n > 1 ? n - 1 : 0, '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, s, -1, &r[0], n, nullptr, nullptr);
    return r;
}
static bool ContainsFold(const std::wstring& hay, const wchar_t* needle) {
    std::wstring h = hay, k = needle;
    for (auto& c : h) if (c >= L'A' && c <= L'Z') c += 32;
    for (auto& c : k) if (c >= L'A' && c <= L'Z') c += 32;
    return h.find(k) != std::wstring::npos;
}
static bool IsImageEffect(LPCWSTR nm) {
    if (!nm || !*nm) return false;
    // Prefix match covers language-pack variants such as
    // 「画像ファイル (アルファ付き)」 without ever admitting an unrelated
    // media type. Add a prefix here when a new STATIC IMAGE input appears.
    static const wchar_t* kAllow[] = {
        L"画像ファイル", L"画像文件", L"图像文件", L"画像入力",
        L"ImageFile", L"Image File"
    };
    for (const wchar_t* a : kAllow)
        if (std::wcsncmp(nm, a, std::wcslen(a)) == 0) return true;
    return false;
}
// Report each skipped effect name once, so adding a new whitelist entry is
// trivially driven by the real host log instead of guesswork.
static void LogSkipOnce(LPCWSTR nm, const char* why) {
    static std::unordered_set<std::wstring> seen;
    static std::mutex skipMu;
    const std::wstring name = nm ? nm : L"(unknown)";
    const std::wstring key = name + L"|" + Utf8ToWide(why);
    {
        std::lock_guard<std::mutex> lk(skipMu);
        if (!seen.insert(key).second) return;
    }
    LogF(L"AutoProxy: skipped '%s' (%hs) - whitelist excluded",
         name.c_str(), why);
}
static bool IsImagePath(const std::string& p) {
    const size_t dot = p.find_last_of('.');
    if (dot == std::string::npos) return false;
    std::string e = p.substr(dot + 1);
    for (auto& c : e) if (c >= 'A' && c <= 'Z') c += 32;
    static const char* kExt[] = { "png", "jpg", "jpeg", "bmp", "tif", "tiff", "webp", "gif" };
    for (const char* k : kExt) if (e == k) return true;
    return false;
}
static bool QueryImageSize(const std::string& pathUtf8, int* w, int* h) {
    if (!g_cache || !g_cache->get_image_file_cache) return false;
    const std::wstring p = Utf8ToWide(pathUtf8.c_str());
    if (p.empty()) return false;
    CACHE_FILE_IMAGE img = g_cache->get_image_file_cache(p.c_str());
    if (!img || img.width <= 0 || img.height <= 0) return false;
    *w = img.width; *h = img.height;
    return true;
}
static bool ReadSourcePath(EDIT_SECTION* e, EFFECT_HANDLE eff,
                           std::string* out, LPCWSTR* itemUsed = nullptr) {
    static const wchar_t* kItems[] = { L"ファイル", L"文件", L"画像ファイル", L"画像文件" };
    for (const wchar_t* name : kItems) {
        LPCSTR v = e->get_effect_item_value(eff, name);
        if (v && *v) { *out = v; if (itemUsed) *itemUsed = name; return true; }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Timeline object name.
// 画像ファイル shows the FILE NAME as its "standard" object name; our
// FLAG_INPUT effect's standard name is simply "ProxyLayer". SDK:
//   get_object_name() -> nullptr means "standard name"
//   set_object_name(o, nullptr) restores the standard name
// So on conversion we set the object name back to the source file name, and
// on restore we clear it. A user-authored custom name is never overwritten.
// Idempotent: safe to run on every watcher pass and on already-converted
// projects created by older builds.
// ---------------------------------------------------------------------------
static std::wstring FileBaseNameWide(const std::string& p) {
    const size_t s = p.find_last_of("\\/");
    const std::string f = (s == std::string::npos) ? p : p.substr(s + 1);
    return f.empty() ? std::wstring() : Utf8ToWide(f.c_str());
}
static bool EnsureDisplayName(EDIT_SECTION* e, OBJECT_HANDLE o) {
    if (!e || !e->get_object_name || !e->set_object_name ||
        !e->get_effect_list || !e->get_effect_item_value) return false;
    LPCWSTR cur = e->get_object_name(o);
    const bool standard = (!cur || !*cur ||
                           std::wcscmp(cur, L"ProxyLayer") == 0);
    if (!standard) return false;            // custom name -> leave it alone
    EFFECT_HANDLE list[64] = {};
    const int n = e->get_effect_list(o, list, 64);
    for (int i = 0; i < n; ++i) {
        LPCWSTR nm = e->get_effect_name ? e->get_effect_name(list[i]) : nullptr;
        if (!nm || std::wcscmp(nm, L"ProxyLayer") != 0) continue;
        LPCSTR v = e->get_effect_item_value(list[i], L"画像ファイル");
        if (!v || !*v) return false;
        const std::wstring base = FileBaseNameWide(v);
        if (base.empty()) return false;
        e->set_object_name(o, base.c_str());
        return true;
    }
    return false;
}

static int ConvertObject(EDIT_SECTION* e, OBJECT_HANDLE o, bool enable) {
    EFFECT_HANDLE list[64] = {};
    const int n = e->get_effect_list ? e->get_effect_list(o, list, 64) : 0;
    if (n <= 0) return 0;
    for (int i = 0; i < n; ++i) {
        LPCWSTR nm = e->get_effect_name ? e->get_effect_name(list[i]) : nullptr;
        const bool isProxy = nm && std::wcscmp(nm, L"ProxyLayer") == 0;
        if (enable) {
            if (isProxy) continue;                       // already converted
            std::string path;
            LPCWSTR usedItem = L"ファイル";
            const bool hasPath = ReadSourcePath(e, list[i], &path, &usedItem);
            if (!IsImageEffect(nm)) {
                if (hasPath) LogSkipOnce(nm, "effect name not whitelisted");
                continue;                                // audio / video / OBJ / VRM / 3D
            }
            if (!hasPath) continue;
            if (!IsImagePath(path)) {
                LogSkipOnce(nm, "file extension not an image");
                continue;                                // .wav / .obj / .mp4 ...
            }
            // Downstream effects are NOT screened out: colour grading, keying
            // and similar 2D filters are extremely common and must still get
            // the speed-up. Size-reading scripts are covered by the optional
            // luaJIT.dll proxy (obj.w/h) and by cross-layer sampling
            // protection (getpixeldata() buffers); see docs/TECHNICAL.md.
            EFFECT_HANDLE ne = e->create_effect(o, L"ProxyLayer");
            if (!ne) continue;
            e->set_effect_item_value(ne, L"画像ファイル", path.c_str());
            // Remember EXACTLY what was replaced, so OFF restores the original
            // effect instead of blindly turning audio/3D into 画像ファイル.
            e->set_effect_item_value(ne, L"元のエフェクト名", WideToUtf8(nm).c_str());
            e->set_effect_item_value(ne, L"元のファイル項目名", WideToUtf8(usedItem).c_str());
            // Expose the ORIGINAL image size to downstream filters/scripts.
            int iw = 0, ih = 0;
            if (QueryImageSize(path, &iw, &ih)) {
                char b[32];
                _snprintf_s(b, _countof(b), _TRUNCATE, "%d", iw);
                e->set_effect_item_value(ne, L"元の幅", b);
                _snprintf_s(b, _countof(b), _TRUNCATE, "%d", ih);
                e->set_effect_item_value(ne, L"元の高さ", b);
            } else {
                Log(L"AutoProxy: size query failed; downstream compensation unavailable");
            }
            e->move_effect(o, ne, i);                    // keep original order
            e->delete_effect(o, list[i]);
            return 1;
        } else {
            if (!isProxy) continue;
            LPCSTR v = e->get_effect_item_value(list[i], L"画像ファイル");
            if (!v || !*v) continue;
            // Copy it BEFORE delete_effect() invalidates the item buffer.
            const std::wstring base = FileBaseNameWide(v);
            LPCSTR oe = e->get_effect_item_value(list[i], L"元のエフェクト名");
            LPCSTR oi = e->get_effect_item_value(list[i], L"元のファイル項目名");
            // Legacy objects converted before this metadata existed fall back
            // to 画像ファイル; a warning makes that visible in the log.
            if (!oe || !*oe) Log(L"AutoProxy: legacy proxy without 元のエフェクト名, restoring 画像ファイル");
            const std::wstring effect = (oe && *oe) ? Utf8ToWide(oe) : L"画像ファイル";
            const std::wstring item   = (oi && *oi) ? Utf8ToWide(oi) : L"ファイル";
            EFFECT_HANDLE ne = e->create_effect(o, effect.c_str());
            if (!ne) { LogF(L"AutoProxy: restore failed for effect %s", effect.c_str()); continue; }
            if (!e->set_effect_item_value(ne, item.c_str(), v))
                e->set_effect_item_value(ne, L"ファイル", v);   // language-pack fallback
            e->move_effect(o, ne, i);
            e->delete_effect(o, list[i]);
            // If the timeline still shows the file name WE wrote, restore the
            // standard name so 画像ファイル again shows its file name.
            if (e->get_object_name && e->set_object_name && !base.empty()) {
                LPCWSTR cur = e->get_object_name(o);
                if (cur && *cur && std::wcscmp(cur, base.c_str()) == 0)
                    e->set_object_name(o, nullptr);
            }
            return 1;
        }
    }
    return 0;
}

// ---- Cross-layer sampling protection --------------------------------------
// Some scripts (volumetric light, fog, 3D bridges) read ANOTHER layer's pixel
// buffer via obj.load("layer", ...) / obj.getpixeldata(). Their beam extent is
// derived from the buffer's REAL pw/ph, not from obj.w/obj.h -- so neither the
// OBJECT_INFO write-back nor the luaJIT.dll proxy can help: the texture really
// is 1/div. The only correct fix is to leave the sampled layer at full size.
//
// Detection is by PARAMETER SEMANTICS (layer-reference item names), NOT by a
// script-name blacklist, so new third-party scripts are covered automatically.
// Both 1-based UI layer numbers and 0-based SDK numbers are accepted.
struct ProtectedLayers {
    std::unordered_set<int> layers;
};
static const wchar_t* kLayerRefItems[] = {
    L"光源レイヤー", L"光源图层", L"LightLayer", L"light_layer",
    L"対象レイヤー指定", L"指定レイヤー", L"参照レイヤー", L"対象レイヤー",
    L"目标图层", L"指定图层", L"参照图层", L"ソースレイヤー",
    L"背景レイヤー", L"前景レイヤー", L"target_layer", L"source_layer"
};
// Walk one layer by OBJECT SPANS instead of every single frame. Objects on
// the same layer never overlap, so after find_object() succeeds we can jump
// straight to that object's end frame. Previously this was a layer x frame
// scan (up to 200 x 20000 host calls), which is far too expensive now that
// the pass runs on the UI thread.
template <class Fn>
static void ForEachObjectOnLayer(EDIT_SECTION* e, int layer, int maxFrame, Fn fn) {
    if (!e || !e->find_object) return;
    std::unordered_set<OBJECT_HANDLE> seen;
    for (int frame = 0; frame <= maxFrame;) {
        OBJECT_HANDLE o = e->find_object(layer, frame);
        int next = frame + 1;
        if (o) {
            if (e->get_object_layer_frame) {
                const OBJECT_LAYER_FRAME lf = e->get_object_layer_frame(o);
                // find_object() returns the first object AT OR AFTER `frame`,
                // so jumping to its end+1 enumerates the whole layer without
                // touching every single frame.
                if (lf.end >= frame)
                    next = lf.end >= maxFrame ? maxFrame + 1 : lf.end + 1;
            }
            if (seen.insert(o).second) fn(o);
        }
        frame = next > frame ? next : frame + 1;
    }
}
static ProtectedLayers CollectProtectedLayers(EDIT_SECTION* e,
                                               int maxLayer, int maxFrame) {
    ProtectedLayers out;
    if (!e || !e->find_object || !e->get_effect_list) return out;
    static std::unordered_set<std::wstring> logged;
    static std::mutex logMu;
    for (int layer = 0; layer <= maxLayer; ++layer) {
      ForEachObjectOnLayer(e, layer, maxFrame, [&](OBJECT_HANDLE o) {
            EFFECT_HANDLE list[64] = {};
            const int n = e->get_effect_list(o, list, 64);
            for (int i = 0; i < n; ++i) {
                LPCWSTR nm = e->get_effect_name ? e->get_effect_name(list[i]) : nullptr;
                if (nm && std::wcscmp(nm, L"ProxyLayer") == 0) continue;
                for (LPCWSTR item : kLayerRefItems) {
                    LPCSTR v = e->get_effect_item_value(list[i], item);
                    if (!v || !*v) continue;
                    // Parse "2", "4 5", "4-5", "4,5" etc. Non-numeric values
                    // such as "前方の全レイヤー自動検出" are ignored here.
                    const char* q = v;
                    int found = 0;
                    while (*q) {
                        if (*q >= '0' && *q <= '9') {
                            int num = 0;
                            while (*q >= '0' && *q <= '9') {
                                if (num < 1000) num = num * 10 + (*q - '0');
                                ++q;
                            }
                            if (num >= 0 && num <= 199) {
                                out.layers.insert(num);       // 1-based UI
                                if (num > 0) out.layers.insert(num - 1); // 0-based SDK
                                ++found;
                            }
                        } else {
                            ++q;
                        }
                    }
                    if (!found) continue;
                    const std::wstring key = std::wstring(nm ? nm : L"?") + L"|" + item + L"|" + Utf8ToWide(v);
                    bool first = false;
                    { std::lock_guard<std::mutex> lk(logMu); first = logged.insert(key).second; }
                    if (first)
                        LogF(L"AutoProxy: cross-layer reference %s/%s=%hs -> protecting layer(s)",
                             nm ? nm : L"?", item, v);
                }
            }
      });
    }
    return out;
}

// A protected layer may ALREADY carry a ProxyLayer effect (converted before
// protection existed, or added by hand). Skipping the conversion is not enough
// then -- flip its per-layer opt-out so the render path uses full resolution.
static int SetKeepOriginal(EDIT_SECTION* e, OBJECT_HANDLE o, bool keep) {
    if (!e || !e->get_effect_list) return 0;
    EFFECT_HANDLE list[64] = {};
    const int n = e->get_effect_list(o, list, 64);
    for (int i = 0; i < n; ++i) {
        LPCWSTR nm = e->get_effect_name ? e->get_effect_name(list[i]) : nullptr;
        if (!nm || std::wcscmp(nm, L"ProxyLayer") != 0) continue;
        LPCSTR cur = e->get_effect_item_value(list[i], L"このレイヤーは元画像を維持");
        const bool on = cur && (std::strcmp(cur, "1") == 0 ||
                                std::strcmp(cur, "on") == 0);
        if (on == keep) return 0;
        if (!e->set_effect_item_value(list[i], L"このレイヤーは元画像を維持",
                                      keep ? "1" : "0")) return 0;
        LogF(L"AutoProxy: layer-protect このレイヤーは元画像を維持=%s", keep ? L"1" : L"0");
        return 1;
    }
    return 0;
}

static int ConvertScene(EDIT_SECTION* e, bool enable) {
    if (!e || !e->find_object) return 0;
    EDIT_INFO info{};
    if (g_edit && g_edit->get_edit_info) g_edit->get_edit_info(&info, sizeof(info));
    const int maxLayer = std::min(std::max(info.layer_max, 0), 199);
    const int maxFrame = std::min(std::max(info.frame_max, 0), 19999);
    // Only relevant when ENABLING. Restoring (OFF) must still reach every
    // ProxyLayer object, including ones on protected layers.
    const ProtectedLayers prot = enable
        ? CollectProtectedLayers(e, maxLayer, maxFrame) : ProtectedLayers{};
    static std::unordered_set<int> announced;   // key = scene_id * 1000 + layer
    int changed = 0;
    for (int layer = 0; layer <= maxLayer; ++layer) {
        if (enable && prot.layers.count(layer)) {
            ForEachObjectOnLayer(e, layer, maxFrame, [&](OBJECT_HANDLE o) {
                changed += SetKeepOriginal(e, o, true);
                if (EnsureDisplayName(e, o)) ++changed;  // protected layers too
            });
            if (announced.insert(info.scene_id * 1000 + layer).second)
                LogF(L"AutoProxy: layer %d left at full resolution "
                     L"(sampled by a cross-layer script)", layer);
            continue;
        }
        ForEachObjectOnLayer(e, layer, maxFrame, [&](OBJECT_HANDLE o) {
            changed += ConvertObject(e, o, enable);
            if (enable && EnsureDisplayName(e, o)) ++changed;
        });
    }
    if (changed && e->set_edited_state) e->set_edited_state();
    return changed;
}

// Reliable escape hatch: the SDK cannot keep obj.w/obj.h unchanged while the
// proxy texture is 1/div (proven by the Lua probe). These two menus let the
// user flip EVERY proxied layer back to full resolution in one click - all
// downstream scripts work again - and restore the speed-up just as easily.
// No script-type enumeration, no blacklist, no per-layer clicking.
static int SetKeepAll(EDIT_SECTION* e, bool on) {
    if (!e || !e->find_object) return 0;
    EDIT_INFO info{};
    if (g_edit && g_edit->get_edit_info) g_edit->get_edit_info(&info, sizeof(info));
    const int maxLayer = std::min(std::max(info.layer_max, 0), 199);
    const int maxFrame = std::min(std::max(info.frame_max, 0), 19999);
    int changed = 0;
    for (int layer = 0; layer <= maxLayer; ++layer) {
      ForEachObjectOnLayer(e, layer, maxFrame, [&](OBJECT_HANDLE o) {
            EFFECT_HANDLE list[64] = {};
            const int n = e->get_effect_list ? e->get_effect_list(o, list, 64) : 0;
            for (int i = 0; i < n; ++i) {
                LPCWSTR nm = e->get_effect_name ? e->get_effect_name(list[i]) : nullptr;
                if (!nm || std::wcscmp(nm, L"ProxyLayer") != 0) continue;
                e->set_effect_item_value(list[i], L"このレイヤーは元画像を維持", on ? "1" : "0");
                ++changed;
            }
      });
    }
    if (changed && e->set_edited_state) e->set_edited_state();
    return changed;
}
static void OnMenuKeepAll(EDIT_SECTION* e) {
    LogF(L"AutoProxy[menu]: keep original on %d layer(s)", SetKeepAll(e, true));
}
static void OnMenuRestoreAll(EDIT_SECTION* e) {
    LogF(L"AutoProxy[menu]: resume proxy on %d layer(s)", SetKeepAll(e, false));
}
static void OnMenuAutoProxy(EDIT_SECTION* e) {
    const bool next = !g_auto_proxy.load();
    g_auto_proxy.store(next);
    WritePrefSwitch(next);              // survives restart; new projects inherit it
    g_watch_dirty.store(true);
    // A menu callback runs INSIDE an edit lock, and select_scene() is
    // explicitly forbidden while locked. Defer the whole-project pass to the
    // UI-thread timer window so it starts after this callback returns.
    if (g_timer_hwnd && IsWindow(g_timer_hwnd)) {
        PostMessageW(g_timer_hwnd, kMsgAutoProxyAll, next ? 1 : 0, 0);
        LogF(L"AutoProxy[menu]: queued whole-project pass, mode=%s",
             next ? L"ON" : L"OFF");
    } else {
        const int n = ConvertScene(e, next);   // fallback: current scene only
        LogF(L"AutoProxy[menu]: converted %d object(s) in current scene, mode=%s",
             n, next ? L"ON" : L"OFF");
    }
}

// Single gateway to EDIT_HANDLE::call_edit_section_param(). Off-thread calls
// (test harness workers, future code) are marshalled to the UI thread through
// the timer window instead of touching the host directly. This is the fix for
// the deterministic 0xc0000005 startup crash on a real installation.
static bool CallEditSection(void* param,
                            void (*thunk)(void*, EDIT_SECTION*)) {
    if (!g_edit || !g_edit->call_edit_section_param || !thunk) return false;
    if (OnHostUiThread())
        return g_edit->call_edit_section_param(param, thunk);
    if (!g_timer_hwnd || !IsWindow(g_timer_hwnd)) return false;
    EditJob* job = new (std::nothrow) EditJob{
        param, thunk, CreateEventW(nullptr, TRUE, FALSE, nullptr), false };
    if (!job || !job->done) { delete job; return false; }
    if (!PostMessageW(g_timer_hwnd, kMsgEditJob, 0,
                      reinterpret_cast<LPARAM>(job))) {
        CloseHandle(job->done); delete job; return false;
    }
    // The UI thread pumps WM_TIMER / our job message. Never wait forever:
    // during shutdown or a modal loop the message may never arrive.
    if (WaitForSingleObject(job->done, 5000) != WAIT_OBJECT_0) {
        // Deliberately leak the small job: the UI thread may still signal it.
        Log(L"AutoProxy: edit-job marshal timed out (host busy)");
        return false;
    }
    const bool ok = job->ok;
    CloseHandle(job->done);
    delete job;
    return ok;
}

// Drive the same conversion from OUTSIDE a menu click. Conversion is
// idempotent: an object already using ProxyLayer is skipped, so a "no new
// objects" pass is cheap and never marks the project dirty.
struct AutoReq { int enable; int result; const wchar_t* reason; };
static void AutoProxyThunk(void* p, EDIT_SECTION* e) {
    AutoReq* r = static_cast<AutoReq*>(p);
    r->result = ConvertScene(e, r->enable != 0);
}
static int RunAutoProxy(int enable, const wchar_t* reason) {
    if (!g_edit || !g_edit->call_edit_section_param) return -2;
    AutoReq req{ enable, -1, reason };
    if (!CallEditSection(&req, &AutoProxyThunk)) return -1;
    if (std::wcscmp(reason, L"watch") != 0 || req.result > 0)
        LogF(L"AutoProxy[%s]: converted %d object(s), mode=%s",
             reason, req.result, enable ? L"ON" : L"OFF");
    return req.result;
}

// ---------------------------------------------------------------------------
// WHOLE-PROJECT toggle.
// EDIT_SECTION only ever addresses the CURRENT scene. PV.aup2 has 21 scenes
// and 114 画像ファイル objects, but scene 0 (主合成) contains ZERO of them -
// it is all シーン / テキスト / 音声ファイル. A single-scene pass therefore
// reported "converted 0 object(s)" even though the plugin was working.
// enum_scene_name() + select_scene() let us visit every scene; select_scene()
// is forbidden inside a read/edit lock, so this MUST run on the UI thread and
// OUTSIDE any call_edit_section_param() callback. The original scene is
// restored at the end so the user never loses their place.
// ---------------------------------------------------------------------------
struct SceneIdList { std::vector<int> ids; };
static void EnumSceneCb(void* p, LPCWSTR /*name*/, int scene_id) {
    static_cast<SceneIdList*>(p)->ids.push_back(scene_id);
}
struct AllScenesGuard {
    AllScenesGuard() { g_in_all_scenes.store(true); }
    ~AllScenesGuard() { g_in_all_scenes.store(false); }
};
static int ConvertAllScenes(bool enable) {
    if (!OnHostUiThread()) return -3;
    if (g_in_all_scenes.load()) return -5;
    if (!g_edit || !g_edit->call_edit_section_param) return -2;
    if (g_edit->get_edit_state &&
        g_edit->get_edit_state() != EDIT_HANDLE::EDIT_STATE_EDIT) return -4;
    AllScenesGuard guard;

    EDIT_INFO info{};
    if (g_edit->get_edit_info) g_edit->get_edit_info(&info, sizeof(info));
    const int original = info.scene_id;
    SceneIdList list;
    if (g_edit->enum_scene_name) g_edit->enum_scene_name(&list, &EnumSceneCb);
    const bool canSwitch = (g_edit->select_scene != nullptr);

    int total = 0, touched = 0, failed = 0;
    // Known-good path FIRST: always handle the scene the user is looking at.
    // This is also the fallback if scene enumeration/switching is unavailable,
    // so the new code can never be worse than the old single-scene behaviour.
    AutoReq cur{ enable ? 1 : 0, -1, L"all-scenes-current" };
    if (CallEditSection(&cur, &AutoProxyThunk) && cur.result > 0) {
        total += cur.result; ++touched;
    }
    // Then visit every OTHER scene and restore the original at the end.
    if (canSwitch) {
        for (int id : list.ids) {
            if (id == original) continue;
            if (!g_edit->select_scene(id)) { ++failed; continue; }
            AutoReq req{ enable ? 1 : 0, -1, L"all-scenes" };
            if (!CallEditSection(&req, &AutoProxyThunk)) { ++failed; continue; }
            if (req.result > 0) { total += req.result; ++touched; }
        }
        if (original >= 0 && !list.ids.empty()) g_edit->select_scene(original);
    }
    g_watch_dirty.store(true);
    LogF(L"AutoProxy[all-scenes]: %u scene(s) listed, %d object(s) changed "
         L"in %d scene(s), %d select failed, switching=%s, mode=%s",
         (unsigned)list.ids.size(), total, touched, failed,
         canSwitch ? L"ON" : L"OFF (fallback)", enable ? L"ON" : L"OFF");
    return total;
}

// Cross-layer protection must also work when the global auto-proxy switch is
// OFF (users may add ProxyLayer by hand, or the preference may have failed to
// save). This pass ONLY flips the opt-out on already-proxied sampled layers;
// it never converts or restores anything.
static int ProtectScene(EDIT_SECTION* e) {
    if (!e || !e->find_object) return 0;
    EDIT_INFO info{};
    if (g_edit && g_edit->get_edit_info) g_edit->get_edit_info(&info, sizeof(info));
    const int maxLayer = std::min(std::max(info.layer_max, 0), 199);
    const int maxFrame = std::min(std::max(info.frame_max, 0), 19999);
    const ProtectedLayers prot = CollectProtectedLayers(e, maxLayer, maxFrame);
    if (prot.layers.empty()) return 0;
    int changed = 0;
    for (int layer = 0; layer <= maxLayer; ++layer) {
        if (!prot.layers.count(layer)) continue;
        ForEachObjectOnLayer(e, layer, maxFrame, [&](OBJECT_HANDLE o) {
            changed += SetKeepOriginal(e, o, true);
        });
    }
    if (changed && e->set_edited_state) e->set_edited_state();
    return changed;
}
struct ProtectReq { int result; };
static void ProtectThunk(void* p, EDIT_SECTION* e) {
    static_cast<ProtectReq*>(p)->result = ProtectScene(e);
}
static int RunProtectOnly() {
    if (!g_edit || !g_edit->call_edit_section_param) return -2;
    ProtectReq req{ -1 };
    if (!CallEditSection(&req, &ProtectThunk)) return -1;
    if (req.result > 0)
        LogF(L"AutoProxy[protect]: %d sampled layer(s) kept at full resolution",
             req.result);
    return req.result;
}

// Periodic passes run on the HOST UI THREAD via a hidden message-only window.
// A std::thread used to do this and crashed aviutl2.exe deterministically
// (see the EDIT_SECTION note near the globals). The window is created in
// RegisterPlugin(), which the host calls on its UI thread.
static DWORD s_lastScan = 0, s_lastProtect = 0;
static bool  s_inPeriodicPass = false;
static void RunPeriodicPass() {
    if (s_inPeriodicPass) return;                 // never re-enter
    if (g_in_all_scenes.load()) return;           // never fight a scene switch
    if (!g_edit || !g_edit->get_edit_state) return;
    if (g_edit->get_edit_state() != EDIT_HANDLE::EDIT_STATE_EDIT) return;
    s_inPeriodicPass = true;
    const DWORD now = GetTickCount();
    if (g_auto_proxy.load()) {
        if (g_watch_dirty.load() || (now - s_lastScan) >= 5000) {
            g_watch_dirty.store(false);
            s_lastScan = now;
            RunAutoProxy(1, L"watch");
        }
    } else if (g_watch_dirty.load() && (now - s_lastProtect) >= 5000) {
        // Cross-layer protection is independent of the switch, but scanning
        // the whole timeline on every idle tick would freeze the UI. Only a
        // real object/scene change wakes this pass.
        g_watch_dirty.store(false);
        s_lastProtect = now;
        RunProtectOnly();
    }
    s_inPeriodicPass = false;
}
static LRESULT CALLBACK TimerWndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_TIMER && w == 1) {
        RunPeriodicPass();
        return 0;
    }
    if (m == kMsgEditJob) {
        EditJob* job = reinterpret_cast<EditJob*>(l);
        if (job) {
            job->ok = (g_edit && g_edit->call_edit_section_param)
                ? g_edit->call_edit_section_param(job->param, job->thunk)
                : false;
            SetEvent(job->done);
        }
        return 0;
    }
    if (m == kMsgAutoProxyAll) {
        // Deferred from the menu callback (which owns an edit lock). By the
        // time this message is dispatched the lock is released.
        ConvertAllScenes(w != 0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
static void CreateTimerWindow() {
    if (g_timer_hwnd) return;
    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = &TimerWndProc;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.lpszClassName = kTimerClass;
    if (!RegisterClassExW(&wc)) {
        LogF(L"Timer: RegisterClassExW failed (%u)", GetLastError());
        return;
    }
    g_timer_hwnd = CreateWindowExW(0, kTimerClass, L"ProxyLayer timer", 0,
        0, 0, 0, 0, HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!g_timer_hwnd) {
        Log(L"Timer: CreateWindowExW failed (periodic passes disabled)");
        return;
    }
    SetTimer(g_timer_hwnd, 1, 2000, nullptr);
    LogF(L"Timer: UI-thread 2s periodic pass installed (uiThread=%u)",
         g_ui_thread);
}

// TEST ONLY: if a marker file is deployed, exercise the complete one-switch
// lifecycle unattended. Production packages never contain that marker.
static std::thread       g_autotest_thread;
static std::atomic<bool> g_autotest_stop{ false };
static bool AutoTestMarkerExists() {
    const std::wstring p = PluginDataFile(L"proxy-autotest.on");
    return !p.empty() && GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES;
}

// Simulates the user dropping a NEW big image onto an empty layer AFTER the
// switch was already on. The watcher must pick it up with no interaction.
static void AddImageThunk(void*, EDIT_SECTION* e) {
    std::string path;
    for (int layer = 0; layer <= 12 && path.empty(); ++layer) {
        OBJECT_HANDLE x = e->find_object ? e->find_object(layer, 0) : nullptr;
        if (!x) continue;
        EFFECT_HANDLE list[16] = {};
        const int n = e->get_effect_list ? e->get_effect_list(x, list, 16) : 0;
        for (int i = 0; i < n; ++i)
            if (ReadSourcePath(e, list[i], &path)) break;
    }
    if (path.empty() || !e->create_object) {
        Log(L"AutoProxy[test]: cannot add object (no source path or API)");
        return;
    }
    OBJECT_HANDLE o = e->create_object(L"画像ファイル", 8, 0, 30);
    if (!o) { Log(L"AutoProxy[test]: create_object failed"); return; }
    const bool ok = e->set_object_item_value
        ? e->set_object_item_value(o, L"画像ファイル", L"ファイル", path.c_str())
        : false;
    if (e->set_edited_state) e->set_edited_state();
    LogF(L"AutoProxy[test]: added 1 new image object (path set=%d)", (int)ok);
}
static void AutoTestWorker() {
    for (int i = 0; i < 120 && !g_autotest_stop.load(); ++i) Sleep(100);
    if (g_autotest_stop.load()) return;
    Log(L"AutoProxy[test]: marker found - flipping the switch ON (persistent)");
    WritePrefSwitch(true);
    g_auto_proxy.store(true);
    RunAutoProxy(1, L"test");

    for (int i = 0; i < 30 && !g_autotest_stop.load(); ++i) Sleep(100);
    if (g_autotest_stop.load()) return;
    if (g_edit && g_edit->call_edit_section_param) {
        int dummy = 0;
        CallEditSection(&dummy, &AddImageThunk);
    }
    g_watch_dirty.store(true);

    for (int i = 0; i < 60 && !g_autotest_stop.load(); ++i) Sleep(100);
    if (g_autotest_stop.load()) return;
    Log(L"AutoProxy[test]: flipping the switch OFF - restoring original inputs");
    WritePrefSwitch(false);
    const int n = RunAutoProxy(0, L"test");
    g_auto_proxy.store(false);
    LogF(L"AutoProxy[test]: restore converted %d object(s), mode=OFF", n);
}

//------------------------------------------------------------------------------
// Settings window.
//
// v0.2 first used register_window_client() (SDK WindowClient.cpp sample).
// That is WRONG for a settings dialog: the host converts the HWND into a
// borderless WS_CHILD, so there is no caption, no system menu, no X, and
// WM_CLOSE is never delivered -> once shown it can never be closed. The host
// also shrinks the client area, which clipped and overlapped our static text.
//
// Follow oov/aviutl2_gcmzdrops2 (src/c/config_dialog.rc + config_dialog.c):
// a real settings dialog is WS_POPUPWINDOW | WS_CAPTION, owned by the host
// HWND, with an explicit way out (IDOK/IDCANCEL -> EndDialog, caption X).
// Our switch applies immediately, so instead of OK/Cancel we use an owned
// popup with WM_CLOSE -> SW_HIDE (keeps the HWND so the menu can reopen it),
// an explicit 关闭 button, and a TOGGLING menu item.
//------------------------------------------------------------------------------
static const wchar_t* kPanelClass = L"ProxyLayerSettings";
static HWND g_panel = nullptr;
static HWND g_host_wnd = nullptr;
static HWND g_panel_check  = nullptr;
static HWND g_panel_status = nullptr;
enum : int { IDC_SWITCH = 1001, IDC_OK = 1002, IDC_CANCEL = 1003 };
// Client-area size (the outer window is computed with AdjustWindowRectEx).
// Bigger than v0.2's 300x220: users with large system fonts clipped the OK /
// Cancel buttons. WM_SIZE now re-lays out every control to the real client
// rect, so font size / DPI can never push buttons out of view again.
static const int kPanelW = 360, kPanelH = 270;
static HWND g_btn_ok = nullptr, g_btn_cancel = nullptr;
// Draft state: window edits change ONLY this. OK commits to the global
// preference; Cancel / caption X discards. Menu switches stay immediate.
static std::atomic<bool> g_panel_draft{ false };

static void SetChildFont(HWND h) {
    // Stock objects must NOT be deleted.
    SendMessageW(h, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), FALSE);
}
static void SaveGlobal(bool on) {
    g_auto_proxy.store(on);
    WritePrefSwitch(on);            // GLOBAL - never per-project
}
static void UpdatePanelStatus() {
    if (!g_panel_status) return;
    const bool cur = g_auto_proxy.load();
    const bool draft = g_panel_draft.load();
    const std::wstring fmt = NormNL(T(L"status",
        L"現在: %s   草稿: %s%s\n"
        L"OK を押すと草稿をグローバルに保存します。\n"
        L"全シーンに適用 - AUTO がピクセル密度から倍率を選びます。\n"
        L"出力時は元画像 / スクリプトは「元の幅」「元の高さ」を参照可能"));
    wchar_t buf[512];
    _snwprintf_s(buf, _countof(buf), _TRUNCATE, fmt.c_str(),
        g_auto_proxy.load() ? T(L"on", L"ON").c_str() : T(L"off", L"OFF").c_str(),
        draft ? T(L"on", L"ON").c_str() : T(L"off", L"OFF").c_str(),
        cur == draft ? L"" : T(L"unsaved", L"  (未保存)").c_str());
    SetWindowTextW(g_panel_status, buf);
    if (g_panel_check)
        CheckDlgButton(g_panel, IDC_SWITCH, draft ? BST_CHECKED : BST_UNCHECKED);
}
static void CommitPanel(HWND h) {
    const bool on = g_panel_draft.load();
    SaveGlobal(on);                                   // OK = save globally
    // One click must cover EVERY scene. CommitPanel is a WM_COMMAND handler
    // on the UI thread and owns no edit lock, so ConvertAllScenes() is legal.
    const int n = ConvertAllScenes(on);
    g_watch_dirty.store(true);
    LogF(L"Panel: OK -> saved globally auto-proxy=%s, "
         L"whole-project pass changed %d object(s)",
         on ? L"ON" : L"OFF", n);
    UpdatePanelStatus();
    if (h) ShowWindow(h, SW_HIDE);
}
static void RevertPanel(HWND h) {
    g_panel_draft.store(g_auto_proxy.load());        // Cancel = discard
    Log(L"Panel: Cancel -> not saved");
    if (h) ShowWindow(h, SW_HIDE);
}
static LRESULT CALLBACK PanelWndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE: {
        HINSTANCE hi = GetModuleHandleW(nullptr);
        const std::wstring sw = T(L"switch", L"自動プロキシ（全シーン、設定を保存）");
        const std::wstring ok = T(L"ok", L"OK");
        const std::wstring cancel = T(L"cancel", L"キャンセル");
        g_panel_check = CreateWindowExW(0, L"BUTTON", sw.c_str(),
            WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX, 12, 12, 336, 24,
            h, (HMENU)IDC_SWITCH, hi, nullptr);
        HWND sep = CreateWindowExW(0, L"STATIC", L"",
            WS_VISIBLE | WS_CHILD | SS_ETCHEDHORZ, 12, 46, 336, 2,
            h, nullptr, hi, nullptr);
        g_panel_status = CreateWindowExW(0, L"STATIC", L"",
            WS_VISIBLE | WS_CHILD | SS_LEFT | SS_NOPREFIX, 12, 56, 336, 150,
            h, nullptr, hi, nullptr);
        g_btn_ok = CreateWindowExW(0, L"BUTTON", ok.c_str(),
            WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON, 180, 216, 84, 30,
            h, (HMENU)IDC_OK, hi, nullptr);
        g_btn_cancel = CreateWindowExW(0, L"BUTTON", cancel.c_str(),
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 264, 216, 84, 30,
            h, (HMENU)IDC_CANCEL, hi, nullptr);
        SetChildFont(g_panel_check); SetChildFont(sep);
        SetChildFont(g_panel_status); SetChildFont(g_btn_ok); SetChildFont(g_btn_cancel);
        UpdatePanelStatus();
        return 0;
    }
    case WM_SIZE: {
        // Buttons are pinned to the bottom-right of the REAL client area; the
        // status block takes whatever height is left. Large system fonts can
        // no longer push the buttons outside the window.
        const int cw = (int)LOWORD(l), ch = (int)HIWORD(l);
        if (cw < 120 || ch < 120) return 0;
        const int margin = 12, btnW = 84, btnH = 30, gap = 8;
        const int btnY = ch - btnH - margin;
        if (g_btn_ok)
            SetWindowPos(g_btn_ok, nullptr, cw - margin - btnW * 2 - gap, btnY,
                         btnW, btnH, SWP_NOZORDER);
        if (g_btn_cancel)
            SetWindowPos(g_btn_cancel, nullptr, cw - margin - btnW, btnY,
                         btnW, btnH, SWP_NOZORDER);
        if (g_panel_check)
            SetWindowPos(g_panel_check, nullptr, margin, 12,
                         cw - margin * 2, 24, SWP_NOZORDER);
        if (g_panel_status) {
            const int top = 56, hgt = btnY - top - 10;
            SetWindowPos(g_panel_status, nullptr, margin, top,
                         cw - margin * 2, hgt > 40 ? hgt : 40, SWP_NOZORDER);
        }
        return 0;
    }
    case WM_COMMAND: {
        const int id = LOWORD(w);
        if (id == IDC_SWITCH) {
            // Draft only: nothing is applied or saved until OK.
            g_panel_draft.store(IsDlgButtonChecked(h, IDC_SWITCH) == BST_CHECKED);
            UpdatePanelStatus();
            return 0;
        }
        if (id == IDC_OK)     { CommitPanel(h); SetFocus(nullptr); return 0; }
        if (id == IDC_CANCEL) { RevertPanel(h); SetFocus(nullptr); return 0; }
        return 0;
    }
    case WM_CLOSE:
        // Caption X behaves like Cancel: discard the draft, never destroy the
        // HWND (the menu must be able to reopen it).
        RevertPanel(h);
        return 0;
    case WM_DESTROY:
        g_panel = nullptr; g_panel_check = nullptr; g_panel_status = nullptr;
        g_btn_ok = nullptr; g_btn_cancel = nullptr;
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
// The window starts hidden on purpose (a stray floating window previously
// confused the QSVEnc save-dialog matcher). Both menus TOGGLE it, so a second
// click always closes it. The switch keeps working with the window closed,
// because its state lives in the project + global preference file.
static void TogglePanel(const wchar_t* why) {
    if (!g_panel || !IsWindow(g_panel)) return;
    if (IsWindowVisible(g_panel)) {
        RevertPanel(g_panel);                       // closing = discard draft
        LogF(L"Panel: hidden by toggle (%s)", why);
    } else {
        g_panel_draft.store(g_auto_proxy.load());   // fresh draft on each open
        UpdatePanelStatus();
        ShowWindow(g_panel, SW_SHOW);
        SetForegroundWindow(g_panel);
        LogF(L"Panel: shown (%s)", why);
    }
}
static void OnConfigMenu(HWND, HINSTANCE) { TogglePanel(L"設定 menu"); }
static void OnMenuPanel(EDIT_SECTION*)    { TogglePanel(L"編集 menu"); }
static void CreatePanel(HOST_APP_TABLE* host) {
    if (!host) return;
    g_host_wnd = (g_edit && g_edit->get_host_app_window)
        ? g_edit->get_host_app_window() : nullptr;
    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = &PanelWndProc;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.lpszClassName = kPanelClass;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hCursor       = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
    if (!RegisterClassExW(&wc)) {
        LogF(L"Panel: RegisterClassExW failed (%u)", GetLastError());
        return;
    }
    // WS_POPUPWINDOW = WS_POPUP | WS_BORDER | WS_SYSMENU; + WS_CAPTION gives
    // a real title bar with an X. Same style as GCMZDrops Settings.
    const DWORD style = WS_POPUPWINDOW | WS_CAPTION;
    RECT rc{ 0, 0, kPanelW, kPanelH };
    AdjustWindowRectEx(&rc, style, FALSE, WS_EX_DLGMODALFRAME);
    const int winW = rc.right - rc.left, winH = rc.bottom - rc.top;
    const std::wstring title = T(L"title", L"ProxyLayer 自動プロキシ");
    g_panel = CreateWindowExW(WS_EX_DLGMODALFRAME, kPanelClass,
        title.c_str(), style,
        CW_USEDEFAULT, CW_USEDEFAULT, winW, winH,
        g_host_wnd, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!g_panel) { Log(L"Panel: CreateWindowExW failed"); return; }
    // Centre over the host without showing or activating it.
    if (g_host_wnd && IsWindow(g_host_wnd)) {
        RECT r{};
        if (GetWindowRect(g_host_wnd, &r)) {
            const int x = r.left + ((r.right - r.left) - winW) / 2;
            const int y = r.top  + ((r.bottom - r.top)  - winH) / 2;
            SetWindowPos(g_panel, HWND_TOP, x, y, 0, 0,
                         SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }
    Log(L"Panel: settings dialog created (hidden, closable)");
}

// TEST ONLY: prove the window can actually be dismissed through every path.
static std::thread g_paneltest_thread;
static bool PanelTestMarkerExists() {
    const std::wstring p = PluginDataFile(L"proxy-paneltest.on");
    return !p.empty() && GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES;
}
static void PanelTestWorker() {
    for (int i = 0; i < 100 && !g_watch_stop.load(); ++i) Sleep(100);
    if (g_watch_stop.load()) return;
    if (!g_panel || !IsWindow(g_panel)) {
        Log(L"Panel[test]: VERDICT=FAIL (no window)");
        return;
    }
    ShowWindow(g_panel, SW_SHOW);
    const bool shown = IsWindowVisible(g_panel) != FALSE;
    SendMessageW(g_panel, WM_CLOSE, 0, 0);            // caption X
    const bool hiddenByClose = IsWindowVisible(g_panel) == FALSE;
    const bool alive = IsWindow(g_panel) != FALSE;     // must NOT be destroyed
    ShowWindow(g_panel, SW_SHOW);
    SendMessageW(g_panel, WM_COMMAND, MAKEWPARAM(IDC_CANCEL, BN_CLICKED), 0);
    const bool hiddenByButton = IsWindowVisible(g_panel) == FALSE;
    TogglePanel(L"test");
    const bool shownByToggle = IsWindowVisible(g_panel) != FALSE;
    TogglePanel(L"test");
    const bool hiddenByToggle = IsWindowVisible(g_panel) == FALSE;
    // OK must save globally; Cancel / X must not.
    const bool globalBefore = PrefSwitchExists();
    g_panel_draft.store(!globalBefore);
    RevertPanel(nullptr);                              // Cancel
    const bool cancelKept = (PrefSwitchExists() == globalBefore);
    g_panel_draft.store(!globalBefore);
    SaveGlobal(g_panel_draft.load());                   // OK
    const bool okWrote = (PrefSwitchExists() == g_panel_draft.load());
    WritePrefSwitch(globalBefore); g_auto_proxy.store(globalBefore);
    // Real OK button click: saves globally, applies, and closes.
    ShowWindow(g_panel, SW_SHOW);
    g_panel_draft.store(g_auto_proxy.load());
    SendMessageW(g_panel, WM_COMMAND, MAKEWPARAM(IDC_OK, BN_CLICKED), 0);
    const bool hiddenByOk = IsWindowVisible(g_panel) == FALSE;
    WritePrefSwitch(globalBefore); g_auto_proxy.store(globalBefore);
    LogF(L"Panel[test]: show=%d X hide=%d alive=%d cancel hide=%d "
         L"toggle show=%d toggle hide=%d cancelKept=%d okWrote=%d ok hide=%d",
         (int)shown, (int)hiddenByClose, (int)alive, (int)hiddenByButton,
         (int)shownByToggle, (int)hiddenByToggle,
         (int)cancelKept, (int)okWrote, (int)hiddenByOk);
    LogF(L"Panel[test]: VERDICT=%s",
         (shown && hiddenByClose && alive && hiddenByButton &&
          shownByToggle && hiddenByToggle && cancelKept && okWrote && hiddenByOk)
             ? L"PASS" : L"FAIL");
}

// TEST ONLY: strict whitelist + downstream compensation data.
static std::thread g_safety_thread;
static bool SafetyTestMarkerExists() {
    const std::wstring p = PluginDataFile(L"proxy-safety-test.on");
    return !p.empty() && GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES;
}
struct SafetyArgs { int proxies = 0, srcW = 0, srcH = 0; };
static void SafetyCheckThunk(void* p, EDIT_SECTION* e) {
    SafetyArgs* a = static_cast<SafetyArgs*>(p);
    EDIT_INFO info{};
    if (g_edit && g_edit->get_edit_info) g_edit->get_edit_info(&info, sizeof(info));
    for (int l = 0; l <= std::min(std::max(info.layer_max, 0), 20); ++l) {
        OBJECT_HANDLE o = e->find_object ? e->find_object(l, 0) : nullptr;
        if (!o) continue;
        EFFECT_HANDLE list[32] = {};
        const int n = e->get_effect_list ? e->get_effect_list(o, list, 32) : 0;
        for (int i = 0; i < n; ++i) {
            LPCWSTR nm = e->get_effect_name ? e->get_effect_name(list[i]) : nullptr;
            if (!nm || std::wcscmp(nm, L"ProxyLayer") != 0) continue;
            ++a->proxies;
            LPCSTR w = e->get_effect_item_value(list[i], L"元の幅");
            LPCSTR h = e->get_effect_item_value(list[i], L"元の高さ");
            if (w && *w) a->srcW = std::atoi(w);
            if (h && *h) a->srcH = std::atoi(h);
        }
    }
}
static void SafetyTestWorker() {
    for (int i = 0; i < 130 && !g_watch_stop.load(); ++i) Sleep(100);
    if (g_watch_stop.load()) return;
    const struct { const wchar_t* n; bool e; } names[] = {
        { L"画像ファイル", true }, { L"画像ファイル (アルファ付き)", true },
        { L"画像文件", true }, { L"Image File", true },
        { L"音声ファイル", false }, { L"動画ファイル", false },
        { L"3Dモデル", false }, { L"OBJファイル", false },
        { L"VRMブリッジ", false }, { L"体積光", false },
        { L"標準描画", false }, { nullptr, false }
    };
    int nOk = 0, nTotal = 0;
    for (int i = 0; names[i].n; ++i) {
        ++nTotal;
        if (IsImageEffect(names[i].n) == names[i].e) ++nOk;
    }
    const struct { const char* p; bool e; } paths[] = {
        { "a.png", true }, { "B.JPG", true }, { "c.tiff", true },
        { "d.wav", false }, { "e.obj", false }, { "f.vrm", false },
        { "g.mp4", false }
    };
    int pOk = 0, pTotal = 0;
    for (const auto& x : paths) {
        ++pTotal;
        if (IsImagePath(x.p) == x.e) ++pOk;
    }
    SafetyArgs a{};
    if (g_edit && g_edit->call_edit_section_param)
        CallEditSection(&a, &SafetyCheckThunk);
    const bool compOk = (a.proxies > 0 && a.srcW > 0 && a.srcH > 0);
    LogF(L"Safety[test]: names %d/%d paths %d/%d proxies=%d 元の幅=%d 元の高さ=%d",
         nOk, nTotal, pOk, pTotal, a.proxies, a.srcW, a.srcH);
    LogF(L"Safety[test]: VERDICT=%s",
         (nOk == nTotal && pOk == pTotal && compOk) ? L"PASS" : L"FAIL");
}

//------------------------------------------------------------------------------
// Exported entry points
//------------------------------------------------------------------------------
extern "C" __declspec(dllexport) FILTER_PLUGIN_TABLE* GetFilterPluginTable(void) {
    return &filter_plugin_table;
}
extern "C" __declspec(dllexport) bool InitializePlugin(DWORD version) {
    g_host_version = version;
    LogF(L"InitializePlugin host=%u (0x%08X)", version, version);
    return true;
}
extern "C" __declspec(dllexport) void UninitializePlugin() {
    PerfFlush("EXIT");        // never lose the last (short) telemetry window
    g_watch_stop.store(true);
    g_autotest_stop.store(true);
    // Stop the UI-thread timer FIRST: no new edit-section jobs can start
    // while the test workers are being joined.
    if (g_timer_hwnd && IsWindow(g_timer_hwnd)) {
        KillTimer(g_timer_hwnd, 1);
        DestroyWindow(g_timer_hwnd);
    }
    g_timer_hwnd = nullptr;
    if (g_autotest_thread.joinable()) g_autotest_thread.join();
    if (g_paneltest_thread.joinable()) g_paneltest_thread.join();
    if (g_safety_thread.joinable()) g_safety_thread.join();
    if (g_panel && IsWindow(g_panel)) DestroyWindow(g_panel);
    g_panel = nullptr;
    UnregisterClassW(kPanelClass, GetModuleHandleW(nullptr));
    UnregisterClassW(kTimerClass, GetModuleHandleW(nullptr));
}
extern "C" __declspec(dllexport) void InitializeLogger(LOG_HANDLE* logger) {
    g_logger = logger;
}
extern "C" __declspec(dllexport) void InitializeCache(CACHE_HANDLE* cache) {
    g_cache = cache;
}
extern "C" __declspec(dllexport) void RegisterPlugin(HOST_APP_TABLE* host) {
    g_host = host;
    if (!host) return;
    if (host->create_edit_handle) g_edit = host->create_edit_handle();
    // The host main window's thread IS the UI thread that owns EDIT_SECTION.
    // Capture it before creating anything so off-thread edit calls can be
    // detected (and marshalled) instead of crashing the host.
    HWND hostWnd = (g_edit && g_edit->get_host_app_window)
        ? g_edit->get_host_app_window() : nullptr;
    g_ui_thread = (hostWnd && IsWindow(hostWnd))
        ? GetWindowThreadProcessId(hostWnd, nullptr) : GetCurrentThreadId();
    CreateTimerWindow();
    // Read the global switch HERE, not only in OnProjectLoad. A restored
    // session or a project opened from the command line does not necessarily
    // fire the project-load callback, which left the switch OFF forever.
    g_auto_proxy.store(PrefSwitchExists());
    g_watch_dirty.store(true);
    LogF(L"RegisterPlugin: auto-proxy=%s (global preference)",
         g_auto_proxy.load() ? L"ON" : L"OFF");
    if (host->register_filter_plugin) host->register_filter_plugin(&filter_plugin_table);
    if (host->register_clear_cache_handler) host->register_clear_cache_handler(&OnClearCache);
    if (host->register_project_load_handler) host->register_project_load_handler(&OnProjectLoad);
    if (host->register_project_save_handler) host->register_project_save_handler(&OnProjectSave);
    if (host->register_event_listener) {
        host->register_event_listener(EVENT_TYPE::CHANGE_EDIT_STATE, nullptr, &OnStateChanged);
        // Wake the background watcher whenever the scene/objects change. The
        // listener itself only sets a flag - EDIT_SECTION writes are illegal
        // inside an event callback.
        host->register_event_listener(EVENT_TYPE::UPDATE_OBJECT, nullptr, &OnObjectsUpdated);
        host->register_event_listener(EVENT_TYPE::CHANGE_EDIT_SCENE, nullptr, &OnObjectsUpdated);
        host->register_event_listener(EVENT_TYPE::CHANGE_FOCUS_OBJECT, nullptr, &OnObjectsUpdated);
    }
    // One switch for the whole scene: available from both the 編集 menu and
    // the object right-click menu. '\' creates a submenu.
    // register_*_menu() may keep the pointer, so store the translated strings
    // in process-lifetime buffers rather than passing a temporary.
    static std::wstring m[6];
    m[0] = L"ProxyLayer\\" + T(L"menu.auto", L"自動プロキシ (ON/OFF)");
    m[1] = L"ProxyLayer\\" + T(L"menu.settings", L"設定ウィンドウ (表示/非表示)");
    m[2] = L"ProxyLayer\\" + T(L"menu.keepall", L"全レイヤーを元画像に維持");
    m[3] = L"ProxyLayer\\" + T(L"menu.restore", L"プロキシを全て復元");
    m[4] = m[0];
    m[5] = T(L"title", L"ProxyLayer 自動プロキシ");
    if (host->register_edit_menu) {
        host->register_edit_menu(m[0].c_str(), &OnMenuAutoProxy);
        host->register_edit_menu(m[1].c_str(), &OnMenuPanel);
        host->register_edit_menu(m[2].c_str(), &OnMenuKeepAll);
        host->register_edit_menu(m[3].c_str(), &OnMenuRestoreAll);
    }
    if (host->register_object_menu)
        host->register_object_menu(m[4].c_str(), &OnMenuAutoProxy);
    if (host->register_config_menu)
        host->register_config_menu(m[5].c_str(), &OnConfigMenu);
    const bool lifecycleTest = AutoTestMarkerExists();
    const bool panelTest = PanelTestMarkerExists();
    const bool safetyTest = SafetyTestMarkerExists();
    // Lifecycle-only automation hosts deliberately get NO extra window, so the
    // QSVEnc save-dialog matcher can never be confused. Panel-close tests do
    // need the HWND, but it stays hidden except for a few milliseconds.
    if (lifecycleTest && !panelTest && !safetyTest) {
        Log(L"RegisterPlugin: test marker -> settings window disabled");
    } else {
        CreatePanel(host);
    }
    if (lifecycleTest) g_autotest_thread  = std::thread(&AutoTestWorker);
    if (panelTest)     g_paneltest_thread = std::thread(&PanelTestWorker);
    if (safetyTest)    g_safety_thread    = std::thread(&SafetyTestWorker);
    // NO background watcher thread any more: RunPeriodicPass() is driven by
    // the UI-thread WM_TIMER created above.
    LogF(L"RegisterPlugin: editHandle=%p cache=%p logger=%p",
         (void*)g_edit, (void*)g_cache, (void*)g_logger);
}

//------------------------------------------------------------------------------
// Pixel helpers
//------------------------------------------------------------------------------
static inline float HalfToFloat(uint16_t h) {
    uint32_t sign = (uint32_t)(h & 0x8000u) << 16;
    uint32_t e = (h >> 10) & 0x1Fu, m = h & 0x3FFu, f;
    if (e == 0) {
        if (m == 0) f = sign;
        else {
            uint32_t ee = 113;
            while (!(m & 0x400u)) { m <<= 1; --ee; }
            m &= 0x3FFu;
            f = sign | (ee << 23) | (m << 13);
        }
    } else if (e == 31) {
        f = sign | 0x7F800000u | (m << 13);
    } else {
        f = sign | ((e + 112u) << 23) | (m << 13);
    }
    float out; std::memcpy(&out, &f, 4); return out;
}
static inline uint8_t U8(float x) {
    if (!(x > 0.f)) return 0;           // also catches NaN
    if (x >= 1.f) return 255;
    return (uint8_t)(x * 255.f + 0.5f);
}
static inline uint8_t U16To8(uint16_t x) { return (uint8_t)((x + 128u) / 257u); }

struct SourceView {
    const uint8_t* data = nullptr;
    int w = 0, h = 0, pitch = 0;
    INPUT_PIXEL_FORMAT fmt = INPUT_PIXEL_FORMAT::RGBA;
};

// Returns the source pixel as PIXEL_RGBA, preserving the host's own alpha
// convention. filter2.h defines INPUT_PIXEL_FORMAT::RGBA as
// "DXGI_FORMAT_R8G8B8A8_UNORM ※PIXEL_RGBA" - i.e. the SAME struct and the
// SAME premultiplied representation that set_image_data() consumes. Only a
// channel swap / 16-to-8 conversion is needed here. Pre-multiplying RGB by
// alpha again (v0.3.1 and earlier) dimmed every translucent pixel twice:
// measured 50%-alpha cyan rendered at ~0.56x brightness. Do NOT re-add it.
static inline PIXEL_RGBA ReadSourcePixel(const SourceView& s, int x, int y) {
    const uint8_t* p = s.data + (size_t)y * (size_t)s.pitch;
    PIXEL_RGBA o{};
    switch (s.fmt) {
    case INPUT_PIXEL_FORMAT::RGBA: { // R8G8B8A8_UNORM, host-native PIXEL_RGBA
        const uint8_t* q = p + (size_t)x * 4u;
        o = PIXEL_RGBA{ q[0], q[1], q[2], q[3] };
        break;
    }
    case INPUT_PIXEL_FORMAT::BGRA: { // B8G8R8A8_UNORM, swap R/B only
        const uint8_t* q = p + (size_t)x * 4u;
        o = PIXEL_RGBA{ q[2], q[1], q[0], q[3] };
        break;
    }
    case INPUT_PIXEL_FORMAT::BGR: { // B8G8R8X8_UNORM, opaque
        const uint8_t* q = p + (size_t)x * 4u;
        o = PIXEL_RGBA{ q[2], q[1], q[0], 255 };
        break;
    }
    case INPUT_PIXEL_FORMAT::PA64: { // R16G16B16A16_UNORM, premultiplied
        const uint16_t* q = (const uint16_t*)(p + (size_t)x * 8u);
        o = PIXEL_RGBA{ U16To8(q[0]), U16To8(q[1]), U16To8(q[2]), U16To8(q[3]) };
        break;
    }
    case INPUT_PIXEL_FORMAT::HF64: { // R16G16B16A16_FLOAT, premultiplied
        const uint16_t* q = (const uint16_t*)(p + (size_t)x * 8u);
        o = PIXEL_RGBA{ U8(HalfToFloat(q[0])), U8(HalfToFloat(q[1])),
                        U8(HalfToFloat(q[2])), U8(HalfToFloat(q[3])) };
        break;
    }
    default: { // best effort: treat as BGRA
        const uint8_t* q = p + (size_t)x * 4u;
        o = PIXEL_RGBA{ q[2], q[1], q[0], q[3] };
        break;
    }
    }
    return o;
}

// Box downsample in the host's premultiplied space (linear, so translucent
// edges keep correct colour/alpha without un-premultiply round trips).
static std::vector<PIXEL_RGBA> Downsample(const SourceView& s, int div) {
    const int pw = std::max(1, (s.w + div - 1) / div);
    const int ph = std::max(1, (s.h + div - 1) / div);
    std::vector<PIXEL_RGBA> out((size_t)pw * (size_t)ph);
    for (int py = 0; py < ph; ++py) {
        for (int px = 0; px < pw; ++px) {
            uint32_t r = 0, g = 0, b = 0, a = 0, n = 0;
            const int y0 = py * div, y1 = std::min(s.h, y0 + div);
            const int x0 = px * div, x1 = std::min(s.w, x0 + div);
            for (int y = y0; y < y1; ++y) {
                for (int x = x0; x < x1; ++x) {
                    PIXEL_RGBA q = ReadSourcePixel(s, x, y);
                    r += q.r; g += q.g; b += q.b; a += q.a; ++n;
                }
            }
            if (!n) n = 1;
            out[(size_t)py * pw + px] = PIXEL_RGBA{
                (uint8_t)(r / n), (uint8_t)(g / n), (uint8_t)(b / n), (uint8_t)(a / n) };
        }
    }
    return out;
}

// Convert the host premultiplied buffer -> straight alpha for PNG files.
static std::vector<uint8_t> ToStraightRGBA8(const std::vector<PIXEL_RGBA>& pm) {
    std::vector<uint8_t> out(pm.size() * 4u);
    for (size_t i = 0; i < pm.size(); ++i) {
        const uint8_t a = pm[i].a;
        if (a == 0) { out[i * 4] = out[i * 4 + 1] = out[i * 4 + 2] = 0; }
        else {
            out[i * 4 + 0] = (uint8_t)std::min(255, (int)pm[i].r * 255 / a);
            out[i * 4 + 1] = (uint8_t)std::min(255, (int)pm[i].g * 255 / a);
            out[i * 4 + 2] = (uint8_t)std::min(255, (int)pm[i].b * 255 / a);
        }
        out[i * 4 + 3] = a;
    }
    return out;
}

//------------------------------------------------------------------------------
// Proxy store: official CACHE_HANDLE (cross-frame, main memory) + proxy PNG
//------------------------------------------------------------------------------
struct ProxyEntry {
    int div = 1, pw = 0, ph = 0;
    std::wstring key;             // content hash, also used for resource naming
    std::vector<PIXEL_RGBA> pm;   // premultiplied, for set_image_data()
    std::wstring png_path;        // for "image:" VRAM-cached resource
    bool png_ready = false;
    bool marked = false;
};
static std::unordered_map<std::wstring, ProxyEntry> g_proxies;

// Source dimensions remembered per (path + size + mtime). Once a proxy has
// been built we never need the host to hand us the FULL decoded original again:
// asking for it every frame forces the host to decode (or thrash its LRU with)
// 40 MPx images. PV.aup2 has ~100 such sources, so this was the dominant cost.
struct SrcDims { int w = 0, h = 0; };
static std::unordered_map<std::wstring, SrcDims> g_src_dims;  // guarded by g_mu

// AUTO level: degrade only when the source is genuinely large. Below ~2 Mpx a
// preview proxy costs more than it saves, so leave it alone entirely.
// AUTO v2 chose div from the SOURCE / SCENE pixel ratio, targeting ~1 texel
// per screen pixel. AUTO v3 fixes a real-project blind spot discovered from
// ProxyLayer-perf.log telemetry:
//
//   FILTER_PROC_VIDEO::scene describes the scene CURRENTLY BEING RENDERED.
//   A PV project often keeps character art in a huge sub-scene (this user's
//   PV工程.aup2 has two 10000x6250 = 62.5 Mpx sub-scenes) that is then scaled
//   down into a 2048x1080 master. A 62.5 Mpx source inside a 62.5 Mpx scene
//   yields need = 1.00 -> div = 1 -> NO PROXY AT ALL, so every frame decodes
//   the full-resolution original. Measured on that project: auto1 = 69.6% of
//   all calls, decode_avg = 56 ms, decode_max = 313 ms, fast = 28.4%.
//
//   A canvas that large is a COMPOSITING WORK CANVAS, not what the viewer
//   sees; only the master composite (here 2048x1080) is displayed. Clamp the
//   effective "screen" to a 1080p-class preview budget. Canvases up to 4K are
//   left completely untouched, so AUTO v2's deliberate "4K is sharper" rule
//   still holds for real master scenes. Preview-only: export restores L0.
//
//   10000x6250 sub-scene, 62.5 Mpx src : sqrt(62.5/2.77)=4.75 -> 8  (was 1)
//    8000x5000 sub-scene, 40.0 Mpx src : sqrt(40.0/2.77)=3.80 -> 8  (was 1)
//    3840x2160 master,    56.0 Mpx src : sqrt(56.0/8.29)=2.60 -> 4  (unchanged)
//    1920x1080 master,     4.19 Mpx src: sqrt( 4.19/2.07)=1.42 -> 2  (unchanged)
//    1920x1080 master,     2.07 Mpx src: sqrt( 2.07/2.07)=1.00 -> 1  (unchanged)
//
// Measured frame times (12 x 7000x8000, VramCacheSize=1024) justify the
// aggressive preview-only downscale: div=1 ~1216 ms, div=4 ~64 ms,
// div=8 ~19 ms. The export gate always restores the original.
static constexpr double kPreviewBudgetPx   = 2048.0 * 1080.0 * 1.25; // ~2.77 Mpx
static constexpr double kOversizedScenePx  = 3840.0 * 2160.0 * 1.50; // ~12.44 Mpx
static constexpr double kHugeSourcePx      = 16.0 * 1000000.0;        // 16 Mpx
// composited = this object lives in a sub-scene being rendered INTO another
// scene (SCENE_INFO::origin_id != id). Its canvas is never the final picture.
static double EffectiveScenePixels(int sw, int sh, bool composited) {
    double scene = (double)sw * (double)sh;
    if (scene < 1.0) scene = 1920.0 * 1080.0;   // sane fallback
    // Sub-scenes and oversized work canvases are clamped; an ordinary 1080p/4K
    // master scene keeps its true resolution (AUTO v2 "4K is sharper" holds).
    if (composited || scene > kOversizedScenePx) {
        if (scene > kPreviewBudgetPx) scene = kPreviewBudgetPx;
    }
    return scene;
}
static int DivFromNeed(double need) {
    if (need <= 1.0001) return 1;               // source is not larger than screen
    int div = 1;
    while (div < 8 && (double)div < need) div <<= 1;
    return div;
}
// capped (optional) reports whether the v3 rules raised div above what the
// raw scene resolution would have produced. Telemetry only; no behaviour.
static int AutoDiv(int w, int h, int sw, int sh, bool composited,
                   bool* capped = nullptr) {
    const double src = (double)w * (double)h;
    double raw = (double)sw * (double)sh;
    if (raw < 1.0) raw = 1920.0 * 1080.0;
    const int rawDiv = DivFromNeed(std::sqrt(src / raw));
    int div = DivFromNeed(std::sqrt(src / EffectiveScenePixels(sw, sh, composited)));
    // Absolute floor: a genuinely huge source must NEVER stay at div=1 in
    // preview, whatever the scene reports. Otherwise every frame decodes the
    // full-resolution original (measured 56 ms avg / 313 ms peak). Manual
    // levels bypass AutoDiv entirely, so users keep a full-resolution escape.
    if (div == 1 && src >= kHugeSourcePx) div = 2;
    if (capped) *capped = (div > rawDiv);
    return div;
}
static double AutoNeed(int w, int h, int sw, int sh, bool composited) {
    const double src = (double)w * (double)h;
    return std::sqrt(src / EffectiveScenePixels(sw, sh, composited));
}

// Some hosts run with a restricted token: %TEMP% can be writable from a normal
// process yet return ERROR_ACCESS_DENIED from inside the render worker. Probe
// several candidate directories and actually verify writability with a temp
// file, rather than assuming CreateDirectoryW() succeeding is enough.
static bool TryMakeWritableDir(const std::wstring& d, DWORD* err) {
    if (d.empty()) { *err = ERROR_INVALID_PARAMETER; return false; }
    DWORD a = GetFileAttributesW(d.c_str());
    if (a == INVALID_FILE_ATTRIBUTES) {
        if (!CreateDirectoryW(d.c_str(), nullptr)) { *err = GetLastError(); return false; }
    } else if (!(a & FILE_ATTRIBUTE_DIRECTORY)) {
        *err = ERROR_ALREADY_EXISTS; return false;
    }
    const std::wstring probe = d + L"\\.pl_write_test";
    HANDLE h = CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_HIDDEN, nullptr);
    if (h == INVALID_HANDLE_VALUE) { *err = GetLastError(); return false; }
    CloseHandle(h);
    DeleteFileW(probe.c_str());
    *err = 0;
    return true;
}

static std::wstring CacheDir() {
    static std::wstring resolved;
    static bool done = false;
    if (done) return resolved;
    done = true;                       // resolve once; do not spam the log

    std::vector<std::wstring> cands;
    wchar_t tmp[MAX_PATH] = {};
    if (GetTempPathW(MAX_PATH, tmp) && tmp[0])
        cands.push_back(std::wstring(tmp) + L"aviutl2_proxylayer");

    // Cache must live INSIDE the host's writable data folder (derived from the
    // DLL location), never next to aviutl2.exe. Creating <exedir>\data makes
    // AviUtl2 switch to a portable data root and silently reset the entire
    // environment (language/plugins/layout) to defaults.
    const std::wstring dllCache = PluginDataFile(L"ProxyLayerCache");
    if (!dllCache.empty()) cands.push_back(dllCache);
    wchar_t la[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", la, MAX_PATH) && la[0])
        cands.push_back(std::wstring(la) + L"\\AviUtl2ProxyLayer");

    for (const auto& c : cands) {
        DWORD err = 0;
        if (TryMakeWritableDir(c, &err)) {
            resolved = c + L"\\";
            LogF(L"CacheDir selected: %s", c.c_str());
            return resolved;
        }
        LogF(L"CacheDir rejected: %s err=%u", c.c_str(), err);
    }
    Log(L"CacheDir: no writable dir; proxies stay in memory only");
    return L"";                        // GetProxy() then skips the on-disk PNG
}

static std::wstring MakeKey(const std::wstring& file, int div, int w, int h,
                            bool mark) {
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    uint64_t sz = 0, mt = 0;
    if (GetFileAttributesExW(file.c_str(), GetFileExInfoStandard, &fad)) {
        sz = ((uint64_t)fad.nFileSizeHigh << 32) | fad.nFileSizeLow;
        mt = ((uint64_t)fad.ftLastWriteTime.dwHighDateTime << 32) |
             fad.ftLastWriteTime.dwLowDateTime;
    }
    uint64_t hv = 1469598103934665603ull;
    auto mix = [&](uint64_t x) { hv ^= x; hv *= 1099511628211ull; };
    for (wchar_t c : file) { mix((unsigned char)(c & 0xFF)); mix((unsigned char)(c >> 8)); }
    mix(sz); mix(mt); mix((uint64_t)div); mix((uint64_t)w); mix((uint64_t)h);
    mix((uint64_t)(mark ? 1 : 0));
    mix(0x50524F58'76303200ull); // "PROXv02\0" schema tag (v02 = mark in key)
    wchar_t buf[48];
    _snwprintf_s(buf, _countof(buf), _TRUNCATE, L"%016llX_%d_%dx%d", hv, div, w, h);
    return buf;
}

// Stable key for one source file, independent of div / scene resolution.
static std::wstring FileKeyFor(const std::wstring& file) {
    return MakeKey(file, 0, 0, 0, false);   // hashes path + size + mtime
}
static ProxyEntry* FindProxy(const std::wstring& key) {
    std::lock_guard<std::mutex> lk(g_mu);
    auto it = g_proxies.find(key);
    return (it != g_proxies.end() && !it->second.pm.empty())
        ? &it->second : nullptr;
}
static bool WriteProxyPng(const std::wstring& path, const ProxyEntry& e) {
    if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) return true;
    std::vector<uint8_t> straight = ToStraightRGBA8(e.pm);
    std::vector<uint8_t> bytes = png::encode_rgba8(straight.data(), e.pw, e.ph);
    HANDLE hf = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                            CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hf == INVALID_HANDLE_VALUE) {
        LogF(L"proxy PNG create failed: %s err=%u", path.c_str(), GetLastError());
        return false;
    }
    DWORD written = 0;
    BOOL ok = WriteFile(hf, bytes.data(), (DWORD)bytes.size(), &written, nullptr);
    CloseHandle(hf);
    if (!ok || written != bytes.size()) { DeleteFileW(path.c_str()); return false; }
    return true;
}

static ProxyEntry* GetProxy(const std::wstring& file, int div,
                            const SourceView& src, bool mark) {
    const std::wstring key = MakeKey(file, div, src.w, src.h, mark);
    std::lock_guard<std::mutex> lk(g_mu);
    auto it = g_proxies.find(key);
    if (it != g_proxies.end()) return &it->second;

    ProxyEntry e;
    e.div = div;
    e.key = key;
    e.pw = std::max(1, (src.w + div - 1) / div);
    e.ph = std::max(1, (src.h + div - 1) / div);

    // 1) official cross-frame main-memory image cache (cache2.h)
    const std::wstring cname = L"ProxyLayer\\" + key;
    if (g_cache && g_cache->get_image_cache && g_cache->create_image_cache) {
        CACHE_IMAGE c = g_cache->get_image_cache(g_cache, cname.c_str());
        if (c && c.width == e.pw && c.height == e.ph) {
            e.pm.assign(c.buffer, c.buffer + (size_t)e.pw * e.ph);
        } else {
            e.pm = Downsample(src, div);
            CACHE_IMAGE nc = g_cache->create_image_cache(g_cache, cname.c_str(), e.pw, e.ph);
            if (nc && nc.buffer)
                std::memcpy(nc.buffer, e.pm.data(), e.pm.size() * sizeof(PIXEL_RGBA));
        }
    } else {
        e.pm = Downsample(src, div);
    }

    // Debug marker BEFORE the PNG is written, so the "image:" VRAM texture and
    // the set_image_data() path both show it. Magenta is deliberately chosen:
    // the alignment fiducial detector only accepts pure-ish red (b < 95).
    if (mark) {
        const int bw = std::max(2, e.ph / 40);
        for (int y = 0; y < bw; ++y)
            for (int x = 0; x < e.pw; ++x)
                e.pm[(size_t)y * e.pw + x] = PIXEL_RGBA{ 255, 0, 255, 255 };
        e.marked = true;
    }

    // 2) proxy PNG on disk, loaded by host as "image:<path>" -> VRAM resident
    const std::wstring dir = CacheDir();
    if (!dir.empty()) {
        e.png_path = dir + key + L".png";
        e.png_ready = WriteProxyPng(e.png_path, e);
    }
    LogF(L"proxy built key=%s div=%d %dx%d -> %dx%d png=%d",
         key.c_str(), div, src.w, src.h, e.pw, e.ph, (int)e.png_ready);

    auto res = g_proxies.emplace(key, std::move(e));
    return &res.first->second;
}

// Safe fallback for every proxy failure: reproduce the original full-res image
// rather than returning false with nothing drawn (which yields a blank object).
static bool FallbackL0(FILTER_PROC_VIDEO* v, const SourceView& src, const wchar_t* why) {
    if (why && *why) {
        static std::atomic<int> n{ 0 };   // do not flood the host log at 30 fps
        if (n.fetch_add(1) < 5) Log(why);
    }
    const int W = src.w, H = src.h;
    // L0 used to re-convert the WHOLE image pixel-by-pixel and allocate a
    // second full-size buffer. For a 10000x6250 source that is a 250 MB
    // allocation + 62.5M virtual calls per frame, which showed up as
    // ~300 ms per filter invocation on real projects. The host's file cache
    // is already PIXEL_RGBA, so hand it straight to set_image_data().
    if (src.fmt == INPUT_PIXEL_FORMAT::RGBA && src.pitch == W * 4) {
        v->set_image_data(reinterpret_cast<const PIXEL_RGBA*>(src.data), W, H);
        return true;
    }
    std::vector<PIXEL_RGBA> full((size_t)W * H);
    if (src.fmt == INPUT_PIXEL_FORMAT::RGBA && src.pitch > W * 4) {
        // Padded rows: one memcpy per row instead of one call per pixel.
        for (int y = 0; y < H; ++y)
            std::memcpy(full.data() + (size_t)y * W,
                        src.data + (size_t)y * (size_t)src.pitch,
                        (size_t)W * sizeof(PIXEL_RGBA));
    } else {
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x)
                full[(size_t)y * W + x] = ReadSourcePixel(src, x, y);
    }
    v->set_image_data(full.data(), W, H);
    return true;
}

// Fast path keeps src.data == nullptr. Only a rare texture rejection needs the
// real full-resolution buffer, so decode it lazily there instead of on every
// frame.
static bool LazyL0(FILTER_PROC_VIDEO* v, const SourceView& src,
                   const std::wstring& file, const wchar_t* why) {
    if (src.data) return FallbackL0(v, src, why);
    if (!g_cache || !g_cache->get_image_file_cache) {
        Log(L"lazy L0: image cache unavailable"); return false;
    }
    CACHE_FILE_IMAGE img = g_cache->get_image_file_cache(file.c_str());
    if (!img || !img.buffer || img.width <= 0 || img.height <= 0) {
        Log(L"lazy L0: source decode failed");
        if (why && *why) Log(why);
        return false;
    }
    SourceView real{ (const uint8_t*)img.buffer, img.width, img.height,
                     img.pitch > 0 ? img.pitch : img.width * 4, img.format };
    return FallbackL0(v, real, why);
}

// Production path: upload the 1/div proxy texture, then scale the object's
// relative draw size back up. Geometry, camera and the whole downstream
// effect chain therefore stay identical to the original full-res image.
static bool DrawProxy(FILTER_PROC_VIDEO* v, ProxyEntry& px, int div) {
    v->set_image_data(px.pm.data(), px.pw, px.ph);
    // param is a RELATIVE setting (same as obj.ox / obj.sx).
    v->param->sx *= (float)div;
    v->param->sy *= (float)div;
    return true;
}

//------------------------------------------------------------------------------
// Probe
//------------------------------------------------------------------------------
static std::unordered_map<std::wstring, int> g_probe_count;
static void Probe(FILTER_PROC_VIDEO* v, int mode, int div, int state,
                  int W, int H) {
    if (!item_probe.value) return;
    wchar_t k[128];
    _snwprintf_s(k, _countof(k), _TRUNCATE, L"%lld_%d_%d_%d",
                 (long long)v->object->effect_id, mode, div, state);
    std::lock_guard<std::mutex> lk(g_mu);
    int& n = g_probe_count[k];
    ++n;
    if (n > 3 && (n % 120) != 0) return;

    OBJECT_IMAGE_PARAM base{};
    bool has_base = false;
    if (v->get_output_image_param)
        has_base = v->get_output_image_param(nullptr, 0.0, &base, sizeof(base));
    int cm = -1;
    if (v->get_camera_mode) cm = (int)v->get_camera_mode();
    bool cam_target = v->object->is_camera_target();
    bool cam_obj = v->get_camera_control_object && v->get_camera_control_object() != nullptr;

    const OBJECT_IMAGE_PARAM& p = *v->param;
    LogF(L"PROBE id=%lld f=%d mode=%d div=%d state=%d | src=%dx%d obj=%dx%d "
         L"camMode=%d camTarget=%d camObj=%d | param xyz=(%.2f,%.2f,%.2f) "
         L"r=(%.2f,%.2f,%.2f) s=(%.3f,%.3f,%.3f) c=(%.2f,%.2f,%.2f) a=%.2f | "
         L"base=%d base_s=(%.3f,%.3f,%.3f) base_c=(%.2f,%.2f,%.2f)",
         (long long)v->object->effect_id, v->object->frame, mode, div, state,
         W, H, v->object->width, v->object->height,
         cm, (int)cam_target, (int)cam_obj,
         p.x, p.y, p.z, p.rx, p.ry, p.rz, p.sx, p.sy, p.sz,
         p.cx, p.cy, p.cz, p.alpha,
         (int)has_base, base.sx, base.sy, base.sz, base.cx, base.cy, base.cz);
}

//------------------------------------------------------------------------------
// Performance telemetry (aggregated, one line per 60 filter invocations,
// plus a guaranteed final flush when the plugin unloads).
//------------------------------------------------------------------------------
struct PerfAgg {
    std::mutex mu;
    int    calls = 0, fast = 0, slow = 0;
    double sum_total = 0, sum_decode = 0, max_decode = 0;
    int    div_hist[7] = {};          // index = min(6, log2(div))
    ULONGLONG window_start = 0;
};
static PerfAgg g_perf;
// Why did a call end up on the L0 (full-resolution) path? Surfaced in the
// perf log so a slow project can be diagnosed without attaching a debugger.
static std::atomic<int> g_stat_gated{ 0 };      // export/save gate or unknown state
static std::atomic<int> g_stat_keep{ 0 };       // このレイヤーは元画像を維持
static std::atomic<int> g_stat_div1{ 0 };       // AUTO decided no downscale needed
static std::atomic<int> g_stat_cap{ 0 };        // preview budget raised div (v3 fix hit)
static std::atomic<int> g_stat_subscene{ 0 };   // rendered inside a composited sub-scene
static std::atomic<int> g_stat_scene_big{ 0 };  // scene canvas larger than preview budget
static std::atomic<int> g_stat_scene_maxw{ 0 }; // widest scene canvas seen in window
static std::atomic<int> g_stat_div1_maxmp{ 0 };// largest source (Mpx) among div=1 frames
static void NoteMaxW(int w) {
    int cur = g_stat_scene_maxw.load();
    while (w > cur && !g_stat_scene_maxw.compare_exchange_weak(cur, w)) {}
}
static void NoteDiv1Mpx(int mp) {
    int cur = g_stat_div1_maxmp.load();
    while (mp > cur && !g_stat_div1_maxmp.compare_exchange_weak(cur, mp)) {}
}

// Caller must hold g_perf.mu. Writes one summary line and resets the window.
static void PerfFlushLocked(const char* tag) {
    if (g_perf.calls <= 0) return;
    const ULONGLONG now = GetTickCount64();
    const double win = (double)(now - g_perf.window_start) / 1000.0;
    char line[700];
    SYSTEMTIME st; GetLocalTime(&st);
    _snprintf_s(line, _countof(line), _TRUNCATE,
        "[%02u/%02u %02u:%02u:%02u] %s calls=%d win=%.2fs fast=%d slow=%d "
        "filter_avg=%.3fms filter_sum=%.1fms decode_avg=%.3fms decode_max=%.1fms "
        "div[1]=%d [2]=%d [4]=%d [8]=%d [16]=%d [32]=%d [64]=%d "
        "L0:gate=%d keep=%d auto1=%d cap=%d sub=%d big=%d maxw=%d d1mp=%d\n",
        st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, tag,
        g_perf.calls, win, g_perf.fast, g_perf.slow,
        g_perf.sum_total / g_perf.calls, g_perf.sum_total,
        g_perf.slow ? g_perf.sum_decode / g_perf.slow : 0.0,
        g_perf.max_decode,
        g_perf.div_hist[0], g_perf.div_hist[1], g_perf.div_hist[2],
        g_perf.div_hist[3], g_perf.div_hist[4], g_perf.div_hist[5],
        g_perf.div_hist[6],
        g_stat_gated.exchange(0), g_stat_keep.exchange(0), g_stat_div1.exchange(0),
        g_stat_cap.exchange(0), g_stat_subscene.exchange(0),
        g_stat_scene_big.exchange(0), g_stat_scene_maxw.exchange(0),
        g_stat_div1_maxmp.exchange(0));
    PerfLogLine(line);
    g_perf.calls = g_perf.fast = g_perf.slow = 0;
    g_perf.sum_total = g_perf.sum_decode = g_perf.max_decode = 0;
    std::memset(g_perf.div_hist, 0, sizeof(g_perf.div_hist));
    g_perf.window_start = now;
}
static void PerfFlush(const char* tag) {
    std::lock_guard<std::mutex> lk(g_perf.mu);
    PerfFlushLocked(tag);
}

struct PerfRecorder {
    LARGE_INTEGER t0;
    double decode_ms = 0;
    bool   fast = false;
    int    div = 0, W = 0, H = 0;
    PerfRecorder() { QueryPerformanceCounter(&t0); }
    ~PerfRecorder() {
        LARGE_INTEGER e, f;
        QueryPerformanceCounter(&e);
        QueryPerformanceFrequency(&f);
        if (f.QuadPart <= 0 || div <= 0) return;  // setup-only early return
        const double total =
            (double)(e.QuadPart - t0.QuadPart) * 1000.0 / (double)f.QuadPart;
        std::lock_guard<std::mutex> lk(g_perf.mu);
        if (g_perf.window_start == 0) g_perf.window_start = GetTickCount64();
        ++g_perf.calls;
        g_perf.sum_total += total;
        if (fast) ++g_perf.fast; else ++g_perf.slow;
        if (decode_ms > 0) {
            g_perf.sum_decode += decode_ms;
            if (decode_ms > g_perf.max_decode) g_perf.max_decode = decode_ms;
        }
        int lg = 0;
        for (int d = div; d > 1 && lg < 6; d >>= 1) ++lg;
        g_perf.div_hist[lg]++;
        if (g_perf.calls >= 60) PerfFlushLocked("W");
    }
};

//------------------------------------------------------------------------------
// Main filter proc
//------------------------------------------------------------------------------
bool func_proc_video(FILTER_PROC_VIDEO* v) {
    if (!v || !v->param || !v->object) return false;
    PerfRecorder perf;

    int state = -1;
    if (g_edit && g_edit->get_edit_state) {
        state = g_edit->get_edit_state();
    } else if (g_event_state.load() >= 0) {
        state = g_event_state.load();
    }
    const bool saving = (state == EDIT_HANDLE::EDIT_STATE_SAVE) || item_sim_save.value;

    constexpr int mode = 3;   // fixed production path, see Effect settings
    const int level = std::min(5, std::max(0, (int)item_level.value));
    int div = (level == 0) ? 0 : (1 << level);   // 0 = AUTO unresolved
    const bool auto_level = (level == 0);
    bool gated = false;

    // Hard export gate. If we cannot read the edit state at all, stay on L0:
    // a proxy that leaks into an export is worse than no optimisation.
    if ((saving && item_force_l0.value) || state < 0) {
        static std::atomic<int> gate_logged{ 0 };
        if (item_probe.value && gate_logged.fetch_add(1) < 3)
            LogF(L"EXPORT GATE fired state=%d sim=%d mode %d div=%d -> L0 (div=1)",
                 state, (int)item_sim_save.value, mode, div);
        div = 1; gated = true;
    }

    const std::wstring file = item_file.value ? item_file.value : L"";
    if (file.empty()) { Log(L"no source file configured"); return false; }
    if (!g_cache || !g_cache->get_image_file_cache) {
        Log(L"InitializeCache was not called; staying on L0");
        return false;
    }
    const int scene_w = (v->scene && v->scene->width  > 0) ? v->scene->width  : 1920;
    const int scene_h = (v->scene && v->scene->height > 0) ? v->scene->height : 1080;
    // SCENE_INFO::origin_id is the scene that STARTED this render. When it
    // differs from id we are inside a sub-scene being composited into the
    // master. Its width/height then describe the sub-scene canvas, NOT what
    // the viewer sees - exactly the case AUTO v2 mis-measured.
    const bool composited = (v->scene && v->scene->origin_id != v->scene->id);
    if (composited) g_stat_subscene.fetch_add(1);
    if ((double)scene_w * (double)scene_h > kOversizedScenePx)
        g_stat_scene_big.fetch_add(1);
    NoteMaxW(scene_w);
    const bool mark = item_mark.value;
    const std::wstring fkey = FileKeyFor(file);

    // ---- Source dimensions remembered across frames -----------------------
    // The host's decoded 40 MPx original is only needed ONCE, when the proxy
    // is built. Asking for it on every frame made PV.aup2 (~100 such sources)
    // thrash the image cache and largely cancelled the proxy speed-up.
    SrcDims dims;
    bool haveDims = false;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        auto it = g_src_dims.find(fkey);
        if (it != g_src_dims.end() && it->second.w > 0 && it->second.h > 0) {
            dims = it->second;
            haveDims = true;
        }
    }
    if (auto_level && !gated && haveDims)
        div = AutoDiv(dims.w, dims.h, scene_w, scene_h, composited);
    const bool wantL0 = gated || item_keep_original.value || div == 1;
    if (gated)                  g_stat_gated.fetch_add(1);
    if (item_keep_original.value) g_stat_keep.fetch_add(1);
    if (!gated && !item_keep_original.value && div == 1) g_stat_div1.fetch_add(1);

    // ---- FAST PATH: proxy already built -> never decode the original ------
    ProxyEntry* px = nullptr;
    if (!wantL0 && haveDims && div >= 2) {
        const std::wstring key = MakeKey(file, div, dims.w, dims.h, mark);
        px = FindProxy(key);
    }

    auto common = [&](const SourceView& src, int W, int H) -> bool {
        perf.W = W; perf.H = H; perf.div = div;
        // Count once per invocation (both fast and slow paths funnel here):
        // did the v3 preview budget raise div above what the raw scene would
        // have produced? This is the direct proof the sub-scene fix is active.
        if (auto_level && !gated && W > 0 && H > 0) {
            bool capHit = false;
            (void)AutoDiv(W, H, scene_w, scene_h, composited, &capHit);
            if (capHit) g_stat_cap.fetch_add(1);
            if (div == 1 && !item_keep_original.value)
                NoteDiv1Mpx((int)(((double)W * (double)H) / 1000000.0 + 0.5));
        }
        {
            static std::unordered_map<std::wstring, int> active_seen;
            wchar_t ak[256];
            _snwprintf_s(ak, _countof(ak), _TRUNCATE, L"%lld_%d_%d_%d",
                         (long long)v->object->effect_id, mode, div, state);
            std::lock_guard<std::mutex> lk(g_mu);
            if (active_seen.emplace(ak, 1).second) {
                LogF(L"ProxyLayer ACTIVE id=%lld mode=%d level=%s div=%d state=%d | "
                     L"%dx%d (%.1f MPx) -> %dx%d | scene=%dx%d need=%.2f | %s | file=%s",
                     (long long)v->object->effect_id, mode,
                     auto_level ? L"AUTO" : L"MANUAL", div, state,
                     W, H, (double)W * (double)H / 1000000.0,
                     (W + div - 1) / div, (H + div - 1) / div,
                     scene_w, scene_h, AutoNeed(W, H, scene_w, scene_h, composited),
                     src.data ? L"decoded" : L"proxy-cache", file.c_str());
            }
        }
        Probe(v, mode, div, state, W, H);
        if (item_keep_original.value)
            return LazyL0(v, src, file, L"layer opted out, original size preserved");
        if (div == 1)
            return LazyL0(v, src, file, nullptr);
        if (!px || px->pm.empty())
            return LazyL0(v, src, file, L"proxy build failed; falling back to L0");
        if (div > 1) RegisterSizeMap(px->pw, px->ph, W, H);
        return DrawProxy(v, *px, div);
    };

    if (px) {
        perf.fast = true;
        SourceView empty{};
        return common(empty, dims.w, dims.h);   // img never requested
    }

    // ---- SLOW PATH: first frame for this (file, div), L0 or export --------
    LARGE_INTEGER t0d, t1d, freq;
    QueryPerformanceCounter(&t0d);
    CACHE_FILE_IMAGE img = g_cache->get_image_file_cache(file.c_str());
    QueryPerformanceCounter(&t1d);
    QueryPerformanceFrequency(&freq);
    if (freq.QuadPart > 0)
        perf.decode_ms = (double)(t1d.QuadPart - t0d.QuadPart) * 1000.0
                         / (double)freq.QuadPart;
    if (!img || !img.buffer || img.width <= 0 || img.height <= 0) {
        LogF(L"get_image_file_cache failed for %s", file.c_str());
        return false;
    }
    SourceView src;
    src.data  = (const uint8_t*)img.buffer;
    src.w     = img.width;
    src.h     = img.height;
    src.pitch = img.pitch > 0 ? img.pitch : img.width * 4;
    src.fmt   = img.format;
    const int W = src.w, H = src.h;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        g_src_dims[fkey] = SrcDims{ W, H };
    }
    if (auto_level && !gated) div = AutoDiv(W, H, scene_w, scene_h, composited);
    if (div < 1) div = 1;

    // YUY2 / YC48 are not 4-byte-per-pixel layouts; reading them through the
    // RGBA path would overrun a row. Refuse rather than risk a bad access.
    if (src.fmt == INPUT_PIXEL_FORMAT::YUY2 ||
        src.fmt == INPUT_PIXEL_FORMAT::YC48 ||
        src.pitch < W * 4) {
        LogF(L"unsupported pixel format=%d pitch=%d; ProxyLayer disabled for this file",
             (int)src.fmt, src.pitch);
        return false;
    }
    if (!wantL0 && div >= 2)
        px = GetProxy(file, div, src, mark);
    return common(src, W, H);        // img stays alive through the whole call
}
