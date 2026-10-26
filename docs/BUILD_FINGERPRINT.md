# 构建指纹档案（BUILD_FINGERPRINT）

- 状态：已完成（静态，2026-10-25）。锁定对象：本机 Steam 版 `SisterLocation.exe`。
- 用途：框架启动时判定「是否已验证版本」；P8 版本门禁的唯一依据。

## 1. 文件级指纹

| 字段 | 值 |
| --- | --- |
| 文件名 | SisterLocation.exe |
| 路径 | `E:\SteamLibrary\steamapps\common\Five Nights at Freddy's Sister Location\SisterLocation.exe` |
| 大小 | 950,556,728 字节 |
| SHA-256 | `c60e1c7994316426d824b82004aa5c76e3aab06d4e78e0c9391628e0d52b2c73` |
| SHA-1 | `4a8ce3c74cef76fd67b3e5ca80eb5bd1b36576cc` |
| MD5 | `cc2b5b61426c2593ce050edd44249211` |
| mtime | 2026-10-25T05:14:05+08:00 |

机器可读版本：`analysis/data/fingerprint.json`；校验工具：`tools/verify_fingerprint.py`。

## 2. PE 头指纹

| 字段 | 值 |
| --- | --- |
| Machine | 0x14c（i386 / 32 位） |
| Optional magic | 0x10b（PE32） |
| ImageBase | 0x400000 |
| EntryPoint RVA | 463014（0x710a6） |
| Linker version | 11.0 |
| COFF timestamp | 1467277395 = 2016-06-30T09:03:15Z |
| Subsystem | 2（GUI） |
| Section 数 | 5 |
| Overlay 偏移/大小 | 1,082,368 / 949,474,360 |

节区表：

| 节区 | VA | VirtualSize | RawSize | RawPtr |
| --- | --- | --- | --- | --- |
| .text | 0x1000 | 564107 | 564224 | 1024 |
| .rdata | 0x8b000 | 136262 | 136704 | 565248 |
| .data | 0xad000 | 32076 | 23552 | 701952 |
| .rsrc | 0xb5000 | 303672 | 304128 | 725504 |
| .reloc | 0x100000 | 52508 | 52736 | 1029632 |

导入特征（用于区分版本）：
- 常规导入：COMCTL32、WINMM、KERNEL32、USER32、GDI32、COMDLG32、SHELL32；
- Delay import 仅 `MMFS2.dll`，且全部按序号（无函数名），序数列表是本版本最强的静态特征之一；
- **无** `steam_api*.dll` 导入、无 SteamStub/.bind 节区、无 CLR/TLS 异常目录。

## 3. Overlay / pack 布局

| 偏移 | 内容 |
| --- | --- |
| 0x108400（1,082,368） | Fusion pack 头 32B：hdrSize=32、dataSize=1,311,581、fmt=0x179dc5a、files=8 |
| 0x108420 起 | 8 条内嵌文件记录（nameLen u16 + UTF-16 名称 + unused i32 + size i32 + payload） |
| 记录结束 2,393,917 | PAMU chunk 流开头（CCN 数据区） |

内嵌文件（pack 记录 size = 压缩后负载大小；除 mmfs2.dll 外均为 zlib 流 `78 DA`，运行时解压）：

| 名称 | pack 内大小(B) | 运行时解压后(B) | 解压后 SHA-256（前 16） | 作用 |
| --- | --- | --- | --- | --- |
| mmfs2.dll | 470,528 | 470,528（raw PE） | 1cb098cfa35e4692 | Fusion 2.5 运行时（PE32 x86，2016-06-30T05:14:46Z） |
| mmf2d3d9.dll | 556,551 | 1,128,960 | fce87bf918787de7 | Direct3D 9 渲染器（本机实际使用） |
| mmf2d3d8.dll | 215,078 | 432,128 | beb827c2d35dec1b | Direct3D 8 渲染器（备选，本机未加载） |
| Perspective.mfx | 5,860 | 15,360 | 090925a4cd961f22 | Fusion 扩展 |
| kcini.mfx | 5,398 | 28,672 | 52cdd0d5265dbe45 | INI 扩展（帧内 Ini 对象来源） |
| kcclock.mfx | 49,996 | 110,592 | f2319a7359fbcb88 | 时间扩展 |
| mp3flt.sft | 4,215 | 24,576 | cd5bd7cc59eaf42b | MP3 声道过滤器 |
| waveflt.sft | 3,633 | 8,192 | 41136b09b033a20b | WAV 声道过滤器 |

