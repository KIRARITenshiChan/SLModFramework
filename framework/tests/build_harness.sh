#!/usr/bin/env bash
# Build the offline Direct3D 9 overlay harness (Phase 3), 32-bit.
# Reuses the ImGui/MinHook objects produced by ../build.sh when present.
set -euo pipefail
cd "$(dirname "$0")"
GCC=/d/devtools/winlibs-i686/mingw32/bin/gcc.exe
GXX=/d/devtools/winlibs-i686/mingw32/bin/g++.exe
[ -x "$GCC" ] && [ -x "$GXX" ] || { echo "missing winlibs i686 compiler" >&2; exit 1; }

IMGUI=../../third_party/imgui
MINHOOK=../../third_party/minhook
LUA=../../third_party/lua-5.4.7
FRAMEWORK_BUILD=../build
OUT=obj
mkdir -p "$OUT" ../bin
rm -f "$OUT"/*.o   # avoid stale objects from earlier/renamed compile steps

CFLAGS="-m32 -O2 -DWIN32_LEAN_AND_MEAN -D_WIN32_WINNT=0x0601 -DIMGUI_IMPL_WIN32_DISABLE_GAMEPAD"
INCS="-I../native -I$IMGUI -I$IMGUI/backends -I$MINHOOK/include -I$LUA/src"
CXXFLAGS="$CFLAGS $INCS -std=c++17 -fno-rtti -Wall -Wno-unused-parameter -Wno-missing-field-initializers"

echo "== vendored ImGui/MinHook objects =="
need_rebuild=0
for f in imgui_imgui imgui_imgui_draw imgui_imgui_tables imgui_imgui_widgets imgui_impl_win32 imgui_impl_dx9 mh_hook mh_buffer mh_trampoline mh_hde32; do
  if [ ! -f "$FRAMEWORK_BUILD/$f.o" ]; then need_rebuild=1; fi
done
if [ "$need_rebuild" -eq 1 ]; then
  echo "-- framework objects missing, compiling vendored deps directly --"
  "$GXX" $CXXFLAGS -c "$IMGUI/imgui.cpp" -o "$OUT/imgui_imgui.o"
  "$GXX" $CXXFLAGS -c "$IMGUI/imgui_draw.cpp" -o "$OUT/imgui_imgui_draw.o"
  "$GXX" $CXXFLAGS -c "$IMGUI/imgui_tables.cpp" -o "$OUT/imgui_imgui_tables.o"
  "$GXX" $CXXFLAGS -c "$IMGUI/imgui_widgets.cpp" -o "$OUT/imgui_imgui_widgets.o"
  "$GXX" $CXXFLAGS -c "$IMGUI/backends/imgui_impl_win32.cpp" -o "$OUT/imgui_impl_win32.o"
  "$GXX" $CXXFLAGS -c "$IMGUI/backends/imgui_impl_dx9.cpp" -o "$OUT/imgui_impl_dx9.o"
  "$GCC" $CFLAGS -I"$MINHOOK/include" -I"$MINHOOK/src" -c "$MINHOOK/src/hook.c" -o "$OUT/mh_hook.o"
  "$GCC" $CFLAGS -I"$MINHOOK/include" -I"$MINHOOK/src" -c "$MINHOOK/src/buffer.c" -o "$OUT/mh_buffer.o"
  "$GCC" $CFLAGS -I"$MINHOOK/include" -I"$MINHOOK/src" -c "$MINHOOK/src/trampoline.c" -o "$OUT/mh_trampoline.o"
  "$GCC" $CFLAGS -I"$MINHOOK/include" -I"$MINHOOK/src" -c "$MINHOOK/src/hde/hde32.c" -o "$OUT/mh_hde32.o"
else
  for f in imgui_imgui imgui_imgui_draw imgui_imgui_tables imgui_imgui_widgets imgui_impl_win32 imgui_impl_dx9 mh_hook mh_buffer mh_trampoline mh_hde32; do
    cp -f "$FRAMEWORK_BUILD/$f.o" "$OUT/$f.o"
  done
fi

echo "== Lua objects =="
LUA_SRCS="lapi lauxlib lbaselib lcode lcorolib lctype ldblib ldebug ldo ldump lfunc lgc linit liolib llex lmathlib lmem loadlib lobject lopcodes loslib lparser lstate lstring lstrlib ltable ltablib ltm lundump lutf8lib lvm lzio"
lua_missing=0
for f in $LUA_SRCS; do [ -f "$FRAMEWORK_BUILD/lua_$f.o" ] || lua_missing=1; done
if [ "$lua_missing" -eq 0 ]; then
  cp -f "$FRAMEWORK_BUILD"/lua_*.o "$OUT/"
else
  echo "-- framework Lua objects missing, compiling vendored Lua directly --"
  for f in $LUA_SRCS; do
    "$GCC" $CFLAGS -I"$LUA/src" -c "$LUA/src/$f.c" -o "$OUT/lua_$f.o"
  done
fi

echo "== compiling harness + framework natives =="
"$GXX" $CXXFLAGS -c d3d9_harness.cpp -o "$OUT/harness.o"
for f in util scene state recorder overlay d3d9hook audio_hook plugin_host; do
  "$GXX" $CXXFLAGS -c "../native/$f.cpp" -o "$OUT/native_$f.o"
done

echo "== linking harness32.exe =="
"$GXX" -m32 -s -o ../bin/harness32.exe "$OUT"/*.o \
  -luser32 -lgdi32 -ld3d9 -ldxguid -lpsapi -ladvapi32 -ldwmapi -lwinmm \
  -static -static-libgcc -static-libstdc++ -Wl,--enable-stdcall-fixup \
  -Wl,--no-insert-timestamp

echo "== done =="
ls -l ../bin/harness32.exe
file ../bin/harness32.exe
