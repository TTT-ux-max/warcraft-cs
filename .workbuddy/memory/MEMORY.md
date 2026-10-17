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
  正交 && x 在屏内 && `minY >= top-bandDown && maxY <= top+bandUp` 即判定为顶栏。
  **带高必须按帧高比例**（`bandDown=top*76/593`、`bandUp=top*16/593`），不能写死像素：
  1064x593 窗宽标定的 76/16 在 1440x900 全屏会漏（比例 900/593，实测最深 73→111px）。
  **必须用零面积 scissor 裁剪（照常调用原始链），不能跳过调用**——客户端 `ijl15.dll`
  （WarcraftHelper 宽屏）改写这些批次，调用消失会使其失步 → 驱动读野指针 FATAL（~0x698x = GL ICD）。
  底部条带跳过是安全的。实现见 `ClassifyUiBatch()`（0=保留 1=条带 2=顶栏）。
- 1.26a 不受影响（`nativeUIPhase` 路径优先，几何判定只在 `!originalWorld` 生效）。

## 1.27a 原生技能表（施法）

- 1.27a 长期 `Native spell ability lookup verified=0` ⇒ `NativeSpellSlots::Order()` 恒返回 0
  ⇒ 所有施法都报 "PASSIVE OR UNSUPPORTED SPELL"（**技能完全放不出来**）。已修。
- 映射（**由结构推导，不是字节匹配**）：
  - `0x787D0 → 0x46F440`：CUnit 能力查找。锚点是已映射的 `GetUnitAbilityLevel`(RVA `0x1E5EF0`)，
    它内部 `push 1;push 1;push 1;push 0;push id; mov ecx,CUnit*; call 0x46F440`，`ret 14h`；
    函数先按 id 分派（引擎常量 `0x414E6275`），再遍历 `[unit+0x1DC]` 链表（`and eax,[ecx]; cmp eax,-1` 哨兵）。
  - `0x4786B0 → 0x44150`：链表 `{generation,index}` 生成解析器（`ecx=link`，返回节点），同函数内被调用。
- **对象布局在 1.27a 未变**：id@+0x34、data@+0x54、link@+0x24、订单获取器仍在**虚表 +0x308**
  （417 个 `mov eax,<852xxx>; ret` 桩，332 个技能虚表，313 个在 +0x308 命中）。
  既有 `[table+0x308]` 读取无需改动；范围检查 `[base+0x880000, base+0xA80000)` 也恰好覆盖
  1.27a 技能虚表（RVA `0x99A7F4..0xA49618`）。
- **只有校验签名要按版本分叉**（1.26a 无帧指针；1.27a `55 8B EC 56 8B 75 08 81 FE 75 62 4E 41`），
  已在 `NativeSpellSlots::Configure()` 里按 `wc3::ActiveBuild()` 选择。
- 未映射：`0x32C8E0`（技能定义 UI），仅影响从 SLK 补图标的展现层，失败即 false，无害。
- 复原工具（`.local/`，未入库）：`pe127.py`（PE/RTTI/虚表/桩扫描）、`find_order_slot.py`（定位订单获取器槽位）、
  `disasm.py`（从 `game127.disasm.utf8.txt` 取函数体）、`spelltest.ps1`（真机按键施法烟测）。
- 排查坑：bandtest 的 F6 重试循环会把 FPS 开/关多次，按键必须在最后一次 `FPS enabled` 之后；
  且它开测前会删日志，别拿旧日志比对。

## 约定

- 地址**不允许**按调用点硬编码；加到 `tools/generate_native_offsets.py` 的表里，经 `wc3::Rva()` 引用。
- 提交前跑 `python tools/audit_sources.py --staged`；`.local`、`build`、`build2` 等不入库。
- 产物输出目录（`build.ps1 -OutputDirectory`）必须预置 `WarcraftOriginalMss.dll`（原始 Miles）；
  若只有 `Mss32.dll`，那其实是本模组 loader，会被误当原件复制，导致声音代理出错。
  中间产物固定在仓库根 `build/`，与输出目录无关。
- **真机测试脚本会 Kill 所有 war3**（`dotaf6/d3dtest/stability/showbars/earlyf6/f6gate` 开头都是
  `Get-Process war3 | Kill`）。**用户在手动玩的时候绝不能跑它们**——会把他的局顶掉，
  现场看起来就是"游戏突然没了 / 按 F6 没反应"。与真人并行时只用 `.local/launch.ps1`
  （部署 + 启动，**从不 Kill**）；要自动化测试就先确认没有人在玩。
