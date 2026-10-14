# Warcraft CS — 项目长期记忆

## 编译工具链（本机）

- 本机**没有 Visual Studio**，`vswhere.exe` 不存在；`tools/build.ps1` 需显式指定工具链：
  ```
  tools/build.ps1 -CompilerRoot F:\PY\msvc -PythonExecutable <python.exe> -OutputDirectory F:\PY\cs\.local\warcraft-cs
  ```
  也可用环境变量 `WCS_COMPILER_ROOT`。`build.ps1` 已支持该参数（优先于 vswhere）。
- 工具链根：`F:\PY\msvc`（MSVC 14.44.35207 + Windows SDK 10.0.26100.0）。
  它原为 x64-only，已补入 x86 目标，**现可产出 32 位目标**：
  - `VC\Tools\MSVC\14.44.35207\bin\Hostx64\x86\` — x86 编译器（cl/c1/c2/link/lib/dumpbin）
  - `VC\Tools\MSVC\14.44.35207\lib\x86\` — x86 CRT（静态 + 导入库）
  - `Windows Kits\10\Lib\10.0.26100.0\{um,ucrt}\x86\` — 完整 SDK x86 导入库
  - `VC\Auxiliary\Build\vcvars32.bat` / `vcvarsall.bat` — **自写的自举脚本**（原文件是 0 字节空壳）
- **模组必须是 32 位**：`war3.exe` / `Game.dll` 是 PE32 (`machine=0x014C`)，注入进程位数必须一致。
  用 x64 宿主的 `Hostx64\x86` 编译器交叉编译到 x86，不是"改成 x64"。
- 复现/补齐工具（仓库外，`F:\PY\tools`）：
  - `fetch_vs_packages.py <包ID>...` — 从 `aka.ms/vs/17/release/channel` →
    `VisualStudio.vsman` 取包 CDN 直链，下载并校验 sha256
  - `resolve_vs_x86_packages.py` — 从组件 BFS 出依赖包集合
  - `merge_sdk_x86.py` — 把 `Microsoft.Windows.SDK.CPP.x86` 的 x86 导入库并入 SDK
  - `extract_vc_x86.py` — 解包 vsix（注意内部有 `Contents/` 前缀）
- **易踩的坑**：`vs_buildtools.exe --layout` 在本机卡死（要 UAC，无产出）。
  x86 桌面级导入库（`msvcrt/oldnames/vcruntime/msvcprt/delayimp.lib`）**不在**
  `CRT.x86.Desktop.base`（那里只有静态 CRT），而在 `CRT.x86.Store.base`，且投放到 `lib/x86/` 根目录；
  编译器资源 `clui.dll` 在 `Tools.*.TargetX86.Res.base.enu`，缺了会报 `fatal error C1510`。

## 受支持主机

- Warcraft III 1.26a x86（`Game.dll` 1.26.0.6401）= Complete
- Warcraft III 1.27a x86（`Game.dll` 1.27.0.52240）= Partial
- 主机清单的**唯一来源**是 `setup/host-build.ps1`，必须与运行时 `wc3::GameBuild::SelectBuild()`
  保持一致；两者不同步会导致"安装器放行、模块却拒绝挂钩"。
- 本机 1.27a 客户端在 `D:\game\Warcraft III 1.27 Reforged`。

## 1.27a 控制台条带

- 1.27a 控制台面板不走 glDrawElements（即时模式），`ShowInterface(FALSE)` 只藏控件；
  覆盖方案：Overlay（present 时）用 CS 底板盖住世界视口 GL y 偏移以下的条带，
  高度来自 `FullscreenView::ConsoleStrip()`（ViewportHook 在 `!originalWorld` 时观测）。
  1.26a 不受影响（恒 0）。1.27a Expand 全屏视口方案未生效，原因未查明。

## 约定

- 地址**不允许**按调用点硬编码；加到 `tools/generate_native_offsets.py` 的表里，经 `wc3::Rva()` 引用。
- 提交前跑 `python tools/audit_sources.py --staged`；`.local`、`build` 等不入库。
