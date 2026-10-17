#include "PluginRuntime.hpp"
#include "../input/InputDispatcher.hpp"
#include "../platform/NativeFloatingText.hpp"
#include "../platform/NativeMinimap.hpp"
#include "../platform/WarcraftApi.hpp"
#include "../platform/DiagnosticLog.hpp"
#include "../platform/GameWindowInput.hpp"
#include "../platform/NativeFrameRate.hpp"
#include "../platform/NativeCameraFarClip.hpp"
#include "../platform/UnitHealthBars.hpp"
#include "../application/ShooterController.hpp"
#include "../presentation/Overlay.hpp"
#include "../presentation/GlCanvas.hpp"
#include "../platform/D3DHooks.hpp"
#include "../presentation/ReticleDiagnostics.hpp"
#include "../platform/MapCameraGuard.hpp"
#include "../platform/ActorRenderFilter.hpp"
#include "../platform/FpsCombatGuard.hpp"
#include "../platform/NativeMovementGuard.hpp"
#include "../platform/NativeWeaponAttack.hpp"
#include "../presentation/FullscreenView.hpp"
#include "../presentation/FpsProjection.hpp"
#include <MinHook.h>
#include <algorithm>
#include <cstring>
#ifdef WCS_FLOATING_TEXT_TEST
#include "../../tests/FloatingTextScene.hpp"
#endif
#ifdef WCS_STATUS_TEST
#include "../../tests/StatusEffectScene.hpp"
#endif
#ifdef WCS_GAMEPLAY_TEST
#include "../../tests/RuneCombatScene.hpp"
#endif
#ifdef WCS_TREE_WHEEL_TEST
#include "../../tests/TreeAndWheelScene.hpp"
#endif

#ifdef WCS_SURFACE_TEST
#include "../../tests/WorldSurfaceScene.hpp"
#endif
#ifdef WCS_MOVEMENT_TEST
#include "../../tests/MovementScene.hpp"
#endif

static ShooterController controller;
static Overlay overlay;
static GlCanvas glCanvas;
static NativeFrameRate frameRate;
static NativeCameraFarClip cameraFarClip;
static UnitHealthBars healthBars;
static WorldLabelView healthBarView;
static float healthNear=8,healthFar=15000;
static float healthVerticalFov=85,healthAspect=1;
static NativeMinimap minimap;
static bool healthy = true;
static DWORD lastWorld = 0;
static char root[MAX_PATH];
static uintptr_t gameBase = 0;
using RenderWorld = int (__fastcall*)(uintptr_t, uintptr_t);
using Swap = BOOL (WINAPI*)(HDC, UINT);
using Pause = void (__cdecl*)(BOOL);
using Perspective = void (__fastcall*)(uintptr_t, uintptr_t, float, float, float, float);
using RenderUI = void (__fastcall*)(uintptr_t, uintptr_t);
static RenderWorld originalWorld = nullptr;
static Swap originalSwap = nullptr;
// Whatever the engine's import entry held before we claimed it. Capture overlays install
// themselves there, and forwarding through them keeps their own overlay working.
static Swap swapForward = nullptr;
static Pause originalPause = nullptr;
static Perspective originalPerspective = nullptr;
static RenderUI originalUI = nullptr;
static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM key, LPARAM data);
static GameWindowInput windowInput(WindowProc);
static bool nativeUIPhase = false, overlayPass = false;
static uintptr_t mapUI = 0;
using DeleteContext = BOOL (WINAPI*)(HGLRC);
using Interface = void (__cdecl*)(BOOL, float*);
static DeleteContext originalDeleteContext = nullptr;
static Interface originalInterface = nullptr;
// The input window used to attach only from the present anchors, so a session that never
// reached a first frame (map still loading, boot splash, or a session stuck before the world
// renders) could not receive F6 at all: the press was neither queued nor logged, and the log
// could not tell a focus problem from a dead session. The message pump is the earliest point
// that always runs on the game's UI thread - the thread that owns the window, which is the
// only thread allowed to install a window procedure. Once attached, the steady state is one
// pointer compare per pump call.
using GetMessageW_t = BOOL (WINAPI*)(LPMSG, HWND, UINT, UINT);
using PeekMessageW_t = BOOL (WINAPI*)(LPMSG, HWND, UINT, UINT, UINT);
static GetMessageW_t originalGetMessageW = nullptr, originalGetMessageA = nullptr;
static PeekMessageW_t originalPeekMessageW = nullptr, originalPeekMessageA = nullptr;
static DWORD pumpSearchTick = 0;
struct PumpWindowSearch { HWND best; LONG area; };
static BOOL CALLBACK PumpWindowEnum(HWND candidate, LPARAM context) {
    auto search = reinterpret_cast<PumpWindowSearch*>(context);
    RECT rect = {};
    if (!IsWindowVisible(candidate) || IsIconic(candidate) || !GetWindowRect(candidate, &rect)) return TRUE;
    LONG area = (rect.right - rect.left) * (rect.bottom - rect.top);
    if (area > search->area) { search->area = area; search->best = candidate; }
    return TRUE;
}
static void ObservePumpWindow() {
    if (windowInput.Window()) return;
    DWORD now = GetTickCount();
    if (now - pumpSearchTick < 500) return;
    pumpSearchTick = now;
    PumpWindowSearch search = {};
    EnumThreadWindows(GetCurrentThreadId(), PumpWindowEnum, reinterpret_cast<LPARAM>(&search));
    // Observe re-checks ownership and thread affinity itself, so a mismatched thread simply
    // declines and the search retries on a later beat.
    if (search.best && windowInput.Observe(search.best)) controller.AttachWindow(search.best);
}
static BOOL WINAPI GetMessageWHook(LPMSG message, HWND window, UINT lowest, UINT highest) {
    ObservePumpWindow();
    return originalGetMessageW(message, window, lowest, highest);
}
static BOOL WINAPI GetMessageAHook(LPMSG message, HWND window, UINT lowest, UINT highest) {
    ObservePumpWindow();
    return originalGetMessageA(message, window, lowest, highest);
}
static BOOL WINAPI PeekMessageWHook(LPMSG message, HWND window, UINT lowest, UINT highest, UINT remove) {
    ObservePumpWindow();
    return originalPeekMessageW(message, window, lowest, highest, remove);
}
static BOOL WINAPI PeekMessageAHook(LPMSG message, HWND window, UINT lowest, UINT highest, UINT remove) {
    ObservePumpWindow();
    return originalPeekMessageA(message, window, lowest, highest, remove);
}
// Builds with no world/UI anchor have nothing that marks the native UI pass, so the console
// cannot be dropped at GL submission the way the 1.26a path drops it. The engine's own
// interface call - the one cinematics use - hides it instead, and the FPS HUD draws its own
// replacement chrome. A request this runtime makes for its own HUD is not a map cinematic.
static bool interfaceHiddenForFps = false;
static bool interfaceReassert = false;
static bool selfInterfaceRequest = false;
static void SyncInterface(bool visible, bool allow = false);
using DrawElements = void (APIENTRY*)(GLenum, GLsizei, GLenum, const void*);
static DrawElements originalDrawElements = nullptr;
using Viewport = void (APIENTRY*)(GLint, GLint, GLsizei, GLsizei);
static Viewport originalViewport = nullptr, originalScissor = nullptr;
static bool WorldRectangle(RECT& rect) {
    return healthy && controller.Visible() && windowInput.Window() &&
        GetClientRect(windowInput.Window(), &rect) && rect.right > 0 && rect.bottom > 0;
}
// Log each distinct rect once per session; per-frame alternation would otherwise flood the log.
// The cache resets when FPS visibility flips so the FPS-state rect inventory gets its own pass.
static bool RectSeen(GLint x, GLint y, GLint w, GLint h, GLint* cache, unsigned& count) {
    static int lastVisible = -1;
    int visible = controller.Visible() ? 1 : 0;
    if (visible != lastVisible) { lastVisible = visible; count = 0; }
    for (unsigned i = 0; i < count; ++i)
        if (cache[i*4]==x && cache[i*4+1]==y && cache[i*4+2]==w && cache[i*4+3]==h) return true;
    if (count < 24) { cache[count*4]=x; cache[count*4+1]=y; cache[count*4+2]=w; cache[count*4+3]=h; ++count; }
    return false;
}
// ---------------------------------------------------------------------------
// Per-second GL state statistics: proves which call path re-issues (or restores)
// the world viewport between our present-time force and the next world pass.
// ---------------------------------------------------------------------------
static volatile long glStatVp = 0, glStatSc = 0, glStatPush = 0, glStatPop = 0;
static GLint glStatLastVp[4] = {}, glStatLastSc[4] = {};
static void APIENTRY ViewportHook(GLint x, GLint y, GLsizei width, GLsizei height) {
    RECT rect={};
    static GLint seen[24*4]; static unsigned seenCount = 0;
    InterlockedIncrement(&glStatVp);
    glStatLastVp[0]=x; glStatLastVp[1]=y; glStatLastVp[2]=width; glStatLastVp[3]=height;
    if (!RectSeen(x,y,width,height,seen,seenCount)) {
        bool expanded=false; char client[48]={};
        if (WorldRectangle(rect)) {
            expanded = (width>rect.right*3/4 && height>rect.bottom/2);
            _snprintf_s(client,sizeof(client),"client=%ldx%ld",rect.right,rect.bottom);
        } else _snprintf_s(client,sizeof(client),"no-client(visible=%d)",controller.Visible()?1:0);
        wc3::Trace("viewport %d,%d %dx%d %s expanded=%d", x,y,width,height,client,expanded?1:0);
    }
    // Record the engine's own world viewport before expansion; its bottom offset measures
    // the native console strip that 1.27a keeps drawing behind the hidden interface.
    if (!originalWorld && WorldRectangle(rect))
        FullscreenView::ObserveWorldViewport(rect.right,rect.bottom,x,y,width,height);
    if (WorldRectangle(rect)) FullscreenView::Expand(rect.right,rect.bottom,x,y,width,height);
    originalViewport(x,y,width,height);
}
static void APIENTRY ScissorHook(GLint x, GLint y, GLsizei width, GLsizei height) {
    RECT rect={};
    static GLint seen[24*4]; static unsigned seenCount = 0;
    InterlockedIncrement(&glStatSc);
    glStatLastSc[0]=x; glStatLastSc[1]=y; glStatLastSc[2]=width; glStatLastSc[3]=height;
    if (!RectSeen(x,y,width,height,seen,seenCount)) {
        bool expanded=false; char client[48]={};
        if (WorldRectangle(rect)) {
            expanded = (width>rect.right*3/4 && height>rect.bottom/2);
            _snprintf_s(client,sizeof(client),"client=%ldx%ld",rect.right,rect.bottom);
        } else _snprintf_s(client,sizeof(client),"no-client(visible=%d)",controller.Visible()?1:0);
        wc3::Trace("scissor %d,%d %dx%d %s expanded=%d", x,y,width,height,client,expanded?1:0);
    }
    if (WorldRectangle(rect)) FullscreenView::Expand(rect.right,rect.bottom,x,y,width,height);
    originalScissor(x,y,width,height);
}
using PushAttrib = void (APIENTRY*)(GLbitfield);
using PopAttrib = void (APIENTRY*)(void);
static PushAttrib originalPushAttrib = nullptr;
static PopAttrib originalPopAttrib = nullptr;
static void APIENTRY PushAttribHook(GLbitfield mask) {
    InterlockedIncrement(&glStatPush);
    originalPushAttrib(mask);
}
static void APIENTRY PopAttribHook(void) {
    InterlockedIncrement(&glStatPop);
    originalPopAttrib();
    // An attribute pop restores the viewport captured at push time, silently undoing the
    // present-time full-window force. Re-assert it so the next world pass stays fullscreen.
    if (healthy && controller.Visible() && windowInput.Window()) {
        RECT client = {};
        if (GetClientRect(windowInput.Window(), &client) && client.right > 0 && client.bottom > 0) {
            originalViewport(0, 0, client.right, client.bottom);
            originalScissor(0, 0, client.right, client.bottom);
        }
    }
}

