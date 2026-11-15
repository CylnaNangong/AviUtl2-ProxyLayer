// ProxyLuaJIT.cpp -- OPTIONAL experimental component, NOT in the shipping .au2pkg.
//
// A same-name proxy for the host's luaJIT.dll. 322 of the 323 exports are PE
// forwarders to luaJIT_real.dll (see host-test/gen_luaproxy_def.py); only
// lua_setfield is implemented here.
//
// Why: AviUtl2 fills the Lua `obj` table with the CURRENT TEXTURE size before
// every script effect.  When ProxyLayer downscales a layer, obj.w/obj.h become
// 1/div and size-dependent scripts (volumetric light, etc.) shrink.  Writing
// back OBJECT_INFO or patching obj.w from Lua both fail (see report 10.9): the
// host re-fills the table through the Lua C API on every effect call.  The only
// transparent interception point left is that very API call.
//
// The hook consults ProxyLayer.aux2's exported ProxyLayer_MapSize() to map a
// proxy dimension back to the source dimension, and replaces the value on the
// Lua stack before forwarding to the real lua_setfield.
//
// Nothing in the default package depends on this DLL.  It is deployed only into
// isolated host-test runtimes via hostAssets.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <wchar.h>

typedef void   (__cdecl *PFN_lua_setfield)(void* L, int idx, const char* k);
typedef double (__cdecl *PFN_lua_tonumber)(void* L, int idx);
typedef void   (__cdecl *PFN_lua_pushnumber)(void* L, double n);
typedef void   (__cdecl *PFN_lua_replace)(void* L, int idx);
// ProxyLayer.aux2: int ProxyLayer_MapSize(double cur, int isW, double* out)
typedef int    (__cdecl *PFN_MapSize)(double cur, int isW, double* out);

static HMODULE          g_real        = nullptr;
static HMODULE          g_aux         = nullptr;
static PFN_lua_setfield g_setfield    = nullptr;
static PFN_lua_tonumber g_tonumber    = nullptr;
static PFN_lua_pushnumber g_pushnumber = nullptr;
static PFN_lua_replace  g_replace     = nullptr;
static PFN_MapSize      g_mapsize     = nullptr;
static wchar_t          g_logPath[MAX_PATH] = L"";
static volatile LONG    g_whCount     = 0;
static volatile LONG    g_logCount    = 0;
static CRITICAL_SECTION g_logCs;
static INIT_ONCE        g_once        = INIT_ONCE_STATIC_INIT;

static BOOL DirExistsW(LPCWSTR p) {
    const DWORD a = GetFileAttributesW(p);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

static BOOL CALLBACK InitOnce(PINIT_ONCE, PVOID, PVOID*) {
    InitializeCriticalSection(&g_logCs);

    // CRITICAL: never CREATE a directory next to the EXE. AviUtl2 switches to
    // a portable data root as soon as <exedir>\data exists, which silently
    // resets the whole environment (language, plugins, layout) to defaults.
    // Only pick a log directory that ALREADY exists; otherwise stay silent.
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&g_once), &self);
    wchar_t mod[MAX_PATH] = L"";
    wchar_t cand[MAX_PATH] = L"";
    if (self && GetModuleFileNameW(self, mod, MAX_PATH)) {
        wchar_t* slash = wcsrchr(mod, L'\\');
        if (slash) *slash = L'\0';
        swprintf_s(cand, MAX_PATH, L"%s\\data\\Log", mod);
        if (DirExistsW(cand))
            swprintf_s(g_logPath, MAX_PATH,
                       L"%s\\proxy-luajit.log", cand);
    }
    if (!g_logPath[0]) {
        wchar_t pd[MAX_PATH] = L"";
        if (GetEnvironmentVariableW(L"ProgramData", pd, MAX_PATH) && pd[0]) {
            swprintf_s(cand, MAX_PATH, L"%s\\aviutl2\\Log", pd);
            if (DirExistsW(cand))
                swprintf_s(g_logPath, MAX_PATH,
                           L"%s\\proxy-luajit.log", cand);
        }
    }
    return TRUE;
}

