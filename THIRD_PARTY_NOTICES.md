# Third-Party Notices / 第三方许可证声明

SLModFramework 本身以 **MIT** 许可证发布（见根目录 `LICENSE`）。发行二进制静态链接或内含以下第三方组件；本文件按其许可证要求保留版权与许可声明。完整许可证文本随发行包放在 `docs/third_party/`，源码仓库中位于 `third_party/`。

## 1. Dear ImGui 1.91.9b

- 用途：运行时 GUI / HUD 渲染（编译进 `SLModFramework32.dll`）。
- 许可证：**MIT**
- 版权：Copyright (c) 2014-2025 Omar Cornut
- 上游：https://github.com/ocornut/imgui
- 许可证全文：`third_party/imgui/LICENSE.txt`

## 2. MinHook 1.3.4

- 用途：D3D9 / DirectSound / user32 API Hook（编译进 `SLModFramework32.dll`）。
- 许可证：**BSD 2-Clause**
- 版权：Copyright (c) 2009-2017 Tsuda Kageyu
- 上游：https://github.com/TsudaKageyu/minhook
- 许可证全文：`third_party/minhook/LICENSE.txt`

## 3. Lua 5.4.7

- 用途：插件沙箱脚本运行时（编译进 `SLModFramework32.dll`）。
- 许可证：**MIT**
- 版权：Copyright (c) 1994-2024 Lua.org, PUC-Rio
- 上游：https://www.lua.org/
- 许可证全文（Lua 官方 MIT 文本，原包 `COPYRIGHT` 在 vendored 时未保留，此处按上游原文收录）：

```
Copyright (C) 1994-2024 Lua.org, PUC-Rio.

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```

## 4. MinGW-w64 / GCC 运行库（静态链接）

- 用途：C/C++ 运行时与线程支持被**静态链接**进两个发行二进制（无外部 DLL 依赖）。
- 许可证：**GPLv3 + GCC Runtime Library Exception**（libstdc++/libgcc）；MinGW-w64 头文件与运行时为公有领域/宽松许可（详见其 `COPYING` 系列）。
- 上游：https://gcc.gnu.org/onlinedocs/libstdc%2B%2B/manual/license.html 、https://www.mingw-w64.org/
- 说明：GCC Runtime Library Exception 允许在非 GPL 程序中以目标码形式分发经静态链接的运行时；本项目的对应源码（构建脚本、框架源码、vendored 依赖）随公开仓库提供。
- 发行包不包含 MinGW 工具链本体。

## 5. 未包含的第三方内容

- **Clickteam Fusion 运行时 / ctfak-cpp**：仅开发期本地逆向分析使用，不随源码仓库或发行包分发。
- **游戏本体及其派生素材**（音频、贴图、dump、截图等）：版权归 Scott Cawthon / Steel Wool 等权利人所有，**不随包分发**；详见 `docs/P8_ASSET_AUDIT.md`。
- **Windows 系统库**（kernel32/user32/d3d9/dsound 等）：由操作系统提供，不在分发范围。