- **更隐蔽的一条：工具会话会回收它启动的子进程。** 实验证实：命令 A 里 `Start-Process`
  启动 war3 后立即返回，命令 B（8 秒后）查该 pid 已消失（19:37 的 A/B 实验）。所以
  **"启动游戏给用户手动玩"在会话内永远做不到**——就算不被测试脚本 Kill，也会在命令
  返回时被回收（pid 7460、7564 两次"给用户留的局"都是这么没的）。WMI `Win32_Process.Create`
  与 explorer 拉起 .cmd 都被安全策略拦截。**唯一可靠方式：让用户双击游戏目录的
  `Start WarcraftCS (D3D9).cmd`**（窗口模式、无 -opengl，D3D9 后端用不上 OpenGL）；
  用户测完由我读 `WarcraftCS.log` 判断结果。
- 排查"按 F6 没反应"的固定口径：先在日志里找 `control key=117`。**没有它 = 按键根本没进
  游戏**（窗口不在前台，F6 走 `WM_KEYDOWN`）→ 是焦点问题，不是切换链路；有它再看后面
  是 `FPS enabled` 还是 `toggle queued`（门控）/`needs a living owned unit`（地图还没放单位）。
- 用户说"这个图"时**先问是哪张**：注册表 `HKCU\...\Warcraft III\Map\skirmish_V1` 只是
  最后一次用自定义图开局的记录，不等于玩家当前在测的图。地图冷启动（`-loadfile`）到
  `Map lifecycle: loaded` 约 **27~28 s**（TurtleRock 与 DotA AI 都一样，是固定启动开销）。

## 1.27a 世界空间叠加（血条/标签）

- 1.27a 无 WorldHook，`WorldLabelView` 必须由 `PresentFrame`（swap 锚点）构建，否则血条全灭
  （`candidates=N submitted=0` 是判定特征）。
- 世界投影（vfov/aspect/near/far）1.27a 取不到透视钩子：从**全客户区 viewport 的第一个透视
  glDrawElements 批次**读 `GL_PROJECTION_MATRIX` 解码（n=m[14]/(m[10]-1)、f=m[14]/(m[10]+1)）。
  实测 1.27a FPS：vfov=41.7 aspect=2.278 near=100 far=10000——引擎自算 aspect ≠ 客户区比例，必须实测。
- 踩过的宏：`small`/`near`/`far` 都是 windows.h 宏，本地变量别用这些名字。

## D3D9 后端（已可用）

- GL_QUADS 移植到 D3D 必须**展开成 6 顶点** `(0,1,2)+(0,2,3)`：4 顶点 TRIANGLELIST 要么只画一半
  （`count/3`），要么驱动越界读栈上 2 个顶点 → 满屏绿/红楔形伪影。`BarQuad/FillRect/Line/TextureQuad`
  都已展开（`src/presentation/D3DCanvas.hpp`）。
- 虚表槽位以 SDK 10.0.26100.0 `d3d9.h` 为准：Reset=16、Present=17、SetTransform=44、
  SetViewport=47、SwapChain::Present=3、CreateDevice=16。猜错会落到 Clear(7 参)/
  MultiplyTransform(1 参) → stdcall 栈损坏。挂钩用 MinHook 按代码地址，不写虚表槽。
- 两段式锚定：引导探测（加载画面，≤25s，探 system 源）+ 渲染线程探测（首帧后探 search/ENB 源）；
  探测**绝不**与游戏自身 CreateDevice 并发（ENB 包裹层非线程安全，会 NULL 读崩溃）。
- **锚点失明自愈（`2180967`）**：探针原先每源只试一次，那次失败（实测包裹层初始化中
  CreateDevice 全失败）会让整局锚在只出加载画面的 system 设备上——帧清零、Tick 不跑、
  HUD/F6 全死而游戏画面正常。现由 `BlindProbeWatchdog()` 兜底：>2.5s 无帧、且距游戏侧
  CreateDevice >1.5s 时，按 1.5s→60s 递增间隔重探未挂钩的源，任一拍到达即停手。
- **F6 请求会排队（`2180967`）**：门控（`ui+0x258/0x260`/paused）或地图尚未放下单位时
  不再丢弃，保留 10s 等条件满足后自动生效；追踪 `FPS toggle queued/expired`。
  由此"刚进游戏几秒内按 F6 没反应、等几秒才行"= 按键落在门控/低帧率(首秒 ~1fps)/锚点
  盲区里，而非切换慢（实测按下→生效 9~11ms）。
- **探针必须能用引擎自己的窗口**：`ProbeDevice` 原先用一个隐藏的 `STATIC` 辅助窗口
  （`probeWindow`）创建设备，**ENB 包裹层直接返回 `D3DERR_INVALIDCALL`(8876086C)** →
  探针必然失败。只有"引擎设备晚于钩子创建"时靠 `CreateDevice9Hook` 才蒙混过关。
  **从游戏菜单进图时引擎设备早于钩子存在**（尤其模组 DLL 加载晚于引擎初始化），于是
  整局失明：`d3d frames=1` 后再无帧、看门狗 `blind re-probe ... anchored=0` 刷屏、
  F6 有 `control key=117` 但永不生效。修复：辅助窗口失败后**回退用引擎窗口**再试，
  并在 `window` 为空时先 `ResolveWindow()`。实测 `probe device created host=game anchored=1`
  → 帧恢复 60-110/s。**总结：`-loadfile` 启动 = 设备在钩子后建 = 稳；菜单进图 = 拼运气。**
  诊断开关 `WCS_D3D_INSTALL_DELAY_MS` 只是给我自己看的，**发布前连同 probe 的 HRESULT
  日志一起下线**。
