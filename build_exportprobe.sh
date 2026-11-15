#!/usr/bin/env bash
# Build ExportProbe.aux2 (TEST HARNESS ONLY) with VS2022 BuildTools.
set -euo pipefail
export MSYS2_ARG_CONV_EXCL='*'

HERE_WIN="$(cygpath -w "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)")"
MSVC="C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Tools\\MSVC\\14.39.33519"
SDK="C:\\Program Files (x86)\\Windows Kits\\10"
SDKV="10.0.22621.0"
CL="C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/14.39.33519/bin/Hostx64/x64/cl.exe"
DUMPBIN="C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/14.39.33519/bin/Hostx64/x64/dumpbin.exe"

mkdir -p "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/build" \
         "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/dist"

INC=(
  "/I${MSVC}\\include"
  "/I${SDK}\\Include\\${SDKV}\\ucrt"
  "/I${SDK}\\Include\\${SDKV}\\um"
  "/I${SDK}\\Include\\${SDKV}\\shared"
  "/I${SDK}\\Include\\${SDKV}\\winrt"
  "/I${HERE_WIN}\\include"
)
LIBS=(
  "/LIBPATH:${MSVC}\\lib\\x64"
  "/LIBPATH:${SDK}\\Lib\\${SDKV}\\ucrt\\x64"
  "/LIBPATH:${SDK}\\Lib\\${SDKV}\\um\\x64"
)

echo "[1/2] Compiling ExportProbe.aux2 ..."
"$CL" /nologo /LD /O2 /std:c++17 /EHsc /W3 /utf-8 /MD /D_CRT_SECURE_NO_WARNINGS \
  "${INC[@]}" \
  "/Fo${HERE_WIN}\\build\\ExportProbe_" "/Fd${HERE_WIN}\\build\\ExportProbe.pdb" \
  "${HERE_WIN}\\src\\ExportProbe.cpp" \
  /link /DLL "${LIBS[@]}" kernel32.lib user32.lib \
  "/OUT:${HERE_WIN}\\dist\\ExportProbe.aux2"

echo "[2/2] Exports:"
"$DUMPBIN" /EXPORTS "${HERE_WIN}\\dist\\ExportProbe.aux2" 2>/dev/null \
  | grep -E "Initialize|RegisterPlugin|Uninitialize" || true
ls -lh "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/dist/ExportProbe.aux2"
