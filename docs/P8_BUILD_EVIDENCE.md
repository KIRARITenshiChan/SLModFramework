# P8.1 — 可复现构建与发行包证据（【v1.20】2026-10-27）

> Phase 8 / P8.1 交付物。目的：证明发行二进制可由本仓库源码在干净目录中确定性重建，且只依赖 Windows 系统库。
> 构建产物：`Workspace/framework/bin/{SLModFramework32.dll, launcher32.exe, harness32.exe}`（开发目录），发行包由 `release/make_release.sh` 从 `bin` 暂存。

## 1. 环境与工具链

| 项目 | 值 |
|---|---|
| 主机 | Windows_NT 10.0.26100 x64（MINGW64_NT-10.0-26100） |
| 编译器 | winlibs MinGW-W64 **i686-ucrt-posix-dwarf r3, gcc/g++ 16.2.0**（`D:/devtools/winlibs-i686/mingw32/bin`） |
| 链接器 | GNU ld 2.47.20260726（同上） |
| bash | GNU bash 5.3.15 (x86_64-pc-cygwin) |
| 构建脚本 | `framework/build.sh`（DLL + launcher）、`framework/tests/build_harness.sh`（离线 harness） |
| 依赖源码 | `third_party/{imgui 1.91.9b, minhook 1.3.4, lua-5.4.7}`（均为 vendored，不依赖子模块/网络） |

## 2. 干净构建步骤（可复现）

1. 关闭所有 `SisterLocation.exe` / `launcher32.exe`（否则链接报 Permission denied）。
2. `rm -rf framework/build framework/bin/{SLModFramework32.dll,launcher32.exe,harness32.exe}`
3. `bash framework/build.sh` → 产出 DLL + launcher32.exe
4. `bash framework/tests/build_harness.sh` → 产出 harness32.exe
5. `sha256sum` 校验（见下表）

**确定性措施（2026-10-27 加入）**：
- 链接加 `-Wl,--no-insert-timestamp`（PE 头时间戳固定，消除逐次构建差异）；
- 移除 DLL 启动日志里的 `__DATE__/__TIME__`（原 `slmodf.cpp` 启动行是唯一随构建时刻变化的字节；日志文件名本身已含精确时间）。
- 验证：连续两次完整构建，三个二进制 SHA-256 **完全一致**（下表为第二次结果）。
- 2026-10-27 06:30（P8.2 代码合入后）**重新验证**：DLL/launcher/harness 连续两次构建哈希仍完全一致（见 §3）。
- 2026-10-27 06:55（P8.5 GUI 高度/滚动修复后）**最终 1.0.0 复验**：三产物连续两次构建哈希一致，哈希见 §3（DLL/launcher 与发行包一致；harness 因 overlay 修复重链接）。

## 3. 产物哈希（本次干净构建，连续两次一致）

| 文件 | 字节 | SHA-256 |
|---|---|---|
| `bin/SLModFramework32.dll` | 1,617,920 | `fd83f195f220e584712dd89be2b7b177d2c596c737cba40b9b43f59f4e420b1e` |
| `bin/launcher32.exe` | 31,744 | `22daac7fe3a197e7b0bd29395274361eefa02f96def7dd42df05ac3fc02b85ea` |
| `bin/harness32.exe` | 1,517,056 | `248ab25b34fde79a047d85718a6fe89b3c57eb66aa8fe4ca8f8627ec558cb24d` |

源码清单（27 个文件）见 `release/BUILD_MANIFEST.sha256`：
- `native/*.cpp,*.h`、`launcher/*.c,*.h`、`tests/*.cpp,*.sh`、`build.sh`、`deploy.sh`；
- 依赖版本与 vendored 源见 `third_party/`（发行包不含源码，许可证见 `THIRD_PARTY_NOTICES.md`）。

## 4. 依赖审计（objdump -p 导入表）

**SLModFramework32.dll**：
`KERNEL32.dll, USER32.dll, GDI32.dll, SHELL32.dll, WINMM.DLL, dwmapi.dll` + `api-ms-win-crt-{convert,environment,filesystem,heap,locale,math,private,runtime,stdio,string,time,utility}-l1-1-0.dll`
**launcher32.exe**：
`KERNEL32.dll, USER32.dll` + `api-ms-win-crt-{convert,environment,heap,locale,math,private,runtime,stdio,string,utility}-l1-1-0.dll`

结论：
- 无 `libstdc++-6.dll / libgcc_s_*.dll / libwinpthread-1.dll` 等 MinGW 运行库依赖（`-static -static-libgcc -static-libstdc++` 生效）；
- 所有 UCRT `api-ms-win-crt-*` 在 Windows 10/11 上由系统提供（本机 10.0.26100 实测可加载）；
- 因此发行包 **只需两个 exe/dll 文件**，无需附带运行库；不需要 VC++ Redistributable。

## 5. 离线回归（本机构建后立即执行）

| 模式 | 结果 | 日志 |
|---|---|---|
| `harness32.exe --wndproc-only` | rc=0 **PASS** | – |
| `harness32.exe --imgui-only` | rc=0 **PASS** | `logs/framework/harness-20261027-065639.log` |
| `harness32.exe --plugins` | rc=0 **PASS** | `logs/framework/harness-20261027-065640.log` |
| `harness32.exe --p8-only`（P8.2 新增：轮转+诊断包） | rc=0 **PASS** | `logs/framework/harness-20261027-065640.log`、`logs/framework/diagnostics-20261027-065640.zip` |

harness 不链接 `slmodf.cpp`（无注入入口），覆盖 WndProc 输入分流、ImGui 构建/字体、插件宿主 6 夹具、P8.2 日志轮转与诊断包。真机冒烟见 `docs/P8_STATUS.md` 与 `logs/probe/p8-layoutB-smoke.md`；P8.2 细节见 `docs/P8_STABILITY.md`。

## 6. 发行包内容（P8.5 已生成最终 ZIP）

`release/make_release.sh` 的暂存布局：
```
SLModFramework/
  bin/launcher32.exe
  bin/SLModFramework32.dll
  config/framework.ini.example
  plugins/HelloPlugin/{plugin.ini,main.lua}
  docs/{README,INSTALL,UNINSTALL,TROUBLESHOOTING,CHANGELOG,PLUGIN_API,LICENSE,THIRD_PARTY_NOTICES}
  README.md  (顶层快速开始，中文为主 + 英文摘要)
```
不包含：其余示例插件（SoftJumpscares/ThreatTelegraphs）、游戏派生素材（`assets/sfx`、recon dump、logs）、开发工具与 harness。

最终 ZIP：`release/SLModFramework-1.0.0-win-x86.zip`，805,520 B，SHA-256 `f8932e2513efbb70ae662d67ae51e21c62f84afc75bcfcb5a27b90980acc7859`；`release/BUILD_MANIFEST.sha256` 18 files。安装/卸载验证见 `docs/P8_ACCEPTANCE_DRILL.md`。

## 7. 已知限制

- 构建脚本绑定 `D:/devtools/winlibs-i686` 路径（本机约定）；换机需改两处 `GCC/GXX` 或设置环境变量。
- 字节级复现依赖同一工具链版本；不同 gcc/binutils 版本产出的 PE 不保证一致（功能等价，哈希会变）。
- 发行包二进制尚未代码签名；Windows SmartScreen 首次运行可能提示（P8.4 文档说明）。

## 8. 参考

- `docs/P8_STATUS.md`、`docs/INSTALL_LAYOUT.md`、主计划 `SL-Mod-Framework-Plan.md` Phase 8 §P8.1