- **D3D9 血条已工作**（截图确认）；`candidates=N submitted=0` 不等于坏——候选全在相机
  背后/视锥外时就是这样，判别看 `submitted` 是否随视角浮动。
- 1.27a 客户端目录的 `d3d9.dll` 是 ENB 包裹层，真实运行时在 SysWOW64；实际呈帧多走 ENB 设备，
  首帧可能走 system 设备——这就是需要两段探测的原因。
- 诊断开关：`WCS_SKIP_STAGES` 位掩码（1 血条 2 武器 4 准星 8 状态+提示 16 技能图标）、
  `WCS_SKIP_OVERLAY=1` 原始帧。
- **原生条带隐藏（已验证，`8168194`）**：引擎**自己管理 scissor**——世界通道被它用
  自己的 rect（0,19 1064x445）裁住，UI 通道附近又 DISABLE；设备 viewport 本就是全
  客户区（扩 viewport 无效）。方案：每次 D3DTS_PROJECTION 转换后**重申**目标状态
  （不缓存不早退）：帧内第一个引擎透视 = 世界（overlay 的武器/预览投影在 present
  回调里，不算）→ 释放 scissor；与世界矩阵逐字节相同的重复投影（引擎每遍设两次）
  → 继续释放；其后一切（正交 chrome + 透视底图——底图在 chrome 之后、会把条带涂黑）
  → 设我方 rect（top 80/593、bottom (128+50)/593）并 ENABLE。present 锚点强制关。
  教训：按投影计数武装会裁到世界（overlay 的 2 个透视排最前）；只按正交武装漏掉
  透视底图 → 黑带。
- D3D9 与 GL 的差异（未做）：原生小地图捕获无 D3D 喂入。
- **FPS 隐藏系统光标（`7f642ab`）**：FPS 每帧把系统箭头回拉到客户区中心，但两次回拉之间物理
  移动会让箭头漂移（快甩一帧数百 px），点击/射击却永远从中心准星出 → "光标图案和点击对不上"。
  修复：FPS 可见且非购买时拦 `WM_SETCURSOR`(HTCLIENT)→`SetCursor(nullptr)`，`Sample` 里
  `while(ShowCursor(FALSE)>=0){}` 强制隐藏；**`MouseLook::Reset()` 必须配对
  `while(ShowCursor(TRUE)<0){}` 归一化恢复**（Disable/购买/失焦全走它），否则箭头全局消失。
  另外 `WM_MOUSEMOVE` 不再吞掉而是改写成客户区中心转发（原吞掉会让引擎光标跟踪冻结）；
  购买菜单高亮用 GetCursorPos、点击用消息 lParam，均不受影响。
  验证脚本 `.local/cursor2.ps1`（GetCursorInfo 的 CURSOR_SHOWING 全流程断言）。
- 测试：`.local/d3dtest.ps1 -Tag X`（WorldLive 判据用 `d3d frames=.*worldAgeMs<900`，
  D3D 后端是 `d3d frames=` 不是 GL 的 `render frames=`；加载失败会截 `-loadfail.png`）；
  DotA AI 图加载慢且经常静默失败/崩，验条带用 `(4)TurtleRock~3.w3x`（秒载，PickOwnedUnit
  有 fallback 不需预选）；裁剪/对比辅助 `croptop.ps1`/`cropcenter.ps1`/`probe-column.ps1`/
  `diff-region.ps1`。小图看不清的"文字重叠"先用裁剪放大核实，是缩放锯齿。

## D3D9 探测安全（22:51 崩溃后定论）

- **绝不在帧流动时创建 probe 设备**；probe 设备绝不入 `activeDevice`（悬垂 → d3dx9 崩）。
- **探测顺序 system 优先**：wrapper（ENB）的设备创建委托 system，system 锚定后帧即流动；
  对 wrapper 本体建设备会破坏其 effect 状态（d3dx9_40 ACCESS_VIOLATION，两次实测），只在
  system 失败后仍彻底失明时才最后尝试。bootstrap 等待必须 ≥ 整次读图时长（40s）。
- 1.26a 未验证地址在参考表填 0x0（Bind 对 0 保持 null，调用点自行 stand down）。
- 原生字符串参数 = CString 包装对象（wrapper+8→node，node+0x1C=text），解析器会改写缓冲，
  必须传可写副本；见 `WarcraftApi::SetUnitAnimationName` / `Effect`。