> 校验：静态解析 `recon/out/ccn_probe13.json` 中 7 个 zlib 记录的 sig 均为 `78da…`，mmfs2.dll 为 `4d5a9000`；运行时解包目录 `%TEMP%\mrt*.tmp\` 中文件的 SHA-256 与上表一致（P2a 实测，见 `docs/RUNTIME_PROBE.md` §4）。早期笔记中"payload 全是 raw"的说法有误，以本表为准。

## 4. Fusion 应用指纹

- 运行时版本：runtimeVersion=0x302（770），runtimeSubversion=0，productVersion=3，**productBuild=286**。
- 顶层 chunk 共 63 个，正好覆盖到文件末尾（Last chunk @950,556,720）；含 36 个 Frame chunk、ImgHandles/Images、FontHandles/Fonts、SndHandles/Sounds。
- 数据规模：Images chunk 780,370,748 B；Sounds chunk 167,474,419 B；banks.json 记录 images 5260、sounds 392。
- 关键字符串（可作辅助指纹）：
  - Name = "Sister Location"
  - Author = "Scott Cawthon"
  - Copyright = "Scott Cawthon"
  - EditorFilename 前缀 `C:\Users\Scott\Desktop\F…`
  - TargetFilename 含 `…\Users\Scott\Desktop\sdk\tools\ContentBuilder\content\windows_cont…`
- 帧名/事件等 Frame 子 chunk 被 flag=3（压缩+加密）保护，需 Fusion 的 state 解密（key 由 name/copyright/editorFilename 派生，build>284 顺序）；本机已有 Python 移植实现（`Workspace/recon/scripts/`）。

## 5. Steam 侧指纹

| 字段 | 值 |
| --- | --- |
| appid | 506610 |
| buildid / TargetBuildID | 1497593 / 1497593 |
| depot | 506611，manifest 749438696463073559 |
| SizeOnDisk | 950,556,728（与 EXE 一致） |
| StateFlags | 4（完全安装） |
| lastupdated | 1792876462 ≈ 2026-10-25 |

## 6. 版本判定与失败行为（写入 P8）

1. 启动时计算 EXE 大小 + SHA-256，与 `fingerprint.json` 对比；同时可读取 PE COFF timestamp、Delay-import 序数表做二次确认。
2. 任何一项不符 → `version_status=unsupported/unknown`：
   - 允许：日志、GUI 框架信息、纯 UI 插件；
   - 禁用：依赖内存布局/计数器/音频 Hook 的插件（DebugInspector/SoftJumpscares/ThreatTelegraphs）；
   - 输出明确提示与「重新采集指纹」指引，不静默套用旧偏移。
3. 指纹一致但运行时模块异常（mmfs2 未加载、渲染器不同）→ 同样降级为 unknown，记录原因。

## 7. 复现命令

```
python tools/verify_fingerprint.py
# 期望最后一行: fingerprint: MATCH (目标版本)

python recon/scripts/pe_probe.py        # 生成 recon/out/pe_probe.json
python recon/scripts/pe_probe13.py      # pack 布局 + PAMU + 顶层 chunk 统计
```

## 8. 运行时指纹与解包产物（Phase 2a 实测，2026-10-25）

- Fusion 运行时在每次启动时把 §3 的 8 个文件解包到 `%TEMP%\mrt*.tmp`（目录名随机，旧目录不清理）；解包后哈希见 §3 表。**安装目录内不产生任何文件**，故不可通过替换 EXE 旁文件改资源。【实测】
- 运行时实际加载的模块（P2a 两次运行一致，除基址受 ASLR 影响）：
  - `mmfs2.dll` 基址 0x67D90000（两次同址，size 0x78000；版本资源 3.0.286.6 "MMF2 Runtime Library" / Clickteam）；
  - `mmf2d3d9.dll` size 0x11B000（本机实际渲染器），只导入 `d3d9.dll!Direct3DCreate9`；
  - 扩展 `Perspective.mfx`、`kcini.mfx`、`kcclock.mfx`、`mp3flt.sft`、`waveflt.sft` 均作为 DLL 加载；
  - `d3d9.dll`、`nvd3dum.dll`（NVIDIA D3D9 UMD）、`DSOUND.dll`、`WINMM.dll`、`winmmbase.dll`、`DDRAW.dll` 已加载；
  - `mmf2d3d8.dll` 已解包但**未加载**；
  - 无 Steam/第三方 overlay 模块；有系统兼容层 `apphelp.dll`/`AcLayers.DLL`。
- 内存中的 CCN 头：PAMU 位于私有可读写堆区，字段 runtimeVersion=0x302、productVersion=3、productBuild=286；地址两次运行分别为 0x016257D8 / 0x00E76008（随机），**禁止硬编码**。内存结构比文件头少 runtimeSubversion 字段。【实测】
- 版本门禁（§6）补充：运行时事实可作为二次判定——若 `mmfs2.dll` 未加载、或实际渲染器不是 `mmf2d3d9`、或内存 PAMU 的 build ≠ 286，均降级为 unsupported/unknown 并记录。
- 退出缺陷：对主窗口发 WM_CLOSE 时进程以 0xC0000005 退出，**原版未注入进程同样如此** → 版本兼容测试必须以"原版对照"排除该原生缺陷（P8 测试矩阵）。【实测】
