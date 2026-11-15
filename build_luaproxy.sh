#!/usr/bin/env bash
# Build the OPTIONAL luaJIT.dll same-name proxy (experimental, not shipped).
# Produces:
#   dist/luaJIT.dll                     -> deploy OVER aviutl2/luaJIT.dll
#   <hut>/aviutl2-host-test/proxy/luaJIT_real.dll -> original, deployed beside it
set -euo pipefail
export MSYS2_ARG_CONV_EXCL='*'

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
HERE_WIN="$(cygpath -w "$HERE")"
ROOT_WIN="$(cygpath -w "$(cd "$HERE/.." && pwd)")"
HUT="$HERE/../Codex的AviUtl2小屋"
SEED="$HUT/.test/VolumetricLight3D-escalated/.astra/runtime/aviutl2/luaJIT.dll"
PROXY_OUT="$HUT/aviutl2-host-test/proxy"

MSVC="C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Tools\\MSVC\\14.39.33519"
SDK="C:\\Program Files (x86)\\Windows Kits\\10"
SDKV="10.0.22621.0"
CL="C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/14.39.33519/bin/Hostx64/x64/cl.exe"
DUMPBIN="C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/14.39.33519/bin/Hostx64/x64/dumpbin.exe"
LIBEXE="C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/14.39.33519/bin/Hostx64/x64/lib.exe"

mkdir -p "$HERE/build" "$HERE/dist" "$PROXY_OUT"

echo "[1/4] Generating PE-forwarder .def from the original luaJIT.dll ..."
GEN_WIN="$(cygpath -w "$HERE/host-test/gen_luaproxy_def.py")"
SEED_WIN="$(cygpath -w "$SEED")"
DEF_WIN="$(cygpath -w "$HERE/dist/luaJIT.def")"
python "$GEN_WIN" "$SEED_WIN" "$DEF_WIN"

INC=(
  "/I${MSVC}\\include"
  "/I${SDK}\\Include\\${SDKV}\\ucrt"
  "/I${SDK}\\Include\\${SDKV}\\um"
  "/I${SDK}\\Include\\${SDKV}\\shared"
)
LIBS=(
  "/LIBPATH:${MSVC}\\lib\\x64"
  "/LIBPATH:${SDK}\\Lib\\${SDKV}\\ucrt\\x64"
  "/LIBPATH:${SDK}\\Lib\\${SDKV}\\um\\x64"
)

echo "[2/4] Compiling proxy DLL ..."
"$LIBEXE" /nologo /MACHINE:X64 \
  "/DEF:${HERE_WIN}\\dist\\luaJIT_real.def" \
  "/OUT:${HERE_WIN}\\build\\luaJIT_real.lib"
"$CL" /nologo /LD /O2 /MD /W3 /utf-8 /D_CRT_SECURE_NO_WARNINGS \
  "${INC[@]}" \
  "/Fo${HERE_WIN}\\build\\" "/Fd${HERE_WIN}\\build\\" \
  "${HERE_WIN}\\src\\ProxyLuaJIT.cpp" \
  /link /DLL /MACHINE:X64 "${LIBS[@]}" kernel32.lib \
  "${HERE_WIN}\\build\\luaJIT_real.lib" \
  "/DEF:${HERE_WIN}\\dist\\luaJIT.def" \
  "/OUT:${HERE_WIN}\\dist\\luaJIT.dll"

echo "[3/4] Verifying exports (322 forwarders + 1 wrapper) ..."
"$DUMPBIN" /EXPORTS "${HERE_WIN}\\dist\\luaJIT.dll" 2>/dev/null \
  | grep -c "forwarded to luaJIT_real" || true
"$DUMPBIN" /EXPORTS "${HERE_WIN}\\dist\\luaJIT.dll" 2>/dev/null \
  | grep -E "lua_setfield|forwarded to luaJIT_real.lua_getfield" | head -4

echo "[4/4] Staging original DLL as luaJIT_real.dll ..."
cp "$SEED" "$PROXY_OUT/luaJIT_real.dll"
cp "$HERE/dist/luaJIT.dll" "$PROXY_OUT/luaJIT.dll"
ls -lh "$PROXY_OUT/luaJIT.dll" "$PROXY_OUT/luaJIT_real.dll"
