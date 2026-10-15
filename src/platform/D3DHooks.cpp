#include "D3DHooks.hpp"
#include "../presentation/D3DCanvas.hpp"
#include "../platform/WarcraftApi.hpp"
#include <MinHook.h>
#include <cstring>
#include <utility>

// Direct3D 9 vtable indices. IDirect3D9 adds CheckDeviceFormatConversion over IDirect3D8, so the
// factory's CreateDevice sits at 16; IDirect3DDevice9 adds GetSwapChain and
// GetNumberOfSwapChains over the Direct3D 8 device, which pushes Reset to 16 and Present to 17.
// (Reading Direct3D 9 with the Direct3D 8 device indices silently patched GetSwapChain and
// GetNumberOfSwapChains and never anchored a frame.)
constexpr int kFactoryCreateDeviceSlot = 16;
constexpr int kDeviceResetSlot = 16;
constexpr int kDevicePresentSlot = 17;
// After EndScene(42) and Clear(43): SetTransform is 44 and SetViewport 47. The two in between
// (GetTransform 45, MultiplyTransform 46) and the slot below each of them must never be hooked:
// a detour with the wrong parameter count pops the wrong number of stack bytes on x86 stdcall.
constexpr int kDeviceSetTransformSlot = 44;
constexpr int kDeviceSetViewportSlot = 47;
// A present can also arrive through the swap chain (IUnknown 0-2, then Present at 3), which is
// what most Direct3D 9 render loops use, so both entry points are anchored.
constexpr int kSwapChainPresentSlot = 3;
// Throwaway devices only need a valid window and surface size; nothing is ever shown.
constexpr int kProbeSize = 8;
// The client directory usually holds a wrapper (an ENB-style d3d9.dll) that keeps its own device
// objects, while the runtime it delegates to owns a second, distinct copy. Both are tracked.
constexpr int kSourceCount = 2;
constexpr int kFactoryCount = 4;

namespace D3DHooks {
namespace {

using Create9 = IDirect3D9* (WINAPI*)(UINT);
using CreateDevice9 = HRESULT (STDMETHODCALLTYPE*)(IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS*, IDirect3DDevice9**);
using Present9 = HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);
using Reset9 = HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
using SwapPresent9 = HRESULT (STDMETHODCALLTYPE*)(IDirect3DSwapChain9*, const RECT*, const RECT*, HWND, const RGNDATA*);
using SetTransform9 = HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*, D3DTRANSFORMSTATETYPE, const D3DMATRIX*);
using SetViewport9 = HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*, const D3DVIEWPORT9*);

ProjectionCallback onProjection = nullptr;

// One loaded Direct3D 9 copy and the frame endpoints reachable through it. The functions are
// hooked by their code address rather than through a vtable slot: Warcraft and the overlay
// wrappers around it cache the pointers they resolved, so a slot written after the fact is never
// consulted, while every cached pointer still lands on the patched code.
struct Source {
    HMODULE module = nullptr;
    const char* label = "";
    Create9 create9 = nullptr;
    CreateDevice9 createDevice = nullptr;
    Present9 devicePresent = nullptr;
    Present9 originalDevicePresent = nullptr;
    Reset9 deviceReset = nullptr;
    Reset9 originalDeviceReset = nullptr;
    SwapPresent9 swapPresent = nullptr;
    SwapPresent9 originalSwapPresent = nullptr;
    SetTransform9 setTransform = nullptr;
    SetTransform9 originalSetTransform = nullptr;
    SetViewport9 setViewport = nullptr;
    SetViewport9 originalSetViewport = nullptr;
};
Source sources[kSourceCount];

// A patched factory remembers which source it came from so its CreateDevice call still forwards
// to the right runtime.
struct PatchedFactory { IDirect3D9* factory = nullptr; int source = -1; };
PatchedFactory patchedFactories[kFactoryCount];

D3DCanvasImpl<IDirect3DDevice9> canvas9;
PresentCallback callback;
bool installed = false;
HWND window = nullptr;
HWND probeWindow = nullptr;
IDirect3DDevice9* activeDevice = nullptr;
// Most recent viewport the device was given; a projection is only trusted as the world's when
// this covers the whole client, mirroring how the OpenGL path recognises its world batch.
D3DVIEWPORT9 lastViewport = {};

bool FrameSize(int& width, int& height) {
    RECT client = {};
    // In fullscreen the client rectangle equals the back buffer; in windowed mode it is the
    // actual game window. A destroyed/minimized window yields no size and skips the frame.
    if (window && GetClientRect(window, &client) && client.right > 0 && client.bottom > 0) {
        width = client.right; height = client.bottom;
        return true;
    }
    return false;
}

