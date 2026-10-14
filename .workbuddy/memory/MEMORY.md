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
- **必须以 OpenGL 启动**：1.27 默认 D3D9，而 1.27a 分支的帧锚点是 `wglSwapLayerBuffers`，
  D3D 下模组加载但完全静止（F6 无效、日志停在初始化且无 `render frames=`）。
  启动方式：`war3.exe -opengl`（游戏目录有 `Start WarcraftCS (OpenGL).cmd`）。
  排查口诀：日志无 `render frames=`/`control key=` → 先看启动参数。
  该客户端还装了 ENB 的 `d3d9.dll` 代理（OpenGL 下不生效）与 `WarcraftHelper.dll`。

## 1.27a 控制台条带

- 1.27a **没有** WorldHook/UIHook 签名，`nativeUIPhase` 永不置位，`ShowInterface(FALSE)`
  只藏控件，控制台皮肤+底图每帧仍画在底带（世界视口 GL y=128 以下）。
- **正式方案（逆向，已替代早期的 CS 底板覆盖）**：在 `glDrawElements` 按几何判定——
  控制台确实走 `glDrawElements`（客户端顶点数组 2D 四边形）；判据为**正交投影**
  （`projection[11]==0 && projection[15]==1`，世界批次恒为透视）+ 全部顶点投影落在
  `FullscreenView::ConsoleStrip()` 条带内（容差 `max(12, strip*0.1)`），另加
  `count<=16 && maxY<=strip*1.6f` 的低矮小四边形规则吃掉皮肤顶梁。命中即交
  `minimap.Submit` 后吞掉（与 1.26a UIHook 同路径）。
- 同一批提交顺带喂原生小地图：`PresentFrame` 在 `!originalWorld` 时 `minimap.BeginFrame`，
  `FinishFrame` 无条件调用；letterbox 清屏惰性化（`capturedAny_/cleared_`），否则左侧留黑块。
- **顶部资源栏**（资源数字/菜单/昼夜图标， deepest 底边 top-64px，部分四边形向上越出屏）：
  正交 && x 在屏内 && `minY >= top-76 && maxY <= top+16` 即判定为顶栏。**必须用零面积 scissor
  裁剪（照常调用原始链），不能跳过调用**——客户端 `ijl15.dll`（WarcraftHelper 宽屏）改写这些
  批次，调用消失会使其失步 → 驱动读野指针 FATAL（~0x698x = GL ICD）。底部条带跳过是安全的。
  实现见 `ClassifyUiBatch()`（0=保留 1=条带 2=顶栏）。
- 1.26a 不受影响（`nativeUIPhase` 路径优先，几何判定只在 `!originalWorld` 生效）。

## 约定

- 地址**不允许**按调用点硬编码；加到 `tools/generate_native_offsets.py` 的表里，经 `wc3::Rva()` 引用。
- 提交前跑 `python tools/audit_sources.py --staged`；`.local`、`build`、`build2` 等不入库。
- 产物输出目录（`build.ps1 -OutputDirectory`）必须预置 `WarcraftOriginalMss.dll`（原始 Miles）；
  若只有 `Mss32.dll`，那其实是本模组 loader，会被误当原件复制，导致声音代理出错。
  中间产物固定在仓库根 `build/`，与输出目录无关。

## 1.27a 世界空间叠加（血条/标签）

- 1.27a 无 WorldHook，`WorldLabelView` 必须由 `PresentFrame`（swap 锚点）构建，否则血条全灭
  （`candidates=N submitted=0` 是判定特征）。
- 世界投影（vfov/aspect/near/far）1.27a 取不到透视钩子：从**全客户区 viewport 的第一个透视
  glDrawElements 批次**读 `GL_PROJECTION_MATRIX` 解码（n=m[14]/(m[10]-1)、f=m[14]/(m[10]+1)）。
  实测 1.27a FPS：vfov=41.7 aspect=2.278 near=100 far=10000——引擎自算 aspect ≠ 客户区比例，必须实测。
- 踩过的宏：`small`/`near`/`far` 都是 windows.h 宏，本地变量别用这些名字。
