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
#include <intrin.h>
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
// Builds with no world/UI anchor have nothing that marks the native UI pass, so the console
// cannot be dropped at GL submission the way the 1.26a path drops it. The engine's own
// interface call - the one cinematics use - hides it instead, and the FPS HUD draws its own
// replacement chrome. A request this runtime makes for its own HUD is not a map cinematic.
static bool interfaceHiddenForFps = false;
static bool interfaceReassert = false;
static bool selfInterfaceRequest = false;
static void SyncInterface(bool visible);
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
// 1.27a console draw-path probe (temporary). Builds without a world anchor have
// nothing that marks the UI phase, so identify instead which GL entries and
// which Game.dll call sites produce the console band: keep the last GL
// submissions before present in a ring and dump it (run-length encoded) at a
// bounded cadence. Remove once the console entry point is verified.
// ---------------------------------------------------------------------------
enum ProbeFunc {
    ProbeGlBegin = 0, ProbeGlVertex2f, ProbeGlTexCoord2f, ProbeGlColor4f, ProbeGlColor4ub,
    ProbeGlRectf, ProbeGlDrawArrays, ProbeGlDrawRangeElements, ProbeGlDrawElements,
    ProbeGlCallList, ProbeGlCallLists, ProbeGlBitmap, ProbeGlViewport, ProbeGlScissor,
    ProbeGlPushAttrib, ProbeGlPopAttrib, ProbeFuncCount
};
static const char* const probeNames[ProbeFuncCount] = {
    "glBegin", "glVertex2f", "glTexCoord2f", "glColor4f", "glColor4ub", "glRectf",
    "glDrawArrays", "glDrawRangeElements", "glDrawElements", "glCallList", "glCallLists",
    "glBitmap", "glViewport", "glScissor", "glPushAttrib", "glPopAttrib"
};
struct ProbeEntry { unsigned char func; unsigned rva, a, b; };
static ProbeEntry probeRing[256];
static volatile long probeHead = 0;
static long probeFrame = 0;
static long probeCounts[ProbeFuncCount] = {};
static unsigned ProbeRva(uintptr_t caller) {
    return caller >= gameBase && caller < gameBase + 0x1000000 ? unsigned(caller - gameBase) : 0xFFFFFFFFu;
}
static void ProbeSubmit(int func, unsigned a, unsigned b, uintptr_t caller) {
    if (overlayPass) return;
    long slot = InterlockedIncrement(&probeHead) - 1;
    ProbeEntry& entry = probeRing[slot % 256];
    entry.func = (unsigned char)func; entry.rva = ProbeRva(caller); entry.a = a; entry.b = b;
    ++probeCounts[func];
}
static unsigned FloatBits(float value) { unsigned bits; memcpy(&bits, &value, 4); return bits; }