// Largest visible top-level window of this process: the game's render window. Warcraft is already
// running when these hooks install, so its device creation (and the window it was given) was
// never observed and the window has to be found instead of remembered.
struct WindowSearch { DWORD pid = 0; HWND best = nullptr; LONG area = 0; };
BOOL CALLBACK FindWindowProc(HWND candidate, LPARAM parameter) {
    WindowSearch* search = reinterpret_cast<WindowSearch*>(parameter);
    DWORD pid = 0;
    GetWindowThreadProcessId(candidate, &pid);
    if (pid != search->pid || !IsWindowVisible(candidate)) return TRUE;
    RECT rect = {};
    if (!GetClientRect(candidate, &rect)) return TRUE;
    LONG area = (rect.right - rect.left) * (rect.bottom - rect.top);
    if (area > search->area) { search->area = area; search->best = candidate; }
    return TRUE;
}
HWND ResolveWindow() {
    if (window && IsWindow(window)) return window;
    WindowSearch search;
    search.pid = GetCurrentProcessId();
    EnumWindows(FindWindowProc, reinterpret_cast<LPARAM>(&search));
    window = search.best;
    return window;
}

void DrawFrame(IDirect3DDevice9* device, const char* label) {
    if (!device) return;
    static const char* reported = nullptr;
    if (reported != label) {
        reported = label;
        wc3::Log("D3D9 frame anchor active source=%s device=%p", label, device);
    }
    // The overlay ortho is built from the window client rect; a back buffer of a different size
    // (a wrapper or widescreen helper choosing its own surface) stretches every overlay pixel.
    static int reportedWidth = -1, reportedHeight = -1;
    IDirect3DSurface9* backBuffer = nullptr;
    if (SUCCEEDED(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backBuffer)) && backBuffer) {
        D3DSURFACE_DESC desc = {};
        if (SUCCEEDED(backBuffer->GetDesc(&desc)) && (int(desc.Width) != reportedWidth || int(desc.Height) != reportedHeight)) {
            reportedWidth = int(desc.Width); reportedHeight = int(desc.Height);
            RECT client = {};
            GetClientRect(window, &client);
            wc3::Log("D3D9 back buffer %ux%u client %ldx%ld", desc.Width, desc.Height, client.right, client.bottom);
        }
        backBuffer->Release();
    }
    int width = 0, height = 0;
    if (!callback || !ResolveWindow() || !FrameSize(width, height)) return;
    canvas9.AttachDevice(device);
    callback(canvas9, window, width, height);
}

