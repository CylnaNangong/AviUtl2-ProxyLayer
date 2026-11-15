#!/usr/bin/env bash
# Build ProxyLayer.aux2 with the locally installed VS2022 BuildTools (no vcvars).
# Git Bash / MSYS would otherwise rewrite every /I... and /link argument into a
# POSIX path, so argument conversion is disabled and all paths are Windows-style.
set -euo pipefail
export MSYS2_ARG_CONV_EXCL='*'

HERE_WIN="$(cygpath -w "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)")"

# Override these if your toolchain versions differ, e.g.
#   VS_BASE="D:\VS2022" MSVC_VER=14.42.34433 SDKV=10.0.26100.0 bash build_msvc.sh
VS_BASE="${VS_BASE:-C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools}"
MSVC_VER="${MSVC_VER:-14.39.33519}"
SDK="${SDK:-C:\\Program Files (x86)\\Windows Kits\\10}"
SDKV="${SDKV:-10.0.22621.0}"

MSVC="${VS_BASE}\\VC\\Tools\\MSVC\\${MSVC_VER}"
CL="${VS_BASE}/VC/Tools/MSVC/${MSVC_VER}/bin/Hostx64/x64/cl.exe"
DUMPBIN="${VS_BASE}/VC/Tools/MSVC/${MSVC_VER}/bin/Hostx64/x64/dumpbin.exe"

mkdir -p "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/build" \
         "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/dist"

INC=(
  "/I${MSVC}\\include"
  "/I${SDK}\\Include\\${SDKV}\\ucrt"
  "/I${SDK}\\Include\\${SDKV}\\um"
  "/I${SDK}\\Include\\${SDKV}\\shared"
  "/I${SDK}\\Include\\${SDKV}\\winrt"
  "/I${HERE_WIN}\\include"
  "/I${HERE_WIN}\\src"
)
LIBS=(
  "/LIBPATH:${MSVC}\\lib\\x64"
  "/LIBPATH:${SDK}\\Lib\\${SDKV}\\ucrt\\x64"
  "/LIBPATH:${SDK}\\Lib\\${SDKV}\\um\\x64"
)

echo "[1/2] Compiling ProxyLayer.aux2 ..."
"$CL" /nologo /LD /O2 /std:c++17 /EHsc /W3 /utf-8 /MD /D_CRT_SECURE_NO_WARNINGS \
  "${INC[@]}" \
  "/Fo${HERE_WIN}\\build\\" "/Fd${HERE_WIN}\\build\\" \
  "${HERE_WIN}\\src\\ProxyLayer.cpp" \
  /link /DLL "${LIBS[@]}" kernel32.lib user32.lib gdi32.lib \
  "/OUT:${HERE_WIN}\\dist\\ProxyLayer.aux2"

echo "[2/2] Exports:"
"$DUMPBIN" /EXPORTS "${HERE_WIN}\\dist\\ProxyLayer.aux2" 2>/dev/null \
  | grep -E "InitializeCache|InitializeLogger|InitializePlugin|GetFilterPluginTable|RegisterPlugin|UninitializePlugin" || true
ls -lh "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/dist/ProxyLayer.aux2"