// Per-second GL state statistics: proves which call path re-issues (or restores) the world
// viewport between our present-time force and the next world pass.
static volatile long glStatVp = 0, glStatSc = 0, glStatPush = 0, glStatPop = 0;
static GLint glStatLastVp[4] = {}, glStatLastSc[4] = {};
static void APIENTRY ViewportHook(GLint x, GLint y, GLsizei width, GLsizei height) {
    RECT rect={};
    static GLint seen[24*4]; static unsigned seenCount = 0;
    ProbeSubmit(ProbeGlViewport, unsigned(x & 0xFFFF) | (unsigned(y & 0xFFFF) << 16),
        unsigned(width & 0xFFFF) | (unsigned(height & 0xFFFF) << 16),
        reinterpret_cast<uintptr_t>(_ReturnAddress()));
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
    ProbeSubmit(ProbeGlScissor, unsigned(x & 0xFFFF) | (unsigned(y & 0xFFFF) << 16),
        unsigned(width & 0xFFFF) | (unsigned(height & 0xFFFF) << 16),
        reinterpret_cast<uintptr_t>(_ReturnAddress()));
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
    ProbeSubmit(ProbeGlPushAttrib, mask, 0, reinterpret_cast<uintptr_t>(_ReturnAddress()));
    InterlockedIncrement(&glStatPush);
    originalPushAttrib(mask);
}
static void APIENTRY PopAttribHook(void) {
    ProbeSubmit(ProbeGlPopAttrib, 0, 0, reinterpret_cast<uintptr_t>(_ReturnAddress()));
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

static InputDispatcher input(controller);
static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM key, LPARAM data) {
    // Each window forwards to its own native procedure, even while a replacement window is active.
    WNDPROC previous=windowInput.Previous(window);
    bool activeWindow=window==windowInput.Window();
    // Window lifecycle explains focus, resizing and orderly exits without logging text input.
    if (message == WM_ACTIVATEAPP || message == WM_SIZE || message == WM_CLOSE || message == WM_DESTROY)
        wc3::Log("window event=%u value=%u size=%ux%u", message, unsigned(key), LOWORD(data), HIWORD(data));
    // Rendering owns focus resource recovery; input owns message translation and key consumption.
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
    if (result) {overlay.ContextDeleted(context);minimap.ContextDeleted(context);}
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
    healthBarView.verticalFov=healthVerticalFov;healthBarView.aspect=healthAspect;
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
// True when every submitted vertex projects into the console strip below the engine's own
// world viewport. Reads the client vertex array directly, mirroring NativeMinimap::MapBatch;
// the guarded walk keeps a hostile or malformed array from ever faulting the host.
static bool BatchInConsoleStrip(GLsizei count, GLenum type, const void* indices) {
    int strip = FullscreenView::ConsoleStrip();
    if (strip <= 0 || count <= 0 || count > 4096 || !indices) return false;
    // The console is screen-fixed 2D UI: an orthographic projection. World batches render
    // perspective, so this cheap rejection keeps their pixels untouched.
    float projection[16] = {};
    glGetFloatv(GL_PROJECTION_MATRIX, projection);
    if (projection[11] != 0.0f || projection[15] != 1.0f) return false;
    if (!glIsEnabled(GL_VERTEX_ARRAY)) return false;
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
            if (index > 100000) return false;
            const float* vertex = reinterpret_cast<const float*>(static_cast<const char*>(vertices) + size_t(index) * stride);
            float position[4] = {vertex[0], vertex[1], size >= 3 ? vertex[2] : 0.0f, size == 4 ? vertex[3] : 1.0f};
            float eye[4] = {}, clip[4] = {};
            for (int row = 0; row < 4; ++row) for (int col = 0; col < 4; ++col) eye[row] += model[col*4+row] * position[col];
            for (int row = 0; row < 4; ++row) for (int col = 0; col < 4; ++col) clip[row] += projection[col*4+row] * eye[col];
            if (!std::isfinite(clip[3]) || std::abs(clip[3]) < .000001f) return false;
            float x = viewport[0] + (clip[0]/clip[3] + 1) * viewport[2] * .5f;
            float y = viewport[1] + (clip[1]/clip[3] + 1) * viewport[3] * .5f;
            if (!std::isfinite(x) || !std::isfinite(y)) return false;
            if (!i) { minX = maxX = x; minY = maxY = y; }
            else { minX = std::min(minX,x); maxX = std::max(maxX,x); minY = std::min(minY,y); maxY = std::max(maxY,y); }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    // Console chrome never leaves its strip; anything reaching the world or off-screen
    // keeps drawing so partial overlaps can never clip live geometry.
    bool tiles = maxY <= stripTop && minY >= -tolerance && minX >= left && maxX <= right;
    // The console skin's top border beam is a short row of quads whose vertices reach above
    // the strip (46px at 593p). Small ortho quads that stay inside that low band are console
    // chrome too; chat and system notices share it, which the anchored path also hides.
    bool border = count <= 16 && minY >= -tolerance && maxY <= strip * 1.6f;
    bool inside = tiles || border;
    static unsigned reported = 0;
    if (reported < 24 && (inside || maxY < strip * 2.5f)) {
        ++reported;
        wc3::Trace("console batch %s count=%d bbox x=%.0f..%.0f y=%.0f..%.0f stripTop=%.0f",
            inside ? (tiles ? "swallow-tile" : "swallow-border") : "keep", count,
            minX, maxX, minY, maxY, stripTop);
    }
    return inside;
}
static void APIENTRY DrawElementsHook(GLenum mode, GLsizei count, GLenum type, const void* indices) {
    ProbeSubmit(ProbeGlDrawElements, mode, unsigned(count),
        reinterpret_cast<uintptr_t>(_ReturnAddress()));
    // Warcraft batches the console after world rendering, including cinematic backing textures.
    if (healthy && nativeUIPhase && !overlayPass && controller.Visible() && GetTickCount() - lastWorld < 500) {
        minimap.Submit(mode,count,type,indices,originalDrawElements);return;
    }
    // Builds without a verified world/UI anchor (1.27a) never raise nativeUIPhase, so the
    // console cannot be dropped by phase. Drop it by geometry instead: batches whose every
    // vertex projects into the strip below the engine's world viewport are console chrome.
    // The native minimap is captured from these submissions first, mirroring the anchor path.
    if (healthy && !overlayPass && !originalWorld && controller.Visible() && GetTickCount() - lastWorld < 500
        && BatchInConsoleStrip(count, type, indices)) {
        minimap.Submit(mode,count,type,indices,originalDrawElements);return;
    }
    // Builds without a verified world entry point (1.27a) never raise nativeUIPhase. Diagnose
    // instead: walk the stack for Game.dll code addresses at draw time, which reveals the
    // console's render call chain without a world/UI anchor. Large batches sample the world
    // chain for contrast; small batches sample the suspected UI chain.
    if (healthy && !overlayPass && !originalWorld && controller.Visible() && GetTickCount() - lastWorld < 500) {
        static unsigned stackProbes = 0;
        static unsigned tick = 0;
        bool compact = count <= 0x400;
        if (++tick % 64 == 0 && stackProbes < 150) {
            ++stackProbes;
            static uintptr_t imageEnd = 0;
            if (!imageEnd) {
                auto dos = reinterpret_cast<PIMAGE_DOS_HEADER>(gameBase);
                auto nt = reinterpret_cast<PIMAGE_NT_HEADERS>(gameBase + dos->e_lfanew);
                imageEnd = gameBase + nt->OptionalHeader.SizeOfImage;
            }
            void** sp = reinterpret_cast<void**>(_AddressOfReturnAddress());
            char chain[300] = {}; int offset = 0;
            int found = 0;
            for (void** slot = sp; slot < sp + 2048 && found < 10; ++slot) {
                uintptr_t value = reinterpret_cast<uintptr_t>(*slot);
                if (value >= gameBase + 0x1000 && value < imageEnd) {
                    offset += _snprintf_s(chain + offset, sizeof(chain) - offset, _TRUNCATE, "%06X ", unsigned(value - gameBase));
                    ++found;
                }
            }
            wc3::Trace("stackprobe %s count=%d chain=%s", compact ? "small" : "large", count, chain);
        }
    }
    originalDrawElements(mode, count, type, indices);
}
// Immediate-mode and array probes: whichever of these the console band uses, its
// submissions cluster at the frame tail with a stable caller RVA.
using GlBeginFn = void (APIENTRY*)(GLenum);
using GlVertex2fFn = void (APIENTRY*)(GLfloat, GLfloat);
using GlTexCoord2fFn = void (APIENTRY*)(GLfloat, GLfloat);
using GlColor4fFn = void (APIENTRY*)(GLfloat, GLfloat, GLfloat, GLfloat);
using GlColor4ubFn = void (APIENTRY*)(GLubyte, GLubyte, GLubyte, GLubyte);
using GlRectfFn = void (APIENTRY*)(GLfloat, GLfloat, GLfloat, GLfloat);
using GlDrawArraysFn = void (APIENTRY*)(GLenum, GLint, GLsizei);
using GlDrawRangeElementsFn = void (APIENTRY*)(GLenum, GLuint, GLuint, GLsizei, GLenum, const void*);
using GlCallListFn = void (APIENTRY*)(GLuint);
using GlCallListsFn = void (APIENTRY*)(GLsizei, GLenum, const void*);
using GlBitmapFn = void (APIENTRY*)(GLsizei, GLsizei, GLfloat, GLfloat, GLfloat, GLfloat, const GLubyte*);
static GlBeginFn originalGlBegin = nullptr;
static GlVertex2fFn originalGlVertex2f = nullptr;
static GlTexCoord2fFn originalGlTexCoord2f = nullptr;
static GlColor4fFn originalGlColor4f = nullptr;
static GlColor4ubFn originalGlColor4ub = nullptr;
static GlRectfFn originalGlRectf = nullptr;
static GlDrawArraysFn originalGlDrawArrays = nullptr;
static GlDrawRangeElementsFn originalGlDrawRangeElements = nullptr;
static GlCallListFn originalGlCallList = nullptr;
static GlCallListsFn originalGlCallLists = nullptr;
static GlBitmapFn originalGlBitmap = nullptr;
static void APIENTRY GlBeginProbe(GLenum mode) {
    uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    ProbeSubmit(ProbeGlBegin, mode, 0, caller);
    originalGlBegin(mode);
}
static void APIENTRY GlVertex2fProbe(GLfloat x, GLfloat y) {
    uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    ProbeSubmit(ProbeGlVertex2f, FloatBits(x), FloatBits(y), caller);
    originalGlVertex2f(x, y);
}
static void APIENTRY GlTexCoord2fProbe(GLfloat s, GLfloat t) {
    uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    ProbeSubmit(ProbeGlTexCoord2f, FloatBits(s), FloatBits(t), caller);
    originalGlTexCoord2f(s, t);
}
static void APIENTRY GlColor4fProbe(GLfloat r, GLfloat g, GLfloat b, GLfloat a) {
    uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    ProbeSubmit(ProbeGlColor4f, FloatBits(r), FloatBits(a), caller);
    originalGlColor4f(r, g, b, a);
}
static void APIENTRY GlColor4ubProbe(GLubyte r, GLubyte g, GLubyte b, GLubyte a) {
    uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    ProbeSubmit(ProbeGlColor4ub, r | (unsigned(g) << 8) | (unsigned(b) << 16) | (unsigned(a) << 24), 0, caller);
    originalGlColor4ub(r, g, b, a);
}
static void APIENTRY GlRectfProbe(GLfloat x1, GLfloat y1, GLfloat x2, GLfloat y2) {
    uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    ProbeSubmit(ProbeGlRectf, FloatBits(x1), FloatBits(y2), caller);
    originalGlRectf(x1, y1, x2, y2);
}
static void APIENTRY GlDrawArraysProbe(GLenum mode, GLint first, GLsizei count) {
    uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    ProbeSubmit(ProbeGlDrawArrays, mode | (unsigned(first) << 8), unsigned(count), caller);
    originalGlDrawArrays(mode, first, count);
}
static void APIENTRY GlDrawRangeElementsProbe(GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type, const void* indices) {
    uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    ProbeSubmit(ProbeGlDrawRangeElements, mode | (unsigned(count) << 8), type, caller);
    originalGlDrawRangeElements(mode, start, end, count, type, indices);
}
static void APIENTRY GlCallListProbe(GLuint list) {
    uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    ProbeSubmit(ProbeGlCallList, list, 0, caller);
    originalGlCallList(list);
}
static void APIENTRY GlCallListsProbe(GLsizei n, GLenum type, const void* lists) {
    uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    ProbeSubmit(ProbeGlCallLists, unsigned(n), type, caller);
    originalGlCallLists(n, type, lists);
}
static void APIENTRY GlBitmapProbe(GLsizei width, GLsizei height, GLfloat xorig, GLfloat yorig, GLfloat xmove, GLfloat ymove, const GLubyte* bitmap) {
    uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    ProbeSubmit(ProbeGlBitmap, unsigned(width), unsigned(height), caller);
    originalGlBitmap(width, height, xorig, yorig, xmove, ymove, bitmap);
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
    // Console draw-path probe: the ring holds the submissions closest to present, which is
    // where the console band lives. Run-length encode so repeated identical callers stay brief.
    if (healthy && controller.Visible() && !originalWorld && probeFrame <= 600) {
        bool dump = (probeFrame >= 2 && probeFrame < 5) || (probeFrame > 4 && probeFrame % 120 == 0);
        ++probeFrame;
        if (dump) {
            long head = probeHead;
            long start = head > 256 ? head - 256 : 0;
            const ProbeEntry* group = nullptr; long run = 0;
            auto flush = [&]() {
                if (group && run) wc3::Trace("glprobe f=%s rva=%06X a=%08X b=%08X x%ld",
                    probeNames[group->func], group->rva, group->a, group->b, run);
                run = 0;
            };
            for (long i = start; i < head; ++i) {
                const ProbeEntry& entry = probeRing[i % 256];
                if (group && (entry.func != group->func || entry.rva != group->rva ||
                    entry.a != group->a || entry.b != group->b)) flush();
                group = &entry; ++run;
            }
            flush();
            char counts[320] = {}; int offset = 0;
            for (int func = 0; func < ProbeFuncCount; ++func)
                if (probeCounts[func])
                    offset += _snprintf_s(counts + offset, sizeof(counts) - offset, _TRUNCATE, "%s=%ld ", probeNames[func], probeCounts[func]);
            wc3::Trace("glprobe counts %s", counts);
            memset(probeCounts, 0, sizeof(probeCounts));
        }
    }
    // Recheck the real render window after films: Warcraft may replace it or reset its input procedure.
    if (windowInput.Observe(window)) {
        controller.AttachWindow(window);
        overlay.RefreshAfterFocus();
    }
    overlayPass = true;
    // Restore captured minimap pixels before the overlay reads world depth for health bars.
    // FinishFrame no-ops unless a submission captured something this frame.
    minimap.FinishFrame();
    if (healthy && GetTickCount() - lastWorld < 500) overlay.Draw(dc,controller,minimap,healthBars.Items(),healthBarView,healthNear,healthFar);
    // The reticle cannot reach its draw entry while native recovery or stale world frames suppress the overlay.
    else ReticleDiagnostics::Skip(healthy ? "world-frame-stale" : "controller-fault", 0, 0, GetTickCount() - lastWorld);
    overlayPass = false; nativeUIPhase = false;
    // Observe skipped frames too: a valid context alone does not establish an active FPS overlay.
    if (DiagnosticLog::Due(previous)) {
        RECT client = {}; if (window) GetClientRect(window, &client);
        wc3::Trace("render frames=%u healthy=%d fps=%d worldAgeMs=%lu context=%p focus=%d window=%ldx%ld buying=%d scope=%d anchor=%d",
            frames, healthy, controller.Visible(), GetTickCount() - lastWorld, wglGetCurrentContext(),
            window == GetForegroundWindow(), client.right, client.bottom, controller.Buying(), controller.Scoped(),
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
static bool Hook(void* target, void* callback, void** original) {
    MH_STATUS create = MH_CreateHook(target, callback, original);
    MH_STATUS enable = create == MH_OK ? MH_EnableHook(target) : create;
    wc3::Log("hook target=%08X create=%d enable=%d", target, create, enable);
    return create == MH_OK && enable == MH_OK;
}
// Installs the console draw-path probe hooks. Only builds without a world anchor install
// them; 1.26a identifies the UI phase structurally and must not pay the overhead.
static void InstallConsoleDrawProbe(HMODULE gl) {
    struct Probe { const char* name; void** original; void* hook; };
    const Probe probes[] = {
        {"glBegin", reinterpret_cast<void**>(&originalGlBegin), reinterpret_cast<void*>(GlBeginProbe)},
        {"glVertex2f", reinterpret_cast<void**>(&originalGlVertex2f), reinterpret_cast<void*>(GlVertex2fProbe)},
        {"glTexCoord2f", reinterpret_cast<void**>(&originalGlTexCoord2f), reinterpret_cast<void*>(GlTexCoord2fProbe)},
        {"glColor4f", reinterpret_cast<void**>(&originalGlColor4f), reinterpret_cast<void*>(GlColor4fProbe)},
        {"glColor4ub", reinterpret_cast<void**>(&originalGlColor4ub), reinterpret_cast<void*>(GlColor4ubProbe)},
        {"glRectf", reinterpret_cast<void**>(&originalGlRectf), reinterpret_cast<void*>(GlRectfProbe)},
        {"glDrawArrays", reinterpret_cast<void**>(&originalGlDrawArrays), reinterpret_cast<void*>(GlDrawArraysProbe)},
        {"glDrawRangeElements", reinterpret_cast<void**>(&originalGlDrawRangeElements), reinterpret_cast<void*>(GlDrawRangeElementsProbe)},
        {"glCallList", reinterpret_cast<void**>(&originalGlCallList), reinterpret_cast<void*>(GlCallListProbe)},
        {"glCallLists", reinterpret_cast<void**>(&originalGlCallLists), reinterpret_cast<void*>(GlCallListsProbe)},
        {"glBitmap", reinterpret_cast<void**>(&originalGlBitmap), reinterpret_cast<void*>(GlBitmapProbe)},
    };
    for (const Probe& probe : probes) {
        void* target = reinterpret_cast<void*>(GetProcAddress(gl, probe.name));
        if (target) Hook(target, probe.hook, probe.original);
        else wc3::Log("glprobe export missing: %s", probe.name);
    }
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
// when the view ends. Only the visibility this runtime set for its own HUD is claimed.
static void SyncInterface(bool visible) {
    if (originalWorld || !wc3::ShowInterface) return;
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
    if (MH_Initialize() != MH_OK) { wc3::Log("MinHook initialization failed"); return 0; }
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
    void* swapExport = reinterpret_cast<void*>(GetProcAddress(gl, "wglSwapLayerBuffers"));
    Hook(reinterpret_cast<void*>(GetProcAddress(gl, "glViewport")), reinterpret_cast<void*>(ViewportHook), reinterpret_cast<void**>(&originalViewport));
    Hook(reinterpret_cast<void*>(GetProcAddress(gl, "glScissor")), reinterpret_cast<void*>(ScissorHook), reinterpret_cast<void**>(&originalScissor));
    Hook(reinterpret_cast<void*>(GetProcAddress(gl, "glPushAttrib")), reinterpret_cast<void*>(PushAttribHook), reinterpret_cast<void**>(&originalPushAttrib));
    Hook(reinterpret_cast<void*>(GetProcAddress(gl, "glPopAttrib")), reinterpret_cast<void*>(PopAttribHook), reinterpret_cast<void**>(&originalPopAttrib));
    Hook(reinterpret_cast<void*>(GetProcAddress(gl, "wglDeleteContext")), reinterpret_cast<void*>(DeleteContextHook), reinterpret_cast<void**>(&originalDeleteContext));
    Hook(reinterpret_cast<void*>(GetProcAddress(gl, "glDrawElements")), reinterpret_cast<void*>(DrawElementsHook), reinterpret_cast<void**>(&originalDrawElements));
    if (!originalWorld) InstallConsoleDrawProbe(gl);
    Hook(swapExport, reinterpret_cast<void*>(SwapLayerHook), reinterpret_cast<void**>(&originalSwap));
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
        // Defer engine work until after the loader lock has been released.
        HANDLE thread = CreateThread(nullptr, 0, Initialize, nullptr, 0, nullptr);
        if (thread) CloseHandle(thread);
    return TRUE;
}