template<int S>
HRESULT STDMETHODCALLTYPE PresentDetour(IDirect3DDevice9* device, const RECT* source, const RECT* dest, HWND overrideWindow, const RGNDATA* dirty) {
    Source& entry = sources[S];
    Present9 forward = entry.originalDevicePresent;
    // A second runtime copy can expose the very same function; the detour that was installed
    // first owns the trampoline, so it is borrowed before giving up.
    if (!forward)
        for (const Source& other : sources)
            if (other.devicePresent == entry.devicePresent && other.originalDevicePresent) forward = other.originalDevicePresent;
    if (!forward) return D3DERR_INVALIDCALL;
    DrawFrame(device, entry.label);
    return forward(device, source, dest, overrideWindow, dirty);
}
template<int S>
HRESULT STDMETHODCALLTYPE ResetDetour(IDirect3DDevice9* device, D3DPRESENT_PARAMETERS* params) {
    Source& entry = sources[S];
    Reset9 forward = entry.originalDeviceReset;
    if (!forward)
        for (const Source& other : sources)
            if (other.deviceReset == entry.deviceReset && other.originalDeviceReset) forward = other.originalDeviceReset;
    if (!forward) return D3DERR_INVALIDCALL;
    // D3DPOOL_MANAGED textures survive a Reset, so the overlay keeps its handles; the canvas
    // re-saves device state on the next frame.
    return forward(device, params);
}
template<int S>
HRESULT STDMETHODCALLTYPE SwapPresentDetour(IDirect3DSwapChain9* swapChain, const RECT* source, const RECT* dest, HWND overrideWindow, const RGNDATA* dirty) {
    Source& entry = sources[S];
    SwapPresent9 forward = entry.originalSwapPresent;
    if (!forward)
        for (const Source& other : sources)
            if (other.swapPresent == entry.swapPresent && other.originalSwapPresent) forward = other.originalSwapPresent;
    if (!forward) return D3DERR_INVALIDCALL;
    // The swap chain does not carry the device; it is asked once and remembered.
    if (!activeDevice && swapChain) swapChain->GetDevice(&activeDevice);
    DrawFrame(activeDevice, entry.label);
    return forward(swapChain, source, dest, overrideWindow, dirty);
}
template<int S>
HRESULT STDMETHODCALLTYPE SetTransformDetour(IDirect3DDevice9* device, D3DTRANSFORMSTATETYPE state, const D3DMATRIX* matrix) {
    Source& entry = sources[S];
    SetTransform9 forward = entry.originalSetTransform;
    if (!forward)
        for (const Source& other : sources)
            if (other.setTransform == entry.setTransform && other.originalSetTransform) forward = other.originalSetTransform;
    if (!forward) return D3DERR_INVALIDCALL;
    // The world projection is what world-anchored HUD art must be measured against; the caller
    // filters out the portrait and interface passes.
    if (state == D3DTS_PROJECTION && matrix && onProjection) {
        const D3DVIEWPORT9 viewport = lastViewport;
        int box[4] = {int(viewport.X), int(viewport.Y), int(viewport.Width), int(viewport.Height)};
        onProjection(&matrix->m[0][0], box);
    }
    return forward(device, state, matrix);
}
template<int S>
HRESULT STDMETHODCALLTYPE SetViewportDetour(IDirect3DDevice9* device, const D3DVIEWPORT9* viewport) {
    Source& entry = sources[S];
    SetViewport9 forward = entry.originalSetViewport;
    if (!forward)
        for (const Source& other : sources)
            if (other.setViewport == entry.setViewport && other.originalSetViewport) forward = other.originalSetViewport;
    if (!forward) return D3DERR_INVALIDCALL;
    if (viewport) lastViewport = *viewport;
    return forward(device, viewport);
}
Present9 const kPresentDetours[kSourceCount] = {&PresentDetour<0>, &PresentDetour<1>};
Reset9 const kResetDetours[kSourceCount] = {&ResetDetour<0>, &ResetDetour<1>};
SwapPresent9 const kSwapPresentDetours[kSourceCount] = {&SwapPresentDetour<0>, &SwapPresentDetour<1>};
SetTransform9 const kSetTransformDetours[kSourceCount] = {&SetTransformDetour<0>, &SetTransformDetour<1>};
SetViewport9 const kSetViewportDetours[kSourceCount] = {&SetViewportDetour<0>, &SetViewportDetour<1>};

// Code-hooks a single endpoint. Re-hooking an address another source already claimed is not an
// error: both detours forward through the one trampoline MinHook created.
bool HookEndpoint(void* target, void* detour, void** trampoline) {
    if (!target) return false;
    MH_STATUS created = MH_CreateHook(target, detour, trampoline);
    if (created == MH_ERROR_ALREADY_CREATED) return true;
    if (created != MH_OK) return false;
    return MH_EnableHook(target) == MH_OK;
}

// Replaces one COM vtable slot (a function pointer stored at vtable[index]) with our handler.
// The slot page is made writable briefly; the original pointer is kept to forward through.
// MinHook is not used here because it patches code bytes, not data slots.
template<typename T>
bool PatchVtableSlot(void* object, int index, T replacement, T* original) {
    if (!object) return false;
    void** vtable = *reinterpret_cast<void***>(object);
    if (!vtable || !vtable[index]) return false;
    if (!*original) *original = reinterpret_cast<T>(vtable[index]);
    if (vtable[index] == reinterpret_cast<void*>(replacement)) return true;
    DWORD previous = 0;
    if (!VirtualProtect(&vtable[index], sizeof(void*), PAGE_READWRITE, &previous)) return false;
    vtable[index] = reinterpret_cast<void*>(replacement);
    VirtualProtect(&vtable[index], sizeof(void*), previous, &previous);
    return true;
}

int SourceOf(IDirect3D9* factory) {
    for (const PatchedFactory& entry : patchedFactories)
        if (entry.factory == factory) return entry.source;
    return -1;
}
void RememberFactory(IDirect3D9* factory, int source) {
    for (PatchedFactory& entry : patchedFactories)
        if (!entry.factory) { entry.factory = factory; entry.source = source; return; }
}

IDirect3D9* WINAPI Create9Detour0(UINT version);
IDirect3D9* WINAPI Create9Detour1(UINT version);

HRESULT STDMETHODCALLTYPE CreateDevice9Hook(IDirect3D9* factory, UINT adapter, D3DDEVTYPE type, HWND focus, DWORD behavior, D3DPRESENT_PARAMETERS* params, IDirect3DDevice9** device);

