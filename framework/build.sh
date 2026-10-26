#!/usr/bin/env bash
# Build SLModFramework (32-bit) with winlibs i686 mingw-w64.
# Phase 4: overlay + plugin host (Lua 5.4.7, sandboxed).
# P2 legacy read-only DLL source (native/slmodf.c) is kept for reference and is NOT compiled.
set -euo pipefail
cd "$(dirname "$0")"
GCC=/d/devtools/winlibs-i686/mingw32/bin/gcc.exe
GXX=/d/devtools/winlibs-i686/mingw32/bin/g++.exe
[ -x "$GCC" ] || { echo "missing compiler: $GCC" >&2; exit 1; }
[ -x "$GXX" ] || { echo "missing compiler: $GXX" >&2; exit 1; }
IMGUI=../third_party/imgui
MINHOOK=../third_party/minhook
LUA=../third_party/lua-5.4.7
OUT=build
mkdir -p "$OUT" bin

CFLAGS="-m32 -O2 -DWIN32_LEAN_AND_MEAN -D_WIN32_WINNT=0x0601 -DIMGUI_IMPL_WIN32_DISABLE_GAMEPAD"
INCS="-Inative -I$IMGUI -I$IMGUI/backends -I$MINHOOK/include -I$LUA/src"
CXXFLAGS="$CFLAGS $INCS -std=c++17 -fno-rtti -Wall -Wno-unused-parameter -Wno-missing-field-initializers"

echo "== compiling vendored ImGui =="
for f in imgui imgui_draw imgui_tables imgui_widgets; do
  "$GXX" $CXXFLAGS -c "$IMGUI/$f.cpp" -o "$OUT/imgui_$f.o"
done
"$GXX" $CXXFLAGS -c "$IMGUI/backends/imgui_impl_win32.cpp" -o "$OUT/imgui_impl_win32.o"
"$GXX" $CXXFLAGS -c "$IMGUI/backends/imgui_impl_dx9.cpp" -o "$OUT/imgui_impl_dx9.o"

echo "== compiling vendored MinHook =="
for f in hook buffer trampoline; do
  "$GCC" $CFLAGS -I"$MINHOOK/include" -I"$MINHOOK/src" -c "$MINHOOK/src/$f.c" -o "$OUT/mh_$f.o"
done
"$GCC" $CFLAGS -I"$MINHOOK/include" -I"$MINHOOK/src" -c "$MINHOOK/src/hde/hde32.c" -o "$OUT/mh_hde32.o"

echo "== compiling vendored Lua 5.4.7 (sandbox core, no lua.c/luac.c) =="
for f in lapi lauxlib lbaselib lcode lcorolib lctype ldblib ldebug ldo ldump lfunc lgc \
         linit liolib llex lmathlib lmem loadlib lobject lopcodes loslib lparser lstate \
         lstring lstrlib ltable ltablib ltm lundump lutf8lib lvm lzio; do
  "$GCC" $CFLAGS -I"$LUA/src" -c "$LUA/src/$f.c" -o "$OUT/lua_$f.o"
done

echo "== compiling framework native =="
for f in util scene state recorder audio_hook overlay d3d9hook plugin_host slmodf; do
  "$GXX" $CXXFLAGS -c "native/$f.cpp" -o "$OUT/native_$f.o"
done

echo "== linking SLModFramework32.dll =="
"$GXX" -m32 -shared -s -o bin/SLModFramework32.dll \
  "$OUT"/native_*.o "$OUT"/imgui_*.o "$OUT"/mh_*.o "$OUT"/lua_*.o \
  -luser32 -lgdi32 -ld3d9 -ldsound -ldxguid -lpsapi -ladvapi32 -ldwmapi -lwinmm \
  -static -static-libgcc -static-libstdc++ -Wl,--enable-stdcall-fixup \
  -Wl,--no-insert-timestamp

echo "== building launcher32.exe =="
"$GCC" -m32 -O2 -Wall -Wextra -s -o bin/launcher32.exe launcher/main.c launcher/sha256.c \
  -luser32 -static-libgcc -Wl,--no-insert-timestamp

echo "== outputs =="
ls -l bin/
file bin/*.dll bin/*.exe 2>/dev/null || true
