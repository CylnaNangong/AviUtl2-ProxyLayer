// ProxyLuaJIT.cpp -- OPTIONAL same-name proxy for the host's luaJIT.dll.
//
// 322 of the 323 exports are PE forwarders to luaJIT_real.dll (see
// host-test/gen_luaproxy_def.py); only lua_setfield is implemented here.
//
// Why: AviUtl2 fills the Lua `obj` table with the CURRENT TEXTURE size before
// every script effect. When ProxyLayer downscales a layer, obj.w/obj.h become
// 1/div and size-dependent scripts (OpenGlow, volumetric light, ...) shrink.
// Writing back OBJECT_INFO or patching obj.w from Lua both fail (report 10.9):
// the host re-fills the table through the Lua C API on every effect call. The
// only transparent interception point left is that very API call.
//
// v0.3.5 -- PAIR MATCHING (the thorough fix).
//   The old hook matched ONE dimension at a time against a value-keyed table.
//   That is fundamentally ambiguous: it is a GLOBAL hook, so it also rewrites
//   obj.w/obj.h of objects ProxyLayer NEVER converted. Real case: a
//   5140x1712 layer at div=2 produced proxyH 856, while an unconverted
//   628x856 hair layer wrote obj.h=856; the hook returned 1712 and the
//   SoftAnchorBone mesh (y0 = -h/2 + ...) moved the hair upward.
//   The host writes obj.w and obj.h as an adjacent pair. We therefore buffer
//   the raw w and, when h arrives, ask ProxyLayer.aux2 for a match on the
//   COMPLETE (w,h) pair. Both dimensions must match, so an unrelated object
//   can never be rewritten. If w and h DO match, we also correct the already
//   committed w field in the table before replacing the h value.
//
// Backward compatible: if the loaded ProxyLayer.aux2 is older and does not
// export ProxyLayer_MapSizePair, the hook falls back to the legacy
// single-dimension ProxyLayer_MapSize behaviour.
//
// Nothing in the default package depends on this DLL until it is installed.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <wchar.h>

typedef void   (__cdecl *PFN_lua_setfield)(void* L, int idx, const char* k);
typedef double (__cdecl *PFN_lua_tonumber)(void* L, int idx);
typedef void   (__cdecl *PFN_lua_pushnumber)(void* L, double n);
typedef void   (__cdecl *PFN_lua_replace)(void* L, int idx);
typedef int    (__cdecl *PFN_lua_gettop)(void* L);
// ProxyLayer.aux2: int ProxyLayer_MapSize(double cur, int isW, double* out)
typedef int    (__cdecl *PFN_MapSize)(double cur, int isW, double* out);
// ProxyLayer.aux2: int ProxyLayer_MapSizePair(double w,double h,double* ow,double* oh)
typedef int    (__cdecl *PFN_MapSizePair)(double w, double h,
                                          double* outW, double* outH);

static HMODULE            g_real         = nullptr;
static HMODULE            g_aux          = nullptr;
static PFN_lua_setfield   g_setfield     = nullptr;
static PFN_lua_tonumber   g_tonumber     = nullptr;
static PFN_lua_pushnumber g_pushnumber   = nullptr;
static PFN_lua_replace    g_replace      = nullptr;
static PFN_lua_gettop     g_gettop       = nullptr;
static PFN_MapSize        g_mapsize      = nullptr;
static PFN_MapSizePair    g_mapsizepair  = nullptr;
static wchar_t            g_logPath[MAX_PATH] = L"";
static volatile LONG      g_whCount      = 0;
static volatile LONG      g_logCount     = 0;
static CRITICAL_SECTION   g_logCs;
static INIT_ONCE          g_once         = INIT_ONCE_STATIC_INIT;

// Per-thread pending w. Explicit TlsAlloc is used instead of
// __declspec(thread): a DLL that the host loads dynamically cannot rely on
// implicit TLS being initialised (that has caused real host crashes before).
// A Lua state belongs to one render thread, so w and h of the same object
// always arrive on the same thread.
struct PendingW {
    void*  L;
    double w;
    DWORD  tick;
    int    valid;
};
static DWORD g_tlsPending = TLS_OUT_OF_INDEXES;
static PendingW* GetPending() {
    if (g_tlsPending == TLS_OUT_OF_INDEXES) return nullptr;
    PendingW* p = reinterpret_cast<PendingW*>(TlsGetValue(g_tlsPending));
    if (!p) {
        p = reinterpret_cast<PendingW*>(LocalAlloc(LPTR, sizeof(PendingW)));
        if (!p) return nullptr;
        TlsSetValue(g_tlsPending, p);
    }
    return p;
}

// LUA_REGISTRYINDEX / LUA_GLOBALSINDEX are pseudo-indices, not stack slots.
#define PL_PSEUDO_INDEX (-10000)