// ---------------------------------------------------------------------------
// World projection capture (swap-anchored builds). The anchored path reads the
// real perspective matrix inside its projection hook; a build whose projection
// hook cannot install has no such source, and every world-space overlay would
// project with stale screen-space guesses. The matrix is not recoverable from
// projection loads: the engine also submits small 3D viewports (portraits) that
// load their own. It is recoverable where the world is actually drawn - world
// batches go through glDrawElements with that very matrix bound and only the
// full-client viewport pass belongs to the visible world. Decoding it there
// gives the exact vertical field of view, aspect and clip planes of the frame.
// ---------------------------------------------------------------------------
// verticalFov, aspect, near, far of the world projection seen this frame.
static float frameProjection[4] = {};
static bool frameProjectionValid = false;
static void CaptureWorldProjection(const float* matrix, const GLint* viewport) {
    RECT client = {};
    if (!windowInput.Window() || !GetClientRect(windowInput.Window(), &client) || client.right <= 0 || client.bottom <= 0) return;
    if (viewport[0] != 0 || viewport[1] != 0 || viewport[2] != client.right || viewport[3] != client.bottom) return;
    if (matrix[5] <= 0.0f || matrix[0] <= 0.0f) return;
    // `near`/`far` are windows.h macros, so the clip planes need local names.
    float clipNear = matrix[14] / (matrix[10] - 1.0f), clipFar = matrix[14] / (matrix[10] + 1.0f);
    float verticalFov = 2 * std::atan(1 / matrix[5]) / 0.01745329252f, aspect = matrix[5] / matrix[0];
    if (!std::isfinite(clipNear) || !std::isfinite(clipFar) || !std::isfinite(verticalFov) || !std::isfinite(aspect)) return;
    if (clipNear <= 0 || clipFar <= clipNear || aspect <= 0) return;
    frameProjection[0] = verticalFov; frameProjection[1] = aspect;
    frameProjection[2] = clipNear; frameProjection[3] = clipFar;
    frameProjectionValid = true;
}
// Publish the projection captured during the frame being presented, then reset so the next
// frame publishes only what it rendered itself.
static void ApplyCapturedProjection() {
    if (!frameProjectionValid) return;
    healthVerticalFov = frameProjection[0]; healthAspect = frameProjection[1];
    healthNear = frameProjection[2]; healthFar = frameProjection[3];
    frameProjectionValid = false;
    static DWORD capturedLog = 0;
    if (DiagnosticLog::Due(capturedLog))
        wc3::Trace("world projection verticalFov=%.1f aspect=%.3f near=%.1f far=%.1f",
            healthVerticalFov, healthAspect, healthNear, healthFar);
}
// Direct3D form of CaptureWorldProjection: the device reports its own projection transforms, so
// the world pass is recognised the same way (full-client viewport, perspective shape) and decoded
// from the left-handed matrix. Our own weapon and preview projections are set inside the present
// callback, so the overlay pass flag keeps them out.
static void CaptureD3DProjection(const float* matrix, const int* viewport) {
    if (overlayPass || frameProjectionValid || !matrix || !viewport) return;
    RECT client = {};
    if (!windowInput.Window() || !GetClientRect(windowInput.Window(), &client) || client.right <= 0 || client.bottom <= 0) return;
    if (viewport[0] != 0 || viewport[1] != 0 || viewport[2] != client.right || viewport[3] != client.bottom) return;
    // Left-handed perspective only: _34==1, _44==0 and a depth ratio past the near plane.
    if (matrix[11] != 1.0f || matrix[15] != 0.0f || matrix[10] <= 1.0f) return;
    if (matrix[0] <= 0.0f || matrix[5] <= 0.0f) return;
    // `near`/`far` are windows.h macros, so the clip planes need local names.
    float clipNear = -matrix[14] / matrix[10];
    float clipFar = clipNear * matrix[10] / (matrix[10] - 1.0f);
    float verticalFov = 2 * std::atan(1 / matrix[5]) / 0.01745329252f, aspect = matrix[5] / matrix[0];
    if (!std::isfinite(clipNear) || !std::isfinite(clipFar) || !std::isfinite(verticalFov) || !std::isfinite(aspect)) return;
    if (clipNear <= 0 || clipFar <= clipNear || aspect <= 0) return;
    frameProjection[0] = verticalFov; frameProjection[1] = aspect;
    frameProjection[2] = clipNear; frameProjection[3] = clipFar;
    frameProjectionValid = true;
}

