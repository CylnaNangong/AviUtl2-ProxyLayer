//==============================================================================
//  ExportProbe.aux2 — TEST HARNESS ONLY. Never ship this.
//
//  Purpose: drive a REAL AviUtl2 file output without any UI / shortcut / dialog,
//  so ProxyLayer's EDIT_STATE_SAVE gate can be validated end-to-end.
//
//  The official SDK exposes EDIT_HANDLE::output_file(), which starts a file
//  output programmatically. We call it from a delayed worker thread (the host
//  must be idle and the project fully loaded; calling it from a render callback
//  or a project-load callback would be inside an edit/reference lock).
//
//  Built-in "PNGファイル出力" is used: no external encoder, no QSVEnc, no
//  save dialog. What we care about is that the host renders output frames and
//  get_edit_state() returns EDIT_STATE_SAVE while ProxyLayer is executing.
//==============================================================================

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <atomic>
#include <cstdarg>
#include <string>
#include <thread>

#include "plugin2.h"
#include "logger2.h"
#include "cache2.h"

static LOG_HANDLE*      g_logger = nullptr;
static EDIT_HANDLE*     g_edit   = nullptr;
static std::thread      g_worker;
static std::atomic<bool> g_stop{ false };

static void Log(const std::wstring& s) {
    if (g_logger && g_logger->info) g_logger->info(g_logger, s.c_str());
    else OutputDebugStringW((L"[ExportProbe] " + s + L"\n").c_str());
}
static void LogF(const wchar_t* fmt, ...) {
    wchar_t buf[1024];
    va_list ap; va_start(ap, fmt);
    _vsnwprintf_s(buf, _countof(buf), _TRUNCATE, fmt, ap);
    va_end(ap);
    Log(buf);
}

// Build "<runRoot>/out/proxy-export-probe.png" from the host exe path:
// the isolated host lives at <runRoot>/aviutl2/aviutl2.exe.
static std::wstring OutputPath(LPCWSTR ext) {
    wchar_t exe[MAX_PATH] = {};
    if (!GetModuleFileNameW(nullptr, exe, MAX_PATH)) return L"";
    std::wstring e(exe);
    const size_t s1 = e.find_last_of(L"\\/");
    if (s1 == std::wstring::npos) return L"";
    const std::wstring hostRoot = e.substr(0, s1);          // <runRoot>/aviutl2
    const size_t s2 = hostRoot.find_last_of(L"\\/");
    if (s2 == std::wstring::npos) return L"";
    const std::wstring runRoot = hostRoot.substr(0, s2);     // <runRoot>
    CreateDirectoryW((runRoot + L"\\out").c_str(), nullptr);  // may already exist
    return runRoot + L"\\out\\proxy-export-probe" + ext;
}

// SEH must live in a function with no C++ objects requiring unwinding
// (MSVC C2712 under /EHsc). Keep it a plain pointer-only wrapper.
static bool __declspec(noinline) CallOutput(EDIT_HANDLE* e, LPCWSTR p, LPCWSTR n) {
    __try {
        return e->output_file(p, n, nullptr, nullptr);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static void Worker() {
    // Poll in small slices so UninitializePlugin() can join promptly.
    for (int i = 0; i < 120 && !g_stop.load(); ++i) Sleep(100);   // ~12 s
    if (g_stop.load() || !g_edit || !g_edit->output_file) return;

    // Only the built-in AVI output can be started headlessly via output_file():
    // QSVEnc / PNG / JPEG all return false (QSVEnc needs its config/save dialog
    // first, which is exactly what the F9 UI route was fumbling). To keep the
    // uncompressed AVI tiny, the SDK test profiles set output.width/height to
    // 320x180 and select.start/end to 0..2 -> well under 1 MB instead of 363 MB.
    struct Candidate { LPCWSTR plugin; LPCWSTR ext; };
    static const Candidate kCandidates[] = {
        { L"AVIファイル出力", L".avi" },
    };
    for (const auto& c : kCandidates) {
        if (g_stop.load()) return;
        const std::wstring path = OutputPath(c.ext);
        const bool ok = CallOutput(g_edit, path.c_str(), c.plugin);
        LogF(L"ExportProbe: output_file(%s, %s) -> %d",
             c.plugin, path.c_str(), (int)ok);
        if (ok) break;
        // A failed call may be "already outputting"; give the host a moment.
        for (int i = 0; i < 10 && !g_stop.load(); ++i) Sleep(100);
    }
    Log(L"ExportProbe: worker done");
}

extern "C" __declspec(dllexport) bool InitializePlugin(DWORD version) {
    LogF(L"ExportProbe InitializePlugin host=%u", version);
    return true;
}
extern "C" __declspec(dllexport) void UninitializePlugin() {
    g_stop.store(true);
    if (g_worker.joinable()) g_worker.join();
}
extern "C" __declspec(dllexport) void InitializeLogger(LOG_HANDLE* logger) {
    g_logger = logger;
}
extern "C" __declspec(dllexport) void InitializeCache(CACHE_HANDLE*) {}
extern "C" __declspec(dllexport) void RegisterPlugin(HOST_APP_TABLE* host) {
    if (!host) return;
    if (host->create_edit_handle) g_edit = host->create_edit_handle();
    LogF(L"ExportProbe RegisterPlugin editHandle=%p output_file=%p",
         (void*)g_edit,
         (void*)(g_edit ? (void*)g_edit->output_file : nullptr));
    if (g_edit && g_edit->output_file)
        g_worker = std::thread(&Worker);
    else
        Log(L"ExportProbe: EDIT_HANDLE::output_file unavailable; aborting");
}