// Anchors the endpoints of a live device. A device created after these hooks install reports its
// own functions, which is how a runtime whose probe failed is still covered.
bool AnchorDevice(int source, IDirect3DDevice9* device, const char* origin) {
    if (!device || source < 0) return false;
    Source& entry = sources[source];
    void** vtable = *reinterpret_cast<void***>(device);
    if (!vtable) return false;
    entry.devicePresent = reinterpret_cast<Present9>(vtable[kDevicePresentSlot]);
    entry.deviceReset = reinterpret_cast<Reset9>(vtable[kDeviceResetSlot]);
    bool present = HookEndpoint(reinterpret_cast<void*>(entry.devicePresent), reinterpret_cast<void*>(kPresentDetours[source]), reinterpret_cast<void**>(&entry.originalDevicePresent));
    bool reset = HookEndpoint(reinterpret_cast<void*>(entry.deviceReset), reinterpret_cast<void*>(kResetDetours[source]), reinterpret_cast<void**>(&entry.originalDeviceReset));
    // The swap chain exposes a second present path; a device without one is still usable.
    IDirect3DSwapChain9* swapChain = nullptr;
    if (SUCCEEDED(device->GetSwapChain(0, &swapChain)) && swapChain) {
        void** chainVtable = *reinterpret_cast<void***>(swapChain);
        if (chainVtable) {
            entry.swapPresent = reinterpret_cast<SwapPresent9>(chainVtable[kSwapChainPresentSlot]);
            HookEndpoint(reinterpret_cast<void*>(entry.swapPresent), reinterpret_cast<void*>(kSwapPresentDetours[source]), reinterpret_cast<void**>(&entry.originalSwapPresent));
        }
        swapChain->Release();
    }
    entry.setTransform = reinterpret_cast<SetTransform9>(vtable[kDeviceSetTransformSlot]);
    entry.setViewport = reinterpret_cast<SetViewport9>(vtable[kDeviceSetViewportSlot]);
    bool transform = HookEndpoint(reinterpret_cast<void*>(entry.setTransform), reinterpret_cast<void*>(kSetTransformDetours[source]), reinterpret_cast<void**>(&entry.originalSetTransform));
    bool viewport = HookEndpoint(reinterpret_cast<void*>(entry.setViewport), reinterpret_cast<void*>(kSetViewportDetours[source]), reinterpret_cast<void**>(&entry.originalSetViewport));
    wc3::Log("D3D9 anchored source=%s origin=%s vtable=%p present=%d reset=%d swapPresent=%p transform=%d viewport=%d",
        entry.label, origin, vtable, present ? 1 : 0, reset ? 1 : 0, entry.swapPresent, transform ? 1 : 0, viewport ? 1 : 0);
    return present;
}

HRESULT STDMETHODCALLTYPE CreateDevice9Hook(IDirect3D9* factory, UINT adapter, D3DDEVTYPE type, HWND focus, DWORD behavior, D3DPRESENT_PARAMETERS* params, IDirect3DDevice9** device) {
    int source = SourceOf(factory);
    if (source < 0 || !sources[source].createDevice) return D3DERR_INVALIDCALL;
    HRESULT result = sources[source].createDevice(factory, adapter, type, focus, behavior, params, device);
    if (SUCCEEDED(result) && device && *device) {
        AnchorDevice(source, *device, "create");
        activeDevice = *device;
        // The probe's own window must never become the frame size source.
        if (focus && focus != probeWindow) window = focus;
        wc3::Log("D3D9 device created source=%s window=%p adapter=%u", sources[source].label, window, adapter);
    }
    return result;
}

IDirect3D9* Create9Common(UINT version, int source) {
    Source& entry = sources[source];
    if (!entry.create9) return nullptr;
    IDirect3D9* factory = entry.create9(version);
    // Several factories can be handed out; each keeps its own CreateDevice trampoline.
    if (factory) {
        if (PatchVtableSlot(factory, kFactoryCreateDeviceSlot, CreateDevice9Hook, &entry.createDevice)) {
            RememberFactory(factory, source);
        } else {
            wc3::Log("D3D9 CreateDevice vtable slot patch failed source=%s", entry.label);
        }
    }
    return factory;
}
IDirect3D9* WINAPI Create9Detour0(UINT version) { return Create9Common(version, 0); }
IDirect3D9* WINAPI Create9Detour1(UINT version) { return Create9Common(version, 1); }
Create9 const kCreateDetours[kSourceCount] = {Create9Detour0, Create9Detour1};