static BOOL DirExistsW(LPCWSTR p) {
    const DWORD a = GetFileAttributesW(p);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

static BOOL CALLBACK InitOnce(PINIT_ONCE, PVOID, PVOID*) {
    InitializeCriticalSection(&g_logCs);
    g_tlsPending = TlsAlloc();

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
    if (InterlockedIncrement(&g_logCount) > 120) return;  // hard cap
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
    if (g_setfield && g_tonumber && g_pushnumber && g_replace && g_gettop) return;
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
    g_gettop     = reinterpret_cast<PFN_lua_gettop>(
        GetProcAddress(g_real, "lua_gettop"));
}

static void EnsureAux() {
    if (!g_aux) g_aux = GetModuleHandleW(L"ProxyLayer.aux2");
    if (!g_aux) return;                 // plugin not loaded yet: retry next call
    if (!g_mapsizepair)
        g_mapsizepair = reinterpret_cast<PFN_MapSizePair>(
            GetProcAddress(g_aux, "ProxyLayer_MapSizePair"));
    if (!g_mapsize)
        g_mapsize = reinterpret_cast<PFN_MapSize>(
            GetProcAddress(g_aux, "ProxyLayer_MapSize"));
}

// Exported through the generated .def (see host-test/gen_luaproxy_def.py).
extern "C" void __cdecl lua_setfield(void* L, int idx, const char* k) {
    InitOnceExecuteOnce(&g_once, &InitOnce, nullptr, nullptr);
    EnsureReal();
    EnsureAux();
    if (!g_setfield) {                 // never silently drop a Lua API call
        LogLine("FATAL: real lua_setfield unresolved");
        return;
    }

    const int isW = (k && k[0] == 'w' && k[1] == '\0') ? 1 :
                    (k && k[0] == 'h' && k[1] == '\0') ? -1 : 0;
    PendingW* pending = isW ? GetPending() : nullptr;
    if (isW && !pending) { g_setfield(L, idx, k); return; }
    const int top = g_gettop ? g_gettop(L) : 0;
    // Resolve the table to an ABSOLUTE index while the stack still has its
    // original height; we push extra values below and a relative index would
    // then point at the wrong slot.
    int absIdx = idx;
    if (idx < 0 && idx > PL_PSEUDO_INDEX) absIdx = top + idx + 1;
    const bool tableRewritable = (absIdx > 0 && absIdx <= top && idx > PL_PSEUDO_INDEX);

    if (isW == 1) {                       // ---- obj.w ----------------------
        const LONG n = InterlockedIncrement(&g_whCount);
        const double cur = g_tonumber ? g_tonumber(L, -1) : 0.0;
        if (g_mapsizepair) {
            // Defer the decision until obj.h arrives so both dimensions can
            // be matched together. Leaving w untouched is always safe for
            // objects that are not proxied.
            pending->L = L; pending->w = cur;
            pending->tick = GetTickCount(); pending->valid = 1;
            if (n <= 24) {
                char buf[300];
                sprintf_s(buf, sizeof(buf),
                          "setfield w cur=%.0f deferred (call #%d, pair=1)",
                          cur, (int)n);
                LogLine(buf);
            }
        } else if (g_mapsize) {           // legacy plugin: single dimension
            double mapped = 0.0;
            bool replaced = false;
            if (g_mapsize(cur, 1, &mapped) && mapped != cur &&
                g_pushnumber && g_replace) {
                g_pushnumber(L, mapped);
                g_replace(L, -2);
                replaced = true;
            }
            if (n <= 24) {
                char buf[300];
                sprintf_s(buf, sizeof(buf),
                          "setfield w cur=%.0f %s mapped=%.0f (call #%d, legacy=1)",
                          cur, replaced ? "->" : "unchanged", mapped, (int)n);
                LogLine(buf);
            }
        }
    } else if (isW == -1) {               // ---- obj.h ---------------------
        const LONG n = InterlockedIncrement(&g_whCount);
        const double cur = g_tonumber ? g_tonumber(L, -1) : 0.0;
        bool replaced = false;
        double mappedW = 0.0, mappedH = 0.0;
        const double pendingW = pending ? pending->w : 0.0;
        if (g_mapsizepair) {
            const bool fresh = pending && pending->valid && pending->L == L &&
                               (GetTickCount() - pending->tick) < 2000;
            if (fresh && tableRewritable &&
                g_mapsizepair(pendingW, cur, &mappedW, &mappedH)) {
                // Correct the w field that the host already committed, then
                // replace the h value currently sitting on the stack.
                // g_setfield is the REAL Lua API, so this does not re-enter
                // our own hook.
                g_pushnumber(L, mappedW);
                g_setfield(L, absIdx, "w");
                g_pushnumber(L, mappedH);
                g_replace(L, -2);
                replaced = true;
            }
            if (pending) pending->valid = 0;
            if (n <= 24) {
                char buf[300];
                sprintf_s(buf, sizeof(buf),
                          "setfield h cur=%.0f pair(%.0f,%.0f) %s -> (%.0f,%.0f) "
                          "(call #%d, fresh=%d, tbl=%d)",
                          cur, pendingW, cur,
                          replaced ? "MATCH" : "no-match",
                          replaced ? mappedW : cur, replaced ? mappedH : cur,
                          (int)n, fresh ? 1 : 0, tableRewritable ? 1 : 0);
                LogLine(buf);
            }
        } else if (g_mapsize) {           // legacy plugin: single dimension
            double mapped = 0.0;
            if (g_mapsize(cur, 0, &mapped) && mapped != cur &&
                g_pushnumber && g_replace) {
                g_pushnumber(L, mapped);
                g_replace(L, -2);
                replaced = true;
                mappedH = mapped;
            }
            if (n <= 24) {
                char buf[300];
                sprintf_s(buf, sizeof(buf),
                          "setfield h cur=%.0f %s mapped=%.0f (call #%d, legacy=1)",
                          cur, replaced ? "->" : "unchanged", mapped, (int)n);
                LogLine(buf);
            }
        }
    }
    g_setfield(L, idx, k);
}

BOOL WINAPI DllMain(HINSTANCE self, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH && self) DisableThreadLibraryCalls(self);
    return TRUE;
}