static void LogLine(const char* text) {
    if (!g_logPath[0]) return;
    if (InterlockedIncrement(&g_logCount) > 60) return;   // hard cap
    EnterCriticalSection(&g_logCs);
    HANDLE h = CreateFileW(g_logPath, FILE_APPEND_DATA,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        SetFilePointer(h, 0, nullptr, FILE_END);
        char line[512];
        int n = sprintf_s(line, sizeof(line),
                          "[INFO] [LuaProxy] %s\r\n", text);
        if (n > 0) {
            DWORD written = 0;
            WriteFile(h, line, (DWORD)n, &written, nullptr);
        }
        CloseHandle(h);
    }
    LeaveCriticalSection(&g_logCs);
}

static void EnsureReal() {
    if (g_setfield && g_tonumber && g_pushnumber && g_replace) return;
    if (!g_real) {
        g_real = GetModuleHandleW(L"luaJIT_real.dll");
        if (!g_real) g_real = LoadLibraryW(L"luaJIT_real.dll");
    }
    if (!g_real) return;
    g_setfield   = reinterpret_cast<PFN_lua_setfield>(
        GetProcAddress(g_real, "lua_setfield"));
    g_tonumber   = reinterpret_cast<PFN_lua_tonumber>(
        GetProcAddress(g_real, "lua_tonumber"));
    g_pushnumber = reinterpret_cast<PFN_lua_pushnumber>(
        GetProcAddress(g_real, "lua_pushnumber"));
    g_replace    = reinterpret_cast<PFN_lua_replace>(
        GetProcAddress(g_real, "lua_replace"));
}

static void EnsureAux() {
    if (g_mapsize) return;
    if (!g_aux) g_aux = GetModuleHandleW(L"ProxyLayer.aux2");
    if (!g_aux) return;                 // plugin not loaded yet: retry next call
    g_mapsize = reinterpret_cast<PFN_MapSize>(
        GetProcAddress(g_aux, "ProxyLayer_MapSize"));
}

// Exported through the generated .def (see host-test/gen_luaproxy_def.py).
extern "C" void __cdecl lua_setfield(void* L, int idx, const char* k) {
    InitOnceExecuteOnce(&g_once, &InitOnce, nullptr, nullptr);
    EnsureReal();
    if (!g_setfield) {                 // never silently drop a Lua API call
        LogLine("FATAL: real lua_setfield unresolved");
        return;
    }

    const int isW = (k && k[0] == 'w' && k[1] == '\0') ? 1 :
                    (k && k[0] == 'h' && k[1] == '\0') ? -1 : 0;
    if (isW) {
        const LONG n = InterlockedIncrement(&g_whCount);
        EnsureAux();
        double cur = g_tonumber ? g_tonumber(L, -1) : 0.0;
        double mapped = 0.0;
        bool replaced = false;
        if (g_mapsize && g_mapsize(cur, isW > 0, &mapped) && mapped != cur &&
            g_pushnumber && g_replace) {
            g_pushnumber(L, mapped);
            g_replace(L, -2);          // pop new top, replace the value below
            replaced = true;
        }
        if (n <= 24) {
            char buf[300];
            sprintf_s(buf, sizeof(buf),
                      "setfield %s cur=%.0f %s mapped=%.0f (call #%d, aux=%d)",
                isW > 0 ? "w" : "h", cur,
                replaced ? "->" : "unchanged", mapped,
                (int)n, g_mapsize ? 1 : 0);
            LogLine(buf);
        }
    }
    g_setfield(L, idx, k);
}

BOOL WINAPI DllMain(HINSTANCE self, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH && self) DisableThreadLibraryCalls(self);
    return TRUE;
}