bool HookCreate9(int source, HMODULE module, const char* label) {
    if (!module) return false;
    Source& entry = sources[source];
    entry.module = module;
    entry.label = label;
    void* create9 = reinterpret_cast<void*>(GetProcAddress(module, "Direct3DCreate9"));
    if (!create9) {
        wc3::Log("D3D9 Create9 export missing source=%s module=%p", label, module);
        return false;
    }
    if (MH_CreateHook(create9, reinterpret_cast<void*>(kCreateDetours[source]), reinterpret_cast<void**>(&entry.create9)) != MH_OK ||
        MH_EnableHook(create9) != MH_OK) {
        wc3::Log("D3D9 Create9 hook failed source=%s module=%p", label, module);
        return false;
    }
    return true;
}

// Warcraft resolves Direct3DCreate9 while it brings up its renderer, which happens before this
// plugin's init thread runs, so the game's device usually exists already and no CreateDevice call
// is left to observe. A throwaway device is created through the runtime instead: its vtable
// yields the exact Present/Reset/swap-chain functions the running device calls, which are then
// code-hooked. The probe window is never shown and the device is released immediately.
bool ProbeDevice(int source) {
    Source& entry = sources[source];
    if (!entry.module) return false;
    auto create9 = reinterpret_cast<Create9>(GetProcAddress(entry.module, "Direct3DCreate9"));
    if (!create9) return false;
    if (!probeWindow) probeWindow = CreateWindowExA(0, "STATIC", "WarcraftCS D3D probe", WS_POPUP,
        0, 0, kProbeSize, kProbeSize, nullptr, nullptr, nullptr, nullptr);
    if (!probeWindow) return false;
    bool anchored = false;
    IDirect3D9* factory = create9(D3D_SDK_VERSION);
    if (factory) {
        D3DPRESENT_PARAMETERS present = {};
        present.Windowed = TRUE;
        present.SwapEffect = D3DSWAPEFFECT_DISCARD;
        present.hDeviceWindow = probeWindow;
        present.BackBufferFormat = D3DFMT_UNKNOWN;
        present.BackBufferWidth = kProbeSize;
        present.BackBufferHeight = kProbeSize;
        present.BackBufferCount = 1;
        present.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
        const DWORD behaviors[] = {D3DCREATE_SOFTWARE_VERTEXPROCESSING, D3DCREATE_HARDWARE_VERTEXPROCESSING, D3DCREATE_MIXED_VERTEXPROCESSING};
        for (DWORD behavior : behaviors) {
            IDirect3DDevice9* device = nullptr;
            if (SUCCEEDED(factory->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, probeWindow, behavior, &present, &device))) {
                wc3::Log("D3D9 probe device created source=%s behavior=%X", entry.label, behavior);
                anchored = AnchorDevice(source, device, "probe");
                device->Release();
                wc3::Log("D3D9 probe device released source=%s anchored=%d", entry.label, anchored ? 1 : 0);
                break;
            }
        }
        factory->Release();
    }
    wc3::Log("D3D9 probe source=%s module=%p anchored=%d", entry.label, entry.module, anchored ? 1 : 0);
    return anchored;
}

// The system copy is probed as well: a wrapper in the client directory owns its own device
// objects, so only the runtime underneath exposes the functions its delegated presents call.
HMODULE LoadSystemD3D9() {
    char path[MAX_PATH] = {};
    UINT length = GetSystemDirectoryA(path, MAX_PATH);
    if (!length || length + 12 >= MAX_PATH) return nullptr;
    strcat_s(path, "\\d3d9.dll");
    return LoadLibraryA(path);
}

} // namespace

bool Install(PresentCallback present, ProjectionCallback projection) {
    if (installed) return true;
    callback = std::move(present);
    onProjection = projection;
    // Sampled before the loads below: a live handle means the game already brought up Direct3D,
    // which is also the signal that its device predates these hooks.
    HMODULE active = GetModuleHandleA("d3d9.dll");
    HMODULE search = LoadLibraryA("d3d9.dll");
    HMODULE system = LoadSystemD3D9();
    bool hooked = HookCreate9(0, search, "search");
    if (system && system != search) {
        if (HookCreate9(1, system, "system")) hooked = true;
    }
    installed = hooked;
    wc3::Log("D3D9 hooks installed=%d renderer=%d search=%p system=%p", installed, active ? 1 : 0, search, system);
    if (!hooked) return installed;
    // Only a renderer that already loaded Direct3D needs the probe; an OpenGL frame never reaches
    // these functions and creating a device there would be a needless side effect.
    if (active) {
        wc3::Log("D3D9 probing search module first");
        ProbeDevice(0);
        if (system && system != search) {
            wc3::Log("D3D9 probing system module");
            ProbeDevice(1);
        }
    }
    return installed;
}
bool Installed() { return installed; }

} // namespace D3DHooks