static InputDispatcher input(controller);
static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM key, LPARAM data) {
    // Each window forwards to its own native procedure, even while a replacement window is active.
    WNDPROC previous=windowInput.Previous(window);
    bool activeWindow=window==windowInput.Window();
    // Window lifecycle explains focus, resizing and orderly exits without logging text input.
    if (message == WM_ACTIVATEAPP || message == WM_SIZE || message == WM_CLOSE || message == WM_DESTROY)
        wc3::Log("window event=%u value=%u size=%ux%u", message, unsigned(key), LOWORD(data), HIWORD(data));
    // Rendering owns focus resource recovery; input owns message translation and key consumption.
    // The game tracks and draws its own cursor sprite from window messages. FPS pins the system
    // cursor to the client center once per frame, so between pins the real packet coordinates
    // roam with the physical mouse; forwarding those would make the sprite wander away from
    // where aiming and firing actually happen. Rewrite every FPS mouse-move packet to the
    // pinned center before the dispatcher decides whether to forward it.
    if (activeWindow && message==WM_MOUSEMOVE && healthy && controller.Visible()) {
        RECT client={};GetClientRect(window,&client);
        data=MAKELPARAM((client.right-client.left)/2,(client.bottom-client.top)/2);
    }
    if (activeWindow && message == WM_ACTIVATEAPP && key) overlay.RefreshAfterFocus();
    auto handled = activeWindow ? input.Handle(window, message, key, data, healthy) : std::optional<LRESULT>{};
    LRESULT result=handled ? *handled : previous ? CallWindowProcA(previous, window, message, key, data) : DefWindowProcA(window,message,key,data);
    // Forget destroyed HWNDs after native cleanup; reused handles must acquire a fresh procedure.
    if (message==WM_NCDESTROY) {windowInput.Destroyed(window);if (activeWindow) controller.ResetMouse();}
    return result;
}
static uintptr_t CurrentUI() {
    using GetUI = uintptr_t (__fastcall*)(int, int);
    return reinterpret_cast<GetUI>(gameBase + wc3::Rva(0x300710))(0, 0);
}
static void ObserveMap(uintptr_t ui) {
    if (mapUI == ui) return;
    // A real UI unload/replacement marks map boundaries; a rendering pause is only a focus pause.
    controller.ResetMap();overlay.MapChanged(); mapUI = ui;
    healthBars.Reset();healthBarView.active=false; // Native models/snapshots belong to the current map.
    if (ui) healthy = true;
    wc3::Log("Map lifecycle: %s ui=%08X", ui ? "loaded" : "unloaded", ui);
}
static BOOL WINAPI DeleteContextHook(HGLRC context) {
    BOOL result = originalDeleteContext(context);
    if (result) {
        // The overlay checks the canvas's context identity first; the GL canvas nulls its own
        // context after forgetting its fonts, so the overlay must run before that forget.
        overlay.ContextDeleted(context);
        glCanvas.ContextDeleted(context);
        minimap.ContextDeleted(context);
    }
    return result;
}
static void __cdecl InterfaceHook(BOOL show, float* duration) {
    // Only map-script requests yield FPS control; the visibility this runtime sets for its own
    // HUD is not a cinematic and must not suspend gameplay.
    if (!selfInterfaceRequest) {
        wc3::Trace("interface request show=%d duration=%.2f", show ? 1 : 0, duration ? *duration : -1.f);
        controller.InterfaceRequest(show != FALSE);
        // A map that re-shows the console while the FPS view is live is answered next frame, so
        // the cached hidden state never outlives the engine's real state.
        if (show != FALSE && interfaceHiddenForFps) interfaceReassert = true;
    }
    originalInterface(show, duration);
}
static void SafeTick(uintptr_t ui) {
    // Disable the extension on a native fault, leaving the game callback intact.
    __try {
        controller.Tick(ui);
        healthBars.Update(controller.ViewActor(),controller.Settings().healthBars);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        healthy = false; wc3::LogError("Controller fault %08X; disabled", GetExceptionCode());
        // A diagnostic/native failure must never keep FPS input or camera ownership locked over an exit dialog.
        __try { controller.RecoverFault(); }
        __except (EXCEPTION_EXECUTE_HANDLER) { wc3::LogError("Native view restoration failed; input remains released"); }
    }
}
// World-space overlays (health bars, native floating text) are drawn after the world pass from an
// immutable camera snapshot. Both frame anchors must publish it: without a snapshot the label view
// stays inactive and every bar is skipped, which is exactly how 1.27a lost all health bars.
static void UpdateLabelView() {
    WorldLabelView labelView;labelView.active=healthy && controller.Visible();
    if (labelView.active) {
        labelView.eye[0]=wc3::Real(wc3::GetCameraEyePositionX());
        labelView.eye[1]=wc3::Real(wc3::GetCameraEyePositionY());
        labelView.eye[2]=wc3::Real(wc3::GetCameraEyePositionZ());
        labelView.yaw=controller.ViewYaw();labelView.pitch=std::clamp(controller.ViewPitch(),-65.f,65.f);
        labelView.verticalFov=controller.ViewFov();
        RECT rect={};if (windowInput.Window() && GetClientRect(windowInput.Window(),&rect) && rect.bottom>0) labelView.aspect=float(rect.right)/rect.bottom;
        labelView.maximumDistance=controller.Settings().floatingTextDistance;
    }
    NativeFloatingText::SetView(labelView);
    healthBarView=labelView;healthBarView.maximumDistance=controller.Settings().healthBars.distance;
    // Bars must match the projection the world was actually rendered with, not the requested angle.
    healthBarView.verticalFov=healthVerticalFov;healthBarView.aspect=healthAspect;
}
static int __fastcall WorldHook(uintptr_t ui, uintptr_t unused) {
    DWORD now = GetTickCount();
    lastWorld = now;
    // RenderWorld's this pointer is not CGameUI; query the real UI for cinematic flags.
    uintptr_t gameUI = CurrentUI(); ObserveMap(gameUI);
    // Lift the native channel ceiling before the FPS camera writes its validated distance.
    cameraFarClip.Update(healthy && controller.Visible());
    if (healthy && gameUI) SafeTick(gameUI);
#ifdef WCS_FLOATING_TEXT_TEST
    if (healthy && gameUI) FloatingTextScene::Tick(controller,gameBase,root);
#endif
#ifdef WCS_STATUS_TEST
    // Real native spell casts are a separate opt-in oracle, excluded from ordinary builds.
    if (healthy && gameUI) StatusEffectScene::Tick(controller, gameBase, root);
#endif
#ifdef WCS_GAMEPLAY_TEST
    if (healthy && gameUI) RuneCombatScene::Tick(controller,gameBase,root);
#endif
#ifdef WCS_TREE_WHEEL_TEST
    // Dedicated tree/input oracles never run in ordinary release builds.
    if (healthy && gameUI) TreeAndWheelScene::Tick(controller,gameBase,root);
#endif
#ifdef WCS_SURFACE_TEST
    if (healthy && gameUI) WorldSurfaceScene::Tick(root);
#endif
#ifdef WCS_MOVEMENT_TEST
    // Native movement oracles are explicit and must run only in the developer's disposable map copy.
    if (healthy && gameUI) MovementScene::Tick(gameBase,root);
#endif
    // Keep layout evaluation running while hiding its final graphics after the world pass.
    nativeUIPhase = false;
    // Presentation adapters consume an immutable camera snapshot after the simulation tick.
    UpdateLabelView();
    // Suppress only the controlled actor during world drawing; menus, portraits and RTS still render it.
    ActorRenderFilter::Begin(healthy ? controller.ViewActor() : 0);
    int result = originalWorld(ui, unused);
    ActorRenderFilter::End();
    // Crop the native map after the world pass without altering Warcraft's UI layout.
    RECT client={};if (windowInput.Window()) GetClientRect(windowInput.Window(),&client);
    minimap.BeginFrame(gameUI,client.right,client.bottom,healthy && controller.Visible() && controller.MinimapVisible());
    nativeUIPhase = true;
    return result;
}
static void __cdecl PauseHook(BOOL value) {
    // Log real pause transitions rather than repeated map requests for the same state.
    static BOOL previous = FALSE;
    if (previous != value) { wc3::Log("native pause=%d", value); previous = value; }
    controller.SetPaused(value != FALSE); originalPause(value);
}
static void __fastcall UIHook(uintptr_t frame, uintptr_t unused) {
    // Suppress classic console/cinematic backing only during live FPS; menus keep their UI.
    using GetUI = uintptr_t (__fastcall*)(int, int);
    uintptr_t ui = reinterpret_cast<GetUI>(gameBase + wc3::Rva(0x300710))(0, 0);
    // UI drawing is filtered at GL submission, preserving layout and frame update callbacks.
    nativeUIPhase = ui && !*reinterpret_cast<int*>(ui + 0x258) && !*reinterpret_cast<int*>(ui + 0x260);
    originalUI(frame, unused);
}
// True when the batch is native 2D chrome the 1.27a FPS view must not draw: returns
// 1 for console-strip chrome (the caller feeds the native minimap capturer first) and
// 2 for the top bar (plain swallow - it is never map geometry). Mirrors NativeMinimap::
// MapBatch; the guarded walk keeps a hostile or malformed array from ever faulting.
static int ClassifyUiBatch(GLsizei count, GLenum type, const void* indices) {
    int strip = FullscreenView::ConsoleStrip();
    if (strip <= 0 || count <= 0 || count > 4096 || !indices) return 0;
    // The console is screen-fixed 2D UI: an orthographic projection. World batches render
    // perspective, so this cheap rejection keeps their pixels untouched.
    float projection[16] = {};
    glGetFloatv(GL_PROJECTION_MATRIX, projection);
    if (projection[11] != 0.0f || projection[15] != 1.0f) {
        // Perspective batches are world geometry. Their viewport is where the matrix that
        // rendered the visible frame can be read from, because the small 3D viewports
        // (portraits, minimap models) each load a projection of their own. The world
        // projection is issued once per frame, so the first world batch describes them all.
        if (!frameProjectionValid) {
            GLint worldViewport[4] = {};
            glGetIntegerv(GL_VIEWPORT, worldViewport);
            CaptureWorldProjection(projection, worldViewport);
        }
        return 0;
    }
    if (!glIsEnabled(GL_VERTEX_ARRAY)) return 0;
    GLint size = 0, stride = 0, vertexType = 0, viewport[4] = {};
    void* vertices = nullptr;
    glGetIntegerv(GL_VERTEX_ARRAY_SIZE, &size);
    glGetIntegerv(GL_VERTEX_ARRAY_STRIDE, &stride);
    glGetIntegerv(GL_VERTEX_ARRAY_TYPE, &vertexType);
    glGetPointerv(GL_VERTEX_ARRAY_POINTER, &vertices);
    if (!vertices || vertexType != GL_FLOAT || size < 2 || size > 4 || stride < 0) return false;
    if (!stride) stride = size * int(sizeof(float));
    glGetIntegerv(GL_VIEWPORT, viewport);
    float model[16] = {};
    glGetFloatv(GL_MODELVIEW_MATRIX, model);
    // The console skin's top border beam overlaps the world region above the strip, so the
    // window extends a scaled allowance past the viewport edge.
    float tolerance = std::max(12.0f, strip * 0.1f);
    float stripTop = float(strip) + tolerance;
    float left = float(viewport[0]) - tolerance, right = float(viewport[0] + viewport[2]) + tolerance;
    float minX = 0, maxX = 0, minY = 0, maxY = 0;
    __try {
        for (GLsizei i = 0; i < count; ++i) {
            unsigned index = type == GL_UNSIGNED_SHORT ? static_cast<const unsigned short*>(indices)[i] :
                type == GL_UNSIGNED_INT ? static_cast<const unsigned*>(indices)[i] :
                type == GL_UNSIGNED_BYTE ? static_cast<const unsigned char*>(indices)[i] : 0xFFFFFFFF;
            if (index > 100000) return 0;
            const float* vertex = reinterpret_cast<const float*>(static_cast<const char*>(vertices) + size_t(index) * stride);
            float position[4] = {vertex[0], vertex[1], size >= 3 ? vertex[2] : 0.0f, size == 4 ? vertex[3] : 1.0f};
            float eye[4] = {}, clip[4] = {};
            for (int row = 0; row < 4; ++row) for (int col = 0; col < 4; ++col) eye[row] += model[col*4+row] * position[col];
            for (int row = 0; row < 4; ++row) for (int col = 0; col < 4; ++col) clip[row] += projection[col*4+row] * eye[col];
            if (!std::isfinite(clip[3]) || std::abs(clip[3]) < .000001f) return 0;
            float x = viewport[0] + (clip[0]/clip[3] + 1) * viewport[2] * .5f;
            float y = viewport[1] + (clip[1]/clip[3] + 1) * viewport[3] * .5f;
            if (!std::isfinite(x) || !std::isfinite(y)) return 0;
            if (!i) { minX = maxX = x; minY = maxY = y; }
            else { minX = std::min(minX,x); maxX = std::max(maxX,x); minY = std::min(minY,y); maxY = std::max(maxY,y); }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    // The native top bar (resource numbers, menu and day/night icons) is screen-fixed chrome
    // above the world viewport; 1.26a hides it with the whole UI phase. Its quads hug the
    // screen top and overflow past the top edge (GL clips them), while tall panels such as
    // quests and multiboards keep their bottom edge far below this band and stay visible.
    // The chrome scales with the frame, so the band is a fraction of the frame height instead
    // of a fixed pixel count. Measured deepest edge and top overflow: 73 px / 12 px at
    // 1064x593 windowed and 111 px / 19 px at 1440x900 fullscreen, both exactly proportional
    // (900/593), so one fraction covers every resolution the client can pick.
    float top = float(viewport[1] + viewport[3]);
    float bandDown = top * (80.0f / 593.0f), bandUp = top * (18.0f / 593.0f);
    if (minX >= left && maxX <= right && minY >= top - bandDown && maxY <= top + bandUp) return 2;
    // Console chrome never leaves its strip; anything reaching the world or off-screen
    // keeps drawing so partial overlaps can never clip live geometry.
    bool tiles = maxY <= stripTop && minY >= -tolerance && minX >= left && maxX <= right;
    // The console skin's top border beam is a short row of quads whose vertices reach above
    // the strip (46px at 593p). Small ortho quads that stay inside that low band are console
    // chrome too; chat and system notices share it, which the anchored path also hides.
    bool border = count <= 16 && minY >= -tolerance && maxY <= strip * 1.6f;
    return tiles || border ? 1 : 0;
}
static void APIENTRY DrawElementsHook(GLenum mode, GLsizei count, GLenum type, const void* indices) {
    // Warcraft batches the console after world rendering, including cinematic backing textures.
    if (healthy && nativeUIPhase && !overlayPass && controller.Visible() && GetTickCount() - lastWorld < 500) {
        minimap.Submit(mode,count,type,indices,originalDrawElements);return;
    }
    // Builds without a verified world/UI anchor (1.27a) never raise nativeUIPhase, so the
    // console cannot be dropped by phase. Drop it by geometry instead: strip batches still
    // feed the native minimap capturer (mirroring the anchor path), while the top bar is
    // never map geometry and is swallowed without touching the capture state.
    if (healthy && !overlayPass && !originalWorld && controller.Visible() && GetTickCount() - lastWorld < 500) {
        int batch = ClassifyUiBatch(count, type, indices);
        if (batch == 1) { minimap.Submit(mode,count,type,indices,originalDrawElements);return; }
        if (batch == 2) {
            // Swallow by rasterization, not by skipping: third-party draw wrappers rewrite
            // these very batches and desync when calls vanish, so let the whole call chain
            // proceed and clip it to an empty rectangle instead. No pixel can survive a
            // zero-area scissor, and driver state stays exactly as an unfiltered frame.
            glPushAttrib(GL_SCISSOR_BIT);
            glEnable(GL_SCISSOR_TEST);
            glScissor(0, 0, 0, 0);
            originalDrawElements(mode, count, type, indices);
            glPopAttrib();
            return;
        }
    }
    originalDrawElements(mode, count, type, indices);
}
static void __fastcall PerspectiveHook(uintptr_t output, uintptr_t unused, float fov, float aspect, float nearZ, float farZ) {
    bool fps=healthy && controller.Visible() && GetTickCount()-lastWorld<500;
    // Native limits/interpolation can differ from the requested INI value. Scope near clipping by
    // the actual world-camera field so the fallback also keeps nearby ground when a limit cannot be lifted.
    float cameraFar=fps ? wc3::Real(wc3::GetCameraField(1)) : farZ;
    float originalNear=nearZ;
    nearZ=FpsProjection::NearPlane(fps,nearZ,farZ,cameraFar);
    if(fps && std::abs(farZ-cameraFar)<.01f) {
        // Actual world projection keeps bar depth/FOV valid during scope and native distance clamping.
        healthNear=nearZ;healthFar=farZ;
    }
    static DWORD projectionLog=0;
    if(fps && std::abs(farZ-cameraFar)<.01f && DiagnosticLog::Due(projectionLog))
        wc3::Trace("world projection requestedFar=%.1f cameraFar=%.1f projectionFar=%.1f near=%.1f->%.1f",
            controller.Settings().render.drawDistance,cameraFar,farZ,originalNear,nearZ);
    originalPerspective(output, unused, fov, aspect, nearZ, farZ);
    if(fps && std::abs(farZ-cameraFar)<.01f) {
        // Warcraft derives vertical FOV from its diagonal field. Read actual matrix scale, not the INI angle.
        const float* matrix=reinterpret_cast<const float*>(output);
        if(std::isfinite(matrix[0]) && std::isfinite(matrix[5]) && matrix[0]>0 && matrix[5]>0) {
            healthVerticalFov=2*std::atan(1/matrix[5])/.01745329252f;healthAspect=matrix[5]/matrix[0];
            healthBarView.verticalFov=healthVerticalFov;healthBarView.aspect=healthAspect;
        }
    }
}
// Present is the frame anchor on builds with no verified world-render entry point. The engine
// reaches present through its import entry, while the opengl32 export stays reachable as a
// secondary path; both can deliver the same frame, so the first arrival in a frame owns the
// tick and the simulation never advances twice for one present.
static BOOL ForwardPresent(HDC dc, UINT planes) {
    static __declspec(thread) int depth = 0;
    ++depth;
    BOOL result = depth == 1 && swapForward ? swapForward(dc, planes) : originalSwap(dc, planes);
    --depth;
    return result;
}
// Reports whether the claimed import entry still holds our detour, so a lost anchor is visible
// in the frame trace instead of only as a stalled counter.
static bool PresentAnchorHeld();
static void PresentFrame(HDC dc) {
    static LARGE_INTEGER frequency = {};
    static LONGLONG arrival = 0;
    if (!frequency.QuadPart) QueryPerformanceFrequency(&frequency);
    LARGE_INTEGER stamp = {};
    QueryPerformanceCounter(&stamp);
    if (arrival && stamp.QuadPart - arrival < frequency.QuadPart / 2000) return;
    arrival = stamp.QuadPart;
    // F8's validated snapshot drives the client cap; gameplay continues to use elapsed real time.
    frameRate.Configure(controller.Settings().render.maxFPS);
    frameRate.Pace();
    static DWORD previous = 0;
    static unsigned frames = 0;
    ++frames;
    ObserveMap(CurrentUI());
    // Builds with no verified world-render entry point (1.27a) advance the controller from
    // here instead. WorldHook owns that tick otherwise, and running both would double-step
    // the simulation, so this path is exclusive to a failed world hook.
    if (!originalWorld) {
        lastWorld = GetTickCount();
        if (healthy) {
            uintptr_t fallbackUI = CurrentUI();
            if (fallbackUI) SafeTick(fallbackUI);
            // The anchored path feeds the minimap capturer from WorldHook; the swap-anchored
            // path starts it here so console-strip submissions can be captured the same way.
            RECT client = {};
            if (windowInput.Window() && GetClientRect(windowInput.Window(), &client) && client.right > 0 && client.bottom > 0)
                minimap.BeginFrame(fallbackUI, client.right, client.bottom,
                    healthy && controller.Visible() && controller.MinimapVisible());
        }
        // The frame being presented already submitted its world projection; publish it and
        // rebuild the camera snapshot the world hook owns on anchored builds.
        ApplyCapturedProjection();
        UpdateLabelView();
    }
    // Menus, cinematics, faults and F6 release the camera limit even without another world batch.
    cameraFarClip.Update(healthy && controller.Visible());
    // Keep the native console in step with the live FPS view wherever GL submission cannot do it.
    SyncInterface(healthy);
    // 1.27a issues the world viewport once and never re-issues it per frame, so the last RTS-state
    // rect (above the console) would confine every FPS frame and leave the console band holding a
    // frozen copy of the first FPS frame. Re-assert the full client rect each present; the next
    // frame's world pass inherits it. RTS frames re-issue their own rects, so this stays FPS-only.
    if (healthy && controller.Visible() && windowInput.Window()) {
        RECT client = {};
        if (GetClientRect(windowInput.Window(), &client) && client.right > 0 && client.bottom > 0) {
            if (originalViewport) originalViewport(0, 0, client.right, client.bottom);
            if (originalScissor) originalScissor(0, 0, client.right, client.bottom);
        }
    }
    HWND window = WindowFromDC(dc);
    // Recheck the real render window after films: Warcraft may replace it or reset its input procedure.
    if (windowInput.Observe(window)) {
        controller.AttachWindow(window);
        overlay.RefreshAfterFocus();
    }
    overlayPass = true;
    // Restore captured minimap pixels before the overlay reads world depth for health bars.
    // FinishFrame no-ops unless a submission captured something this frame.
    minimap.FinishFrame();
    RECT client = {};
    if (window) GetClientRect(window, &client);
    if (healthy && GetTickCount() - lastWorld < 500)
        overlay.Draw(glCanvas, dc, window, client.right, client.bottom, controller, minimap, healthBars.Items(), healthBarView, healthNear, healthFar);
    // The reticle cannot reach its draw entry while native recovery or stale world frames suppress the overlay.
    else ReticleDiagnostics::Skip(healthy ? "world-frame-stale" : "controller-fault", 0, 0, GetTickCount() - lastWorld);
    overlayPass = false; nativeUIPhase = false;
    // Observe skipped frames too: a valid context alone does not establish an active FPS overlay.
    if (DiagnosticLog::Due(previous)) {
        RECT diagClient = {}; if (window) GetClientRect(window, &diagClient);
        wc3::Trace("render frames=%u healthy=%d fps=%d worldAgeMs=%lu context=%p focus=%d window=%ldx%ld buying=%d scope=%d anchor=%d",
            frames, healthy, controller.Visible(), GetTickCount() - lastWorld, wglGetCurrentContext(),
            window == GetForegroundWindow(), diagClient.right, diagClient.bottom, controller.Buying(), controller.Scoped(),
            PresentAnchorHeld() ? 1 : 0);
        wc3::Trace("gl stats/sec vp=%ld sc=%ld push=%ld pop=%ld lastVp=%d,%d %dx%d lastSc=%d,%d %dx%d",
            glStatVp, glStatSc, glStatPush, glStatPop,
            glStatLastVp[0], glStatLastVp[1], glStatLastVp[2], glStatLastVp[3],
            glStatLastSc[0], glStatLastSc[1], glStatLastSc[2], glStatLastSc[3]);
        glStatVp = glStatSc = glStatPush = glStatPop = 0;
        frames = 0;
    }
}
// Interposer stubs sit between the engine and the driver, so calling the displaced target
// preserves them. Re-entering our own detour from there falls back to the trampoline once,
// which keeps the swap exactly one driver call deep.
static BOOL WINAPI SwapLayerHook(HDC dc, UINT planes) {
    PresentFrame(dc);
    BOOL result = ForwardPresent(dc, planes);
    if (!result) wc3::LogError("SwapLayerBuffers failed win32=%lu", GetLastError());
    return result;
}
// Direct3D 9 Present is the frame anchor for Direct3D renders. Simulation pacing and the
// swap-anchored tick mirror PresentFrame; GL-only work (minimap capture, viewport re-assert)
// has no D3D equivalent. The native console is hidden through the engine's interface call on
// every build because no GL submission pass exists where 1.26a drops its batches.
static void D3DPresentFrame(Canvas& canvas, HWND window, int width, int height) {
    // F8's validated snapshot drives the client cap; gameplay continues to use elapsed real time.
    frameRate.Configure(controller.Settings().render.maxFPS);
    frameRate.Pace();
    static DWORD previous = 0;
    static unsigned frames = 0;
    ++frames;
    ObserveMap(CurrentUI());
    // Builds with no verified world-render entry point (1.27a) advance the controller from the
    // present anchor; WorldHook owns that tick otherwise, so this path stays exclusive.
    if (!originalWorld) {
        lastWorld = GetTickCount();
        if (healthy) {
            uintptr_t fallbackUI = CurrentUI();
            if (fallbackUI) SafeTick(fallbackUI);
        }
        ApplyCapturedProjection();
        UpdateLabelView();
    }
    cameraFarClip.Update(healthy && controller.Visible());
    SyncInterface(healthy, true);
    if (windowInput.Observe(window)) {
        controller.AttachWindow(window);
        overlay.RefreshAfterFocus();
    }
    overlayPass = true;
    // Diagnosis escape hatch: WCS_SKIP_OVERLAY=1 shows the raw game frame from the FPS camera,
    // separating the game's own ground-level rendering from everything the overlay draws.
    static bool skipOverlayDiag = GetEnvironmentVariableA("WCS_SKIP_OVERLAY", nullptr, 0) != 0;
    // Direct3D has no GL submission pass to drop native console batches at, so the classic
    // strip and top bar are clipped from the scissor side while the FPS view is live. The
    // band fractions mirror the OpenGL filter: top chrome hugs the frame top (80/593) and the
    // console strip plus its border beam occupies the bottom (128 + 50)/593; when no GL
    // viewport ever observed the strip, the measured GL value stands in.
    bool hideBands = healthy && controller.Visible() && GetTickCount() - lastWorld < 500 && !skipOverlayDiag;
    int strip = FullscreenView::ConsoleStrip();
    if (strip <= 0) strip = height * 128 / 593;
    int beam = height * 50 / 593;
    RECT worldRegion = {0, height * 80 / 593, width, height - strip - beam};
    D3DHooks::SetUiSuppression(hideBands, worldRegion);
    if (healthy && GetTickCount() - lastWorld < 500 && !skipOverlayDiag)
        overlay.Draw(canvas, nullptr, window, width, height, controller, minimap, healthBars.Items(), healthBarView, healthNear, healthFar);
    else if (!skipOverlayDiag) ReticleDiagnostics::Skip(healthy ? "world-frame-stale" : "controller-fault", width, height, GetTickCount() - lastWorld);
    overlayPass = false; nativeUIPhase = false;
    if (DiagnosticLog::Due(previous)) {
        wc3::Trace("d3d frames=%u healthy=%d fps=%d worldAgeMs=%lu backend=%s focus=%d window=%ldx%ld buying=%d scope=%d anchor=%d",
            frames, healthy, controller.Visible(), GetTickCount() - lastWorld, canvas.Name(),
            window == GetForegroundWindow(), long(width), long(height), controller.Buying(), controller.Scoped(),
            PresentAnchorHeld() ? 1 : 0);
        frames = 0;
    }
}
static bool Hook(void* target, void* callback, void** original) {
    MH_STATUS create = MH_CreateHook(target, callback, original);
    MH_STATUS enable = create == MH_OK ? MH_EnableHook(target) : create;
    wc3::Log("hook target=%08X create=%d enable=%d", target, create, enable);
    return create == MH_OK && enable == MH_OK;
}
// A detour behind the opengl32 export only survives while that export is what the engine calls.
// Overlay and vendor present interposers rewrite the caller's import entry as soon as the first
// frame is presented, which leaves the engine calling their stub and our detour unreached - the
// hook installs cleanly and then never fires. Claiming the entry itself keeps the frame anchor
// in place no matter which engines are already active, and the guard restores it if one of them
// reinstates its own stub later.
static struct {
    void** slot = nullptr;
    void* displaced = nullptr;
} presentImport;
static bool PresentAnchorHeld() {
    return presentImport.slot && *presentImport.slot == reinterpret_cast<void*>(SwapLayerHook);
}
// The 1.26a path drops native console batches at GL submission, which needs the world-render or
// UI-draw entry point. A build that has neither cannot mark the UI pass, so the live FPS view
// asks the engine itself to hide the console - the same call cinematics use - and restores it
// when the view ends. Direct3D renders have no GL submission pass at all, so their frames hide
// the console through this same call on every build (allow=true). Only the visibility this
// runtime set for its own HUD is claimed.
static void SyncInterface(bool visible, bool allow) {
    if ((!allow && originalWorld) || !wc3::ShowInterface) return;
    bool hidden = visible && controller.Visible();
    // A re-assert repeats the hide even though the requested state did not change.
    if (hidden == interfaceHiddenForFps && !(hidden && interfaceReassert)) return;
    interfaceReassert = false;
    interfaceHiddenForFps = hidden;
    // A zero-length interface fade never reaches its completed state, so the console would stay
    // on screen; the shortest fade the engine advances is used instead.
    float fade = 0.1f;
    selfInterfaceRequest = true;
    wc3::ShowInterface(hidden ? FALSE : TRUE, &fade);
    selfInterfaceRequest = false;
    wc3::Log("FPS native interface %s", hidden ? "hidden" : "restored");
}
static bool FindImportSlot(HMODULE module, const char* symbol, void** slot) {
    auto dos = reinterpret_cast<PIMAGE_DOS_HEADER>(module);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    auto nt = reinterpret_cast<PIMAGE_NT_HEADERS>(reinterpret_cast<uint8_t*>(module) + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
    DWORD directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    if (!directory) return false;
    auto descriptor = reinterpret_cast<PIMAGE_IMPORT_DESCRIPTOR>(reinterpret_cast<uint8_t*>(module) + directory);
    for (; descriptor->Name; ++descriptor) {
        // The original thunk array carries names; the first thunk array is the live import table.
        if (!descriptor->OriginalFirstThunk) continue;
        auto names = reinterpret_cast<PIMAGE_THUNK_DATA>(reinterpret_cast<uint8_t*>(module) + descriptor->OriginalFirstThunk);
        auto entries = reinterpret_cast<PIMAGE_THUNK_DATA>(reinterpret_cast<uint8_t*>(module) + descriptor->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++entries) {
            if (names->u1.Ordinal & IMAGE_ORDINAL_FLAG) continue;
            auto named = reinterpret_cast<PIMAGE_IMPORT_BY_NAME>(reinterpret_cast<uint8_t*>(module) + names->u1.AddressOfData);
            if (strcmp(reinterpret_cast<const char*>(named->Name), symbol)) continue;
            *slot = reinterpret_cast<void**>(&entries->u1.Function);
            return true;
        }
    }
    return false;
}
static bool ClaimPresentImport(HMODULE module, const char* symbol, void* detour) {
    void* slot = nullptr;
    if (!FindImportSlot(module, symbol, &slot) || !slot) return false;
    presentImport.slot = reinterpret_cast<void**>(slot);
    presentImport.displaced = *presentImport.slot;
    DWORD previous = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &previous)) return false;
    *presentImport.slot = detour;
    VirtualProtect(slot, sizeof(void*), previous, &previous);
    return true;
}
static DWORD WINAPI PresentGuard(void*) {
    for (;;) {
        Sleep(1000);
        if (!presentImport.slot) return 0;
        void* current = *presentImport.slot;
        if (current == reinterpret_cast<void*>(SwapLayerHook)) continue;
        DWORD previous = 0;
        if (!VirtualProtect(presentImport.slot, sizeof(void*), PAGE_READWRITE, &previous)) continue;
        *presentImport.slot = reinterpret_cast<void*>(SwapLayerHook);
        VirtualProtect(presentImport.slot, sizeof(void*), previous, &previous);
        wc3::Log("present import reclaimed value=%p; a present interposer rewrote it", current);
    }
}
static DWORD WINAPI Initialize(void*) {
    HMODULE game = nullptr;
    for (int i = 0; i < 100 && !game; ++i) { game = GetModuleHandleA("Game.dll"); if (!game) Sleep(100); }
    // Load retention before opening so custom archive limits apply to the previous session too.
    DiagnosticLog::Configure(GameplaySettings::Load(std::string(root) + "\\WarcraftCS.ini").logging);
    wc3::OpenLog(root);
    SYSTEM_INFO system = {}; GetNativeSystemInfo(&system);
    wc3::Log("Session start version=%s source=%s compiled=%s %s nativeArch=%u pid=%lu",
        WCS_BUILD_VERSION, WCS_BUILD_SOURCE, __DATE__, __TIME__, system.wProcessorArchitecture, GetCurrentProcessId());
    using GetVersion = LONG (WINAPI*)(OSVERSIONINFOW*);
    auto getVersion = reinterpret_cast<GetVersion>(GetProcAddress(GetModuleHandleA("ntdll.dll"), "RtlGetVersion"));
    OSVERSIONINFOW version = {}; version.dwOSVersionInfoSize = sizeof(version);
    if (getVersion && getVersion(&version) == 0)
        wc3::Log("Windows kernel=%lu.%lu build=%lu", version.dwMajorVersion, version.dwMinorVersion, version.dwBuildNumber);
    if (!game) { wc3::LogError("Game.dll not loaded after startup wait"); return 0; }
    if (!wc3::Bind(game)) { wc3::LogError("Native API bind failed; no hooks installed"); return 0; }
    // Direct RoC launches must also opt out of Windows bitmap scaling before Warcraft creates its window.
    // Launcher startup supplies HIGHDPIAWARE; leave an already-created window's awareness unchanged.
    if (!IsProcessDPIAware() && !FindWindowA("Warcraft III", nullptr))
        wc3::Log("Early DPI awareness enabled=%d", SetProcessDPIAware());
    uintptr_t base = gameBase = reinterpret_cast<uintptr_t>(game);
    controller.Configure(root, base); overlay.Configure(root);
    frameRate.Install(base);
    // The early install thread initializes MinHook first so it can claim Create9 during startup;
    // that is not an error here.
    MH_STATUS hookStatus = MH_Initialize();
    if (hookStatus != MH_OK && hookStatus != MH_ERROR_ALREADY_INITIALIZED) { wc3::Log("MinHook initialization failed"); return 0; }
    // The world render callback is the frame anchor on 1.26a. Where this build has no
    // verified address for it the native prologue check cannot pass, so the swap hook
    // takes over the per-frame tick and every game hook that depends on the world pass
    // disables itself through its own signature check.
    const unsigned char expected[] = {0x53,0x56,0x8B,0xF1,0x8B,0x8E,0x38,0x03,0x00,0x00};
    uintptr_t worldRender = wc3::Rva(0x395900);
    if (!worldRender || memcmp(reinterpret_cast<void*>(base + worldRender), expected, sizeof(expected))) {
        wc3::Log("World render callback unavailable on %s; per-frame tick moves to the swap hook", wc3::ActiveBuildName());
    } else if (!Hook(reinterpret_cast<void*>(base + worldRender), reinterpret_cast<void*>(WorldHook), reinterpret_cast<void**>(&originalWorld))) {
        wc3::LogError("World render hook failed; no hooks installed"); return 0;
    }
    // Map timers may request RTS camera presets repeatedly; live FPS owns those native camera writes.
    MapCameraGuard::Install(base, []() { return healthy && controller.BlocksMapCamera(); }, Hook);
    ActorRenderFilter::Install(base, Hook);
    // Scoped base-damage replacement leaves all ordinary RTS attacks untouched.
    NativeWeaponAttack::Install(base,Hook);
    NativeFloatingText::Install(base, Hook);
    FpsCombatGuard::Install(base, []() { return healthy ? controller.ViewActor() : 0; },
        [](uintptr_t unit) { return healthy && controller.BlocksPassiveUnit(unit); },
        [](uintptr_t attack) { return healthy && controller.BlocksPassiveAttack(attack); }, Hook);
    NativeMovementGuard::Install(base,[]() { return healthy ? controller.ViewActor() : 0; },
        []() { return healthy && controller.NativeCastMovement(); },Hook);
    Hook(reinterpret_cast<void*>(base + wc3::Rva(0x3BC4D0)), reinterpret_cast<void*>(PauseHook), reinterpret_cast<void**>(&originalPause));
    Hook(reinterpret_cast<void*>(wc3::ShowInterface), reinterpret_cast<void*>(InterfaceHook), reinterpret_cast<void**>(&originalInterface));
    const unsigned char uiSignature[] = {0x83,0xEC,0x10,0x53,0x55,0x56,0x57,0x8B,0xF9};
    uintptr_t uiDraw = wc3::Rva(0x60C580);
    if (uiDraw && !memcmp(reinterpret_cast<void*>(base + uiDraw), uiSignature, sizeof(uiSignature)))
        Hook(reinterpret_cast<void*>(base + uiDraw), reinterpret_cast<void*>(UIHook), reinterpret_cast<void**>(&originalUI));
    else wc3::Log("UI signature mismatch; original UI retained");
    const unsigned char perspectiveSignature[] = {0x51, 0xD9, 0x44, 0x24, 0x0C};
    uintptr_t projection = wc3::Rva(0x7B66F0);
    if (projection && !memcmp(reinterpret_cast<void*>(base + projection), perspectiveSignature, sizeof(perspectiveSignature)))
        Hook(reinterpret_cast<void*>(base + projection), reinterpret_cast<void*>(PerspectiveHook), reinterpret_cast<void**>(&originalPerspective));
    else wc3::Log("Perspective signature mismatch; original near plane retained");
    // The verified Game.dll import uses the two-argument layer-buffer API.
    HMODULE gl = GetModuleHandleA("opengl32.dll");
    // Module bases make any crash address attributable in the log alone.
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&DrawElementsHook), &self);
    wc3::Log("modules self=%p ijl15=%p game=%p helper=%p d3d9=%p",
        self, GetModuleHandleA("ijl15.dll"), GetModuleHandleA("game.dll"),
        GetModuleHandleA("WarcraftHelper.dll"), GetModuleHandleA("d3d9.dll"));
    void* swapExport = reinterpret_cast<void*>(GetProcAddress(gl, "wglSwapLayerBuffers"));
    Hook(reinterpret_cast<void*>(GetProcAddress(gl, "glViewport")), reinterpret_cast<void*>(ViewportHook), reinterpret_cast<void**>(&originalViewport));
    Hook(reinterpret_cast<void*>(GetProcAddress(gl, "glScissor")), reinterpret_cast<void*>(ScissorHook), reinterpret_cast<void**>(&originalScissor));
    Hook(reinterpret_cast<void*>(GetProcAddress(gl, "glPushAttrib")), reinterpret_cast<void*>(PushAttribHook), reinterpret_cast<void**>(&originalPushAttrib));
    Hook(reinterpret_cast<void*>(GetProcAddress(gl, "glPopAttrib")), reinterpret_cast<void*>(PopAttribHook), reinterpret_cast<void**>(&originalPopAttrib));
    Hook(reinterpret_cast<void*>(GetProcAddress(gl, "wglDeleteContext")), reinterpret_cast<void*>(DeleteContextHook), reinterpret_cast<void**>(&originalDeleteContext));
    Hook(reinterpret_cast<void*>(GetProcAddress(gl, "glDrawElements")), reinterpret_cast<void*>(DrawElementsHook), reinterpret_cast<void**>(&originalDrawElements));
    Hook(swapExport, reinterpret_cast<void*>(SwapLayerHook), reinterpret_cast<void**>(&originalSwap));
    // Input attach must not depend on the first frame. These pump hooks run on the window's own
    // thread from the very first message, whether the window predates this module or not.
    HMODULE user32 = GetModuleHandleA("user32.dll");
    Hook(reinterpret_cast<void*>(GetProcAddress(user32, "GetMessageW")),
        reinterpret_cast<void*>(GetMessageWHook), reinterpret_cast<void**>(&originalGetMessageW));
    Hook(reinterpret_cast<void*>(GetProcAddress(user32, "GetMessageA")),
        reinterpret_cast<void*>(GetMessageAHook), reinterpret_cast<void**>(&originalGetMessageA));
    Hook(reinterpret_cast<void*>(GetProcAddress(user32, "PeekMessageW")),
        reinterpret_cast<void*>(PeekMessageWHook), reinterpret_cast<void**>(&originalPeekMessageW));
    Hook(reinterpret_cast<void*>(GetProcAddress(user32, "PeekMessageA")),
        reinterpret_cast<void*>(PeekMessageAHook), reinterpret_cast<void**>(&originalPeekMessageA));
    // The engine presents through its own import entry, so claim that entry as the authoritative
    // anchor instead of relying on the export prologue surviving every other in-process hook.
    bool anchor = ClaimPresentImport(game, "wglSwapLayerBuffers", reinterpret_cast<void*>(SwapLayerHook));
    if (!anchor) anchor = ClaimPresentImport(GetModuleHandleA(nullptr), "wglSwapLayerBuffers", reinterpret_cast<void*>(SwapLayerHook));
    if (anchor) {
        // A displaced value that is neither the export nor ourselves belongs to an interposer;
        // forwards through it so its own overlay keeps working.
        if (presentImport.displaced != swapExport && presentImport.displaced != reinterpret_cast<void*>(SwapLayerHook))
            swapForward = reinterpret_cast<Swap>(presentImport.displaced);
        wc3::Log("present anchor slot=%p displaced=%p export=%p interposed=%d",
            presentImport.slot, presentImport.displaced, swapExport, swapForward ? 1 : 0);
        HANDLE guard = CreateThread(nullptr, 0, PresentGuard, nullptr, 0, nullptr);
        if (guard) CloseHandle(guard);
    } else {
        wc3::LogError("present import wglSwapLayerBuffers not found; frame anchor unavailable");
    }
    // Route the HUD into Direct3D 9 when the game renders through D3D. The hook stays inert
    // under the OpenGL renderer; when the game uses D3D, its device Present becomes the frame
    // anchor above (D3DPresentFrame) and the GL present import simply never fires.
    D3DHooks::Install([](Canvas& canvas, HWND window, int width, int height) {
        D3DPresentFrame(canvas, window, width, height);
    }, CaptureD3DProjection);
    wc3::Log("WarcraftCS initialized. Offline only. Select an owned unit and press F6.");
    return 0;
}
BOOL PluginRuntime::Attach(HINSTANCE module) {
        DisableThreadLibraryCalls(module); GetModuleFileNameA(module, root, MAX_PATH);
        // Miles probes then unloads .mix providers; hooks require the module to stay resident.
        HMODULE pinned = nullptr;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCSTR>(PluginRuntime::Attach), &pinned);
        char* slash = strrchr(root, '\\'); if (slash) *slash = 0;
        strcat_s(root, "\\WarcraftCS");
        // Claim the Direct3DCreate9 exports from here, not from Initialize: the engine builds its
        // device inside its first second, while Initialize is still binding and hooking the engine,
        // and an exclusive-fullscreen device can never be re-created for a probe afterwards.
        D3DHooks::StartEarlyInstall();
        // Defer engine work until after the loader lock has been released.
        HANDLE thread = CreateThread(nullptr, 0, Initialize, nullptr, 0, nullptr);
        if (thread) CloseHandle(thread);
    return TRUE;
}
