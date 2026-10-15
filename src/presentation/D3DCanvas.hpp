#pragma once
#include "Canvas.hpp"
#include "../platform/WarcraftApi.hpp"
#include <d3d9.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <list>
#include <string>
#include <unordered_map>
#include <vector>

// Direct3D 9 canvas. The device exposes the fixed-function surface the overlay needs (render
// states, FVF immediate-mode drawing, managed textures); the API details are isolated in
// D3DDeviceTraits. 2D primitives use XYZRHW|DIFFUSE so screen coordinates are pixels (y-down)
// with z directly the [0,1] depth fraction, which lets health bars compare against the game's
// own depth buffer without touching its projection. Only the first-person weapon pass installs
// a perspective matrix, and it receives vertices in the OpenGL camera convention (looking down
// -z) and mirrors z to the left-handed projection.
template<typename TDevice>
struct D3DDeviceTraits;

template<>
struct D3DDeviceTraits<IDirect3DDevice9> {
    using Texture = IDirect3DTexture9;
    using Surface = IDirect3DSurface9;
    using Viewport = D3DVIEWPORT9;
    static HRESULT CreateTexture(IDirect3DDevice9* device, UINT width, UINT height,
        D3DFORMAT format, D3DPOOL pool, Texture** out) {
        // D3D9 appends a shared-handle out-parameter after the texture pointer.
        return device->CreateTexture(width, height, 1, 0, format, pool, out, nullptr);
    }
    static void SetVertexShader(IDirect3DDevice9* device, void* shader) {
        device->SetVertexShader(static_cast<IDirect3DVertexShader9*>(shader));
    }
    static void SetPixelShader(IDirect3DDevice9* device, void* shader) {
        device->SetPixelShader(static_cast<IDirect3DPixelShader9*>(shader));
    }
    static void* GetVertexShader(IDirect3DDevice9* device) {
        IDirect3DVertexShader9* shader = nullptr; device->GetVertexShader(&shader); return shader;
    }
    static void* GetPixelShader(IDirect3DDevice9* device) {
        IDirect3DPixelShader9* shader = nullptr; device->GetPixelShader(&shader); return shader;
    }
    static void SetVertexDeclaration(IDirect3DDevice9* device, void* declaration) {
        device->SetVertexDeclaration(static_cast<IDirect3DVertexDeclaration9*>(declaration));
    }
    static void* GetVertexDeclaration(IDirect3DDevice9* device) {
        IDirect3DVertexDeclaration9* declaration = nullptr;
        device->GetVertexDeclaration(&declaration); return declaration;
    }
};

template<typename TDevice>
class D3DCanvasImpl : public Canvas {
public:
    using Traits = D3DDeviceTraits<TDevice>;
    using TTexture = typename Traits::Texture;
    using TSurface = typename Traits::Surface;
    using TViewport = typename Traits::Viewport;

    const char* Name() const override { return "d3d9"; }
    uint64_t Generation() const override { return generation_; }

    void AttachDevice(TDevice* device) {
        if (device_ == device && device) return;
        if (device_) ResetGraphics(true);
        device_ = device;
        lostLogged_ = false;
        if (device_) ++generation_;
    }
    void DetachDevice() { AttachDevice(nullptr); }
    TDevice* Device() const { return device_; }
    // Not const: the first device-lost transition is logged once through lostLogged_.
    bool DeviceReady() {
        if (!device_) return false;
        HRESULT level = device_->TestCooperativeLevel();
        if (level == D3DERR_DEVICELOST) {
            if (!lostLogged_) { wc3::Log("D3D overlay device lost"); lostLogged_ = true; }
            return false;
        }
        if (level == D3DERR_DEVICENOTRESET) {
            if (!lostLogged_) { wc3::Log("D3D overlay device needs reset"); lostLogged_ = true; }
            return false;
        }
        lostLogged_ = false;
        return true;
    }

    bool BeginFrame(HDC dc, int width, int height) override {
        (void)dc;
        if (!DeviceReady()) return false;
        width_ = width; height_ = height;
        SaveState();
        TViewport viewport = {};
        viewport.X = 0; viewport.Y = 0;
        viewport.Width = DWORD(width); viewport.Height = DWORD(height);
        viewport.MinZ = 0; viewport.MaxZ = 1;
        device_->SetViewport(&viewport);
        SetOverlayState();
        active_ = true;
        return true;
    }

    void EndFrame() override {
        if (!active_) return;
        active_ = false;
        RestoreState();
    }

    // ---- 2D primitives (XYZRHW|DIFFUSE, screen pixels, y-down) ----

    void FillRect(float x, float y, float w, float h, float r, float g, float b, float a) override {
        if (!active_) return;
        V2C vertices[4] = {
            {x, y, 0, 1, Color(r, g, b, a)},
            {x + w, y, 0, 1, Color(r, g, b, a)},
            {x + w, y + h, 0, 1, Color(r, g, b, a)},
            {x, y + h, 0, 1, Color(r, g, b, a)},
        };
        StageDisabled();
        Draw2D(D3DPT_TRIANGLELIST, vertices, 4);
    }

    void OutlineRect(float x, float y, float w, float h, float r, float g, float b, float a) override {
        if (!active_) return;
        DWORD color = Color(r, g, b, a);
        V2C vertices[5] = {
            {x, y, 0, 1, color}, {x + w, y, 0, 1, color},
            {x + w, y + h, 0, 1, color}, {x, y + h, 0, 1, color}, {x, y, 0, 1, color},
        };
        StageDisabled();
        Draw2D(D3DPT_LINESTRIP, vertices, 5);
    }

    void Line(float x0, float y0, float x1, float y1, float thickness, float r, float g, float b, float a) override {
        if (!active_ || thickness <= 0) return;
        // D3D9 has no wide-line support; a rotated rectangle renders identically at any angle.
        float dx = x1 - x0, dy = y1 - y0;
        float length = std::sqrt(dx * dx + dy * dy);
        if (length < 0.0001f) return;
        float nx = -dy / length * thickness * 0.5f, ny = dx / length * thickness * 0.5f;
        DWORD color = Color(r, g, b, a);
        V2C vertices[4] = {
            {x0 + nx, y0 + ny, 0, 1, color}, {x1 + nx, y1 + ny, 0, 1, color},
            {x1 - nx, y1 - ny, 0, 1, color}, {x0 - nx, y0 - ny, 0, 1, color},
        };
        StageDisabled();
        Draw2D(D3DPT_TRIANGLELIST, vertices, 4);
    }

    void TriangleStrip(const float* xy, int count, float r, float g, float b, float a) override {
        if (!active_ || count < 3) return;
        std::vector<V2C> vertices;
        vertices.reserve(size_t(count));
        DWORD color = Color(r, g, b, a);
        for (int i = 0; i < count; ++i) vertices.push_back({xy[i * 2], xy[i * 2 + 1], 0, 1, color});
        StageDisabled();
        Draw2D(D3DPT_TRIANGLESTRIP, vertices.data(), count);
    }

    void TriangleFan(const float* xy, int count, float r, float g, float b, float a) override {
        if (!active_ || count < 3) return;
        std::vector<V2C> vertices;
        vertices.reserve(size_t(count));
        DWORD color = Color(r, g, b, a);
        for (int i = 0; i < count; ++i) vertices.push_back({xy[i * 2], xy[i * 2 + 1], 0, 1, color});
        StageDisabled();
        Draw2D(D3DPT_TRIANGLEFAN, vertices.data(), count);
    }

    void Text(float x, float y, const char* text, int size, float r, float g, float b, float a) override {
        if (!active_ || !text || !*text) return;
        TextSprite* sprite = Sprite(text, size);
        if (!sprite) return;
        TextureQuad(sprite->texture, x, y, float(sprite->width), float(sprite->height), r, g, b, a, false);
    }

    int GlyphWidth(int size) const override {
        // Measured once on the 20px hint font, mirroring the OpenGL path.
        if (!glyphWidth_ && device_) {
            HDC dc = CreateCompatibleDC(nullptr);
            HFONT font = CreateFontA(20, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, ANSI_CHARSET,
                OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, FF_DONTCARE, "Consolas");
            HGDIOBJ old = SelectObject(dc, font);
            TEXTMETRICA metrics = {}; GetTextMetricsA(dc, &metrics);
            glyphWidth_ = std::max(1, int(metrics.tmAveCharWidth));
            SelectObject(dc, old); DeleteObject(font); DeleteDC(dc);
        }
        (void)size;
        return glyphWidth_ ? glyphWidth_ : 12;
    }

    // ---- textures ----

    uintptr_t CreateTexture(const unsigned char* rgba, int width, int height) override {
        if (!device_ || !rgba || width <= 0 || height <= 0) return 0;
        TTexture* texture = nullptr;
        if (FAILED(Traits::CreateTexture(device_, UINT(width), UINT(height),
                D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &texture))) return 0;
        D3DLOCKED_RECT locked = {};
        if (FAILED(texture->LockRect(0, &locked, nullptr, 0))) { texture->Release(); return 0; }
        // D3D A8R8G8B8 stores BGRA bytes; tightly packed input is converted row by row.
        unsigned char* dst = static_cast<unsigned char*>(locked.pBits);
        for (int y = 0; y < height; ++y) {
            const unsigned char* src = rgba + size_t(y) * width * 4;
            unsigned char* row = dst + size_t(y) * locked.Pitch;
            for (int x = 0; x < width; ++x) {
                row[x * 4 + 0] = src[x * 4 + 2];
                row[x * 4 + 1] = src[x * 4 + 1];
                row[x * 4 + 2] = src[x * 4 + 0];
                row[x * 4 + 3] = src[x * 4 + 3];
            }
        }
        texture->UnlockRect(0);
        return uintptr_t(texture);
    }

    void DestroyTexture(uintptr_t texture) override {
        TTexture* tex = reinterpret_cast<TTexture*>(texture);
        if (tex) tex->Release();
    }

    bool TextureValid(uintptr_t texture) const override {
        // Managed textures live as long as the owning COM reference; a non-null handle is
        // valid in the attached device (unlike GL names, D3D handles are not recycled).
        return device_ != nullptr && texture != 0;
    }

    void TextureQuad(uintptr_t texture, float x, float y, float w, float h,
        float r, float g, float b, float a, bool flipVertical) override {
        if (!active_ || !texture) return;
        float v0 = flipVertical ? 1.f : 0.f, v1 = flipVertical ? 0.f : 1.f;
        DWORD color = Color(r, g, b, a);
        V2 vertices[4] = {
            {x, y, 0, 1, color, 0, v0},
            {x + w, y, 0, 1, color, 1, v0},
            {x + w, y + h, 0, 1, color, 1, v1},
            {x, y + h, 0, 1, color, 0, v1},
        };
        StageTexture(reinterpret_cast<TTexture*>(texture));
        Draw2D(D3DPT_TRIANGLELIST, vertices, 4);
    }

    void TexturedTriangles(uintptr_t texture, const float* xyzuv, int vertexCount,
        float r, float g, float b, float a) override {
        if (!active_ || !texture || !xyzuv || vertexCount < 3) return;
        std::vector<V3> vertices;
        vertices.reserve(size_t(vertexCount));
        DWORD color = Color(r, g, b, a);
        for (int i = 0; i < vertexCount; ++i) {
            const float* v = xyzuv + i * 5;
            // OpenGL camera convention (looking down -z) is mirrored into the left-handed
            // projection by negating z; screen placement and depth ordering are unchanged.
            vertices.push_back({v[0], v[1], -v[2], color, v[3], v[4]});
        }
        StageTexture(reinterpret_cast<TTexture*>(texture));
        SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1);
        device_->DrawPrimitiveUP(D3DPT_TRIANGLELIST, UINT(vertexCount / 3), vertices.data(), sizeof(V3));
    }

    void PreviewTriangles(uintptr_t texture, const float* xyzuv, int vertexCount,
        float r, float g, float b, float a) override {
        if (!active_ || !texture || !xyzuv || vertexCount < 3) return;
        std::vector<V3> vertices;
        vertices.reserve(size_t(vertexCount));
        DWORD color = Color(r, g, b, a);
        for (int i = 0; i < vertexCount; ++i) {
            const float* v = xyzuv + i * 5;
            // z is already the [0,1] depth fraction; the preview ortho (zn=0,zf=1) maps it
            // straight into the depth buffer, so no camera-space mirroring applies.
            vertices.push_back({v[0], v[1], v[2], color, v[3], v[4]});
        }
        StageTexture(reinterpret_cast<TTexture*>(texture));
        SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1);
        device_->DrawPrimitiveUP(D3DPT_TRIANGLELIST, UINT(vertexCount / 3), vertices.data(), sizeof(V3));
    }

    // ---- world-depth health bars ----

    void BeginBars(int width, int height) override {
        (void)width; (void)height;
        if (!active_) return;
        device_->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
        device_->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
        device_->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
        device_->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    }

    void BarQuad(float x, float y, float w, float h, float z, float r, float g, float b, float a) override {
        if (!active_) return;
        DWORD color = Color(r, g, b, a);
        V2C vertices[4] = {
            {x, y, z, 1, color}, {x + w, y, z, 1, color},
            {x + w, y + h, z, 1, color}, {x, y + h, z, 1, color},
        };
        StageDisabled();
        SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
        device_->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, vertices, sizeof(V2C));
    }

    void EndBars() override {
        if (!active_) return;
        SetOverlayState();
    }

    // ---- first-person weapon pass ----

    void BeginWeapon(int width, int height, float projectionScale, float nearPlane, float farPlane) override {
        if (!active_) return;
        // Mirror of the OpenGL glFrustum(-side,side,-top,top,near,far): the left-handed
        // projection maps the mirrored vertices (z negated in TexturedTriangles) to the
        // same screen positions and near->0/far->1 depth range.
        float top = std::tan(45.0f * 0.01745329252f) * projectionScale;
        float side = top * float(width) / float(height);
        D3DMATRIX projection = PerspectiveOffCenter(-side, side, -top, top, nearPlane, farPlane);
        D3DMATRIX identity = IdentityMatrix();
        device_->SetTransform(D3DTS_PROJECTION, &projection);
        device_->SetTransform(D3DTS_VIEW, &identity);
        device_->SetTransform(D3DTS_WORLD, &identity);
        device_->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
        device_->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
        device_->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
        device_->Clear(0, nullptr, D3DCLEAR_ZBUFFER, 0, 1.0f, 0);
    }

    void EndWeapon() override {
        if (!active_) return;
        SetOverlayState();
    }

    // ---- buy-menu previews (2D projection + depth write) ----

    void BeginPreview() override {
        if (!active_) return;
        // y-down ortho whose z maps the [0,1] depth fraction straight into the depth buffer.
        D3DMATRIX projection = OrthoOffCenter(0, float(width_), float(height_), 0, 0.0f, 1.0f);
        D3DMATRIX identity = IdentityMatrix();
        device_->SetTransform(D3DTS_PROJECTION, &projection);
        device_->SetTransform(D3DTS_VIEW, &identity);
        device_->SetTransform(D3DTS_WORLD, &identity);
        device_->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
        device_->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
        device_->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
        device_->Clear(0, nullptr, D3DCLEAR_ZBUFFER, 0, 1.0f, 0);
    }

    void EndPreview() override {
        if (!active_) return;
        device_->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
        device_->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    }

    // ---- sky ----

    struct SkySet { uintptr_t faces[6]; };

    uintptr_t CreateSky(const unsigned char* const* faces, int side) override {
        if (!device_ || !faces || side <= 0) return 0;
        SkySet* sky = new SkySet();
        for (int i = 0; i < 6; ++i) {
            sky->faces[i] = faces[i] ? CreateTexture(faces[i], side, side) : 0;
            if (!sky->faces[i]) { DestroySky(uintptr_t(sky)); return 0; }
        }
        return uintptr_t(sky);
    }

    void DrawSky(uintptr_t skyHandle, float yaw, float pitch, float fov, float aspect) override {
        if (!active_ || !skyHandle) return;
        SkySet* sky = reinterpret_cast<SkySet*>(skyHandle);
        constexpr float rad = 0.01745329252f;
        float y = yaw * rad, p = std::clamp(pitch, -89.0f, 89.0f) * rad;
        float forward[3] = {std::cos(p) * std::cos(y), std::cos(p) * std::sin(y), std::sin(p)};
        float right[3] = {std::sin(y), -std::cos(y), 0};
        float up[3] = {-std::sin(p) * std::cos(y), -std::sin(p) * std::sin(y), std::cos(p)};
        // The sky is a world-aligned box around the camera. Face textures follow the OpenGL
        // cube target order (rt/lf/bk/ft/up/dn -> +X/-X/+Z/-Z/+Y/-Y) and each face's (uAxis,
        // vAxis) is chosen so the box UVs reproduce the OpenGL cube mapping exactly, so the
        // D3D horizon joins the way the proven GL sky does. If a driver ever disagrees on
        // one face's handedness, flip that face's axis here rather than touching the shaders.
        const float H = 1000.0f;
        // Warcraft uses a horizontal camera FOV; vertical rays follow the window aspect ratio.
        // Off-center extents of horizontal*H / vertical*H give the box faces the same angular
        // size the GL sky's direction rays produce, so the horizon is neither stretched nor
        // squashed by the viewport shape.
        float horizontal = std::tan(fov * rad * 0.5f);
        float vertical = aspect > 0.0001f ? horizontal / aspect : horizontal;
        struct Face { float center[3]; float uAxis[3]; float vAxis[3]; uintptr_t texture; };
        const Face faces[6] = {
            {{H, 0, 0}, {0, 0, -1}, {0, -1, 0}, sky->faces[0]},   // +X: rt
            {{-H, 0, 0}, {0, 0, -1}, {0, -1, 0}, sky->faces[1]},  // -X: lf
            {{0, 0, H}, {1, 0, 0}, {0, -1, 0}, sky->faces[2]},    // +Z: bk
            {{0, 0, -H}, {1, 0, 0}, {0, 1, 0}, sky->faces[3]},    // -Z: ft
            {{0, H, 0}, {1, 0, 0}, {0, 0, 1}, sky->faces[4]},     // +Y: up
            {{0, -H, 0}, {-1, 0, 0}, {0, 0, 1}, sky->faces[5]},   // -Y: dn
        };
        D3DMATRIX projection = PerspectiveOffCenter(-horizontal * H, horizontal * H,
            -vertical * H, vertical * H, 0.1f, H * 4);
        D3DMATRIX identity = IdentityMatrix();
        device_->SetTransform(D3DTS_PROJECTION, &projection);
        device_->SetTransform(D3DTS_VIEW, &identity);
        device_->SetTransform(D3DTS_WORLD, &identity);
        device_->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
        device_->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
        device_->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
        device_->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        device_->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1);
        for (const Face& face : faces) {
            if (!face.texture) continue;
            V3 vertices[4];
            const float corners[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
            for (int i = 0; i < 4; ++i) {
                float world[3] = {
                    face.center[0] + face.uAxis[0] * corners[i][0] * H + face.vAxis[0] * corners[i][1] * H,
                    face.center[1] + face.uAxis[1] * corners[i][0] * H + face.vAxis[1] * corners[i][1] * H,
                    face.center[2] + face.uAxis[2] * corners[i][0] * H + face.vAxis[2] * corners[i][1] * H,
                };
                // Warcraft's Z-up world becomes the D3D +z view basis: right/up/forward.
                float cx = world[0] * right[0] + world[1] * right[1] + world[2] * right[2];
                float cy = world[0] * up[0] + world[1] * up[1] + world[2] * up[2];
                float cz = world[0] * forward[0] + world[1] * forward[1] + world[2] * forward[2];
                float u = 0.5f + corners[i][0] * 0.5f, v = 0.5f + corners[i][1] * 0.5f;
                vertices[i] = {cx, cy, cz, 0xFFFFFFFF, u, v};
            }
            StageTexture(reinterpret_cast<TTexture*>(face.texture));
            device_->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, vertices, sizeof(V3));
        }
        SetOverlayState();
    }

    void DestroySky(uintptr_t skyHandle) override {
        SkySet* sky = reinterpret_cast<SkySet*>(skyHandle);
        if (!sky) return;
        for (int i = 0; i < 6; ++i)
            if (sky->faces[i]) { TTexture* tex = reinterpret_cast<TTexture*>(sky->faces[i]); tex->Release(); }
        delete sky;
    }

    void ResetGraphics(bool deleteObjects) override {
        if (deleteObjects) {
            for (const auto& entry : sprites_) if (entry.second.texture) DestroyTexture(entry.second.texture);
        }
        sprites_.clear(); lru_.clear();
        ++generation_;
    }

    bool ResourcesValid() const override {
        return device_ != nullptr;
    }

    // Direct3D has no per-call error queue like glGetError; device results are sampled by the
    // runtime's own CreateDevice/Present hooks, so the canvas reports none.
    unsigned SampleError() const override { return 0; }
    // D3D canvases are keyed by device, not by an OpenGL context.
    bool ContextMatches(HGLRC) const override { return false; }

protected:
    struct V2C { float x, y, z, rhw; DWORD color; };
    struct V2 { float x, y, z, rhw; DWORD color; float u, v; };
    struct V3 { float x, y, z; DWORD color; float u, v; };
    struct TextSprite { uintptr_t texture = 0; int width = 0; int height = 0; };

    struct SavedState {
        TViewport viewport = {};
        DWORD renderStates[16] = {};
        DWORD stage0[8] = {};
        DWORD stage1[2] = {};
        DWORD sampler[5] = {};
        DWORD fvf = 0;
        void* vertexShader = nullptr;
        void* pixelShader = nullptr;
        void* vertexDeclaration = nullptr;
        D3DMATRIX world = {}, view = {}, projection = {};
        TSurface* renderTarget = nullptr;
        TSurface* depthStencil = nullptr;
        // GetTexture returns the base-texture pointer and adds its own reference, which the
        // matching GetTexture call in RestoreState releases after the slot is restored.
        IDirect3DBaseTexture9* texture = nullptr;
    };

    static DWORD Color(float r, float g, float b, float a) {
        auto byte = [](float v) { return DWORD(std::clamp(v, 0.f, 1.f) * 255.f + 0.5f); };
        return D3DCOLOR_ARGB(byte(a), byte(r), byte(g), byte(b));
    }

    static D3DMATRIX IdentityMatrix() {
        D3DMATRIX m = {};
        m._11 = m._22 = m._33 = m._44 = 1;
        return m;
    }

    static D3DMATRIX PerspectiveOffCenter(float left, float right, float bottom, float top,
        float nearPlane, float farPlane) {
        D3DMATRIX m = {};
        m._11 = 2 * nearPlane / (right - left);
        m._22 = 2 * nearPlane / (top - bottom);
        m._33 = farPlane / (farPlane - nearPlane);
        m._34 = 1;
        m._43 = -nearPlane * farPlane / (farPlane - nearPlane);
        return m;
    }

    static D3DMATRIX OrthoOffCenter(float left, float right, float bottom, float top,
        float nearPlane, float farPlane) {
        D3DMATRIX m = {};
        m._11 = 2 / (right - left);
        m._22 = 2 / (top - bottom);
        m._33 = 1 / (farPlane - nearPlane);
        m._41 = -(left + right) / (right - left);
        m._42 = -(top + bottom) / (top - bottom);
        m._43 = -nearPlane / (farPlane - nearPlane);
        m._44 = 1;
        return m;
    }

    void SetFVF(DWORD fvf) {
        if (fvf_ == fvf) return;
        fvf_ = fvf;
        device_->SetFVF(fvf);
    }

    // The untextured stage state (pure diffuse output).
    void StageDisabled() {
        device_->SetTexture(0, nullptr);
        device_->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_DISABLE);
        device_->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
        SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    }

    // Textured stage 0: modulate texture with vertex diffuse.
    void StageTexture(TTexture* texture) {
        device_->SetTexture(0, texture);
        device_->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
        device_->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
        device_->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
        device_->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
        device_->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
        device_->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
        device_->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
        device_->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
        SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);
    }

    template<typename TVertex>
    void Draw2D(D3DPRIMITIVETYPE type, const TVertex* vertices, int count) {
        UINT primitives = 0;
        switch (type) {
            case D3DPT_TRIANGLELIST: primitives = UINT(count / 3); break;
            case D3DPT_TRIANGLESTRIP:
            case D3DPT_TRIANGLEFAN: primitives = UINT(count - 2); break;
            case D3DPT_LINESTRIP: primitives = UINT(count - 1); break;
            default: return;
        }
        device_->DrawPrimitiveUP(type, primitives, vertices, UINT(sizeof(TVertex)));
    }

    void SetOverlayState() {
        fvf_ = 0;
        device_->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
        device_->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
        device_->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        device_->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        device_->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        device_->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
        device_->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        device_->SetRenderState(D3DRS_LIGHTING, FALSE);
        device_->SetRenderState(D3DRS_FOGENABLE, FALSE);
        device_->SetRenderState(D3DRS_STENCILENABLE, FALSE);
        device_->SetRenderState(D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN |
            D3DCOLORWRITEENABLE_BLUE | D3DCOLORWRITEENABLE_ALPHA);
        device_->SetRenderState(D3DRS_SHADEMODE, D3DSHADE_GOURAUD);
        device_->SetRenderState(D3DRS_COLORVERTEX, TRUE);
        device_->SetRenderState(D3DRS_DIFFUSEMATERIALSOURCE, D3DMCS_COLOR1);
        device_->SetRenderState(D3DRS_SPECULARENABLE, FALSE);
        device_->SetRenderState(D3DRS_CLIPPLANEENABLE, 0);
        // Stage 0 modulates texture with diffuse; stage 1 stays disabled.
        device_->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
        device_->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
        device_->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
        device_->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
        device_->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
        device_->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
        device_->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
        device_->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
        device_->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
        device_->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
        device_->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
        device_->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
        device_->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_POINT);
        device_->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
        device_->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
        Traits::SetVertexShader(device_, nullptr);
        Traits::SetPixelShader(device_, nullptr);
        Traits::SetVertexDeclaration(device_, nullptr);
    }

    void SaveState() {
        SavedState state = {};
        device_->GetViewport(&state.viewport);
        device_->GetRenderState(D3DRS_ZENABLE, &state.renderStates[0]);
        device_->GetRenderState(D3DRS_ZWRITEENABLE, &state.renderStates[1]);
        device_->GetRenderState(D3DRS_ZFUNC, &state.renderStates[2]);
        device_->GetRenderState(D3DRS_ALPHABLENDENABLE, &state.renderStates[3]);
        device_->GetRenderState(D3DRS_SRCBLEND, &state.renderStates[4]);
        device_->GetRenderState(D3DRS_DESTBLEND, &state.renderStates[5]);
        device_->GetRenderState(D3DRS_ALPHATESTENABLE, &state.renderStates[6]);
        device_->GetRenderState(D3DRS_ALPHAREF, &state.renderStates[7]);
        device_->GetRenderState(D3DRS_ALPHAFUNC, &state.renderStates[8]);
        device_->GetRenderState(D3DRS_CULLMODE, &state.renderStates[9]);
        device_->GetRenderState(D3DRS_LIGHTING, &state.renderStates[10]);
        device_->GetRenderState(D3DRS_FOGENABLE, &state.renderStates[11]);
        device_->GetRenderState(D3DRS_STENCILENABLE, &state.renderStates[12]);
        device_->GetRenderState(D3DRS_COLORWRITEENABLE, &state.renderStates[13]);
        device_->GetRenderState(D3DRS_SHADEMODE, &state.renderStates[14]);
        device_->GetRenderState(D3DRS_SPECULARENABLE, &state.renderStates[15]);
        device_->GetTextureStageState(0, D3DTSS_COLOROP, &state.stage0[0]);
        device_->GetTextureStageState(0, D3DTSS_COLORARG1, &state.stage0[1]);
        device_->GetTextureStageState(0, D3DTSS_COLORARG2, &state.stage0[2]);
        device_->GetTextureStageState(0, D3DTSS_ALPHAOP, &state.stage0[3]);
        device_->GetTextureStageState(0, D3DTSS_ALPHAARG1, &state.stage0[4]);
        device_->GetTextureStageState(0, D3DTSS_ALPHAARG2, &state.stage0[5]);
        device_->GetTextureStageState(0, D3DTSS_TEXCOORDINDEX, &state.stage0[6]);
        device_->GetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, &state.stage0[7]);
        device_->GetTextureStageState(1, D3DTSS_COLOROP, &state.stage1[0]);
        device_->GetTextureStageState(1, D3DTSS_ALPHAOP, &state.stage1[1]);
        device_->GetSamplerState(0, D3DSAMP_MAGFILTER, &state.sampler[0]);
        device_->GetSamplerState(0, D3DSAMP_MINFILTER, &state.sampler[1]);
        device_->GetSamplerState(0, D3DSAMP_MIPFILTER, &state.sampler[2]);
        device_->GetSamplerState(0, D3DSAMP_ADDRESSU, &state.sampler[3]);
        device_->GetSamplerState(0, D3DSAMP_ADDRESSV, &state.sampler[4]);
        device_->GetFVF(&state.fvf);
        state.vertexShader = Traits::GetVertexShader(device_);
        state.pixelShader = Traits::GetPixelShader(device_);
        state.vertexDeclaration = Traits::GetVertexDeclaration(device_);
        device_->GetTransform(D3DTS_WORLD, &state.world);
        device_->GetTransform(D3DTS_VIEW, &state.view);
        device_->GetTransform(D3DTS_PROJECTION, &state.projection);
        state.renderTarget = nullptr;
        state.depthStencil = nullptr;
        device_->GetRenderTarget(0, &state.renderTarget);
        device_->GetDepthStencilSurface(&state.depthStencil);
        state.texture = nullptr;
        device_->GetTexture(0, &state.texture);
        saved_ = state;
    }

    void RestoreState() {
        const SavedState& state = saved_;
        device_->SetViewport(&state.viewport);
        device_->SetRenderState(D3DRS_ZENABLE, state.renderStates[0]);
        device_->SetRenderState(D3DRS_ZWRITEENABLE, state.renderStates[1]);
        device_->SetRenderState(D3DRS_ZFUNC, state.renderStates[2]);
        device_->SetRenderState(D3DRS_ALPHABLENDENABLE, state.renderStates[3]);
        device_->SetRenderState(D3DRS_SRCBLEND, state.renderStates[4]);
        device_->SetRenderState(D3DRS_DESTBLEND, state.renderStates[5]);
        device_->SetRenderState(D3DRS_ALPHATESTENABLE, state.renderStates[6]);
        device_->SetRenderState(D3DRS_ALPHAREF, state.renderStates[7]);
        device_->SetRenderState(D3DRS_ALPHAFUNC, state.renderStates[8]);
        device_->SetRenderState(D3DRS_CULLMODE, state.renderStates[9]);
        device_->SetRenderState(D3DRS_LIGHTING, state.renderStates[10]);
        device_->SetRenderState(D3DRS_FOGENABLE, state.renderStates[11]);
        device_->SetRenderState(D3DRS_STENCILENABLE, state.renderStates[12]);
        device_->SetRenderState(D3DRS_COLORWRITEENABLE, state.renderStates[13]);
        device_->SetRenderState(D3DRS_SHADEMODE, state.renderStates[14]);
        device_->SetRenderState(D3DRS_SPECULARENABLE, state.renderStates[15]);
        device_->SetTextureStageState(0, D3DTSS_COLOROP, state.stage0[0]);
        device_->SetTextureStageState(0, D3DTSS_COLORARG1, state.stage0[1]);
        device_->SetTextureStageState(0, D3DTSS_COLORARG2, state.stage0[2]);
        device_->SetTextureStageState(0, D3DTSS_ALPHAOP, state.stage0[3]);
        device_->SetTextureStageState(0, D3DTSS_ALPHAARG1, state.stage0[4]);
        device_->SetTextureStageState(0, D3DTSS_ALPHAARG2, state.stage0[5]);
        device_->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, state.stage0[6]);
        device_->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, state.stage0[7]);
        device_->SetTextureStageState(1, D3DTSS_COLOROP, state.stage1[0]);
        device_->SetTextureStageState(1, D3DTSS_ALPHAOP, state.stage1[1]);
        device_->SetSamplerState(0, D3DSAMP_MAGFILTER, state.sampler[0]);
        device_->SetSamplerState(0, D3DSAMP_MINFILTER, state.sampler[1]);
        device_->SetSamplerState(0, D3DSAMP_MIPFILTER, state.sampler[2]);
        device_->SetSamplerState(0, D3DSAMP_ADDRESSU, state.sampler[3]);
        device_->SetSamplerState(0, D3DSAMP_ADDRESSV, state.sampler[4]);
        device_->SetFVF(state.fvf);
        Traits::SetVertexShader(device_, state.vertexShader);
        Traits::SetPixelShader(device_, state.pixelShader);
        if (state.vertexDeclaration) Traits::SetVertexDeclaration(device_, state.vertexDeclaration);
        device_->SetTransform(D3DTS_WORLD, &state.world);
        device_->SetTransform(D3DTS_VIEW, &state.view);
        device_->SetTransform(D3DTS_PROJECTION, &state.projection);
        device_->SetTexture(0, state.texture);
        if (state.texture) state.texture->Release();
        if (state.renderTarget) { device_->SetRenderTarget(0, state.renderTarget); state.renderTarget->Release(); }
        if (state.depthStencil) { device_->SetDepthStencilSurface(state.depthStencil); state.depthStencil->Release(); }
    }

    TextSprite* Sprite(const char* text, int size) {
        std::string key = std::to_string(size) + "\n" + text;
        auto found = sprites_.find(key);
        if (found != sprites_.end()) {
            // Move the entry to the back of the LRU list.
            lru_.splice(lru_.end(), lru_, found->second.lru);
            return &found->second;
        }
        if (sprites_.size() >= 128) {
            const std::string& oldest = lru_.front();
            auto entry = sprites_.find(oldest);
            if (entry != sprites_.end()) {
                if (entry->second.texture) DestroyTexture(entry->second.texture);
                sprites_.erase(entry);
            }
            lru_.pop_front();
        }
        SpriteEntry sprite;
        if (!Rasterize(text, size, sprite)) return nullptr;
        std::string stored = key;
        sprite.lru = lru_.insert(lru_.end(), stored);
        auto result = sprites_.emplace(std::move(key), sprite);
        return &result.first->second;
    }

    bool Rasterize(const char* text, int size, TextSprite& sprite) {
        HDC mem = CreateCompatibleDC(nullptr);
        if (!mem) return false;
        HFONT font = CreateFontA(size, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, ANSI_CHARSET,
            OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, FF_DONTCARE, "Consolas");
        HGDIOBJ oldFont = SelectObject(mem, font);
        SetTextColor(mem, RGB(255, 255, 255)); SetBkMode(mem, TRANSPARENT);
        int length = int(strlen(text));
        SIZE extent = {};
        GetTextExtentPoint32A(mem, text, length, &extent);
        int width = extent.cx + 2, height = extent.cy + 2;
        if (width <= 0 || height <= 0 || width > 2048 || height > 2048) {
            SelectObject(mem, oldFont); DeleteObject(font); DeleteDC(mem); return false;
        }
        BITMAPINFO info = {};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height; // top-down
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        HBITMAP dib = CreateDIBSection(mem, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!dib || !bits) {
            if (dib) DeleteObject(dib);
            SelectObject(mem, oldFont); DeleteObject(font); DeleteDC(mem); return false;
        }
        HGDIOBJ oldBitmap = SelectObject(mem, dib);
        memset(bits, 0, size_t(width) * height * 4);
        TextOutA(mem, 1, 1, text, length);
        // Convert while the DIB section is still alive: deleting it frees the bit pointer.
        std::vector<unsigned char> rgba(size_t(width) * height * 4);
        const unsigned char* src = static_cast<const unsigned char*>(bits);
        for (int i = 0; i < width * height; ++i) {
            // GDI painted antialiased grayscale coverage over transparent black; keep it as
            // white glyphs with alpha coverage so Text can tint the sprite at draw time.
            unsigned coverage = std::max({src[i * 4 + 0], src[i * 4 + 1], src[i * 4 + 2]});
            rgba[i * 4 + 0] = 255; rgba[i * 4 + 1] = 255; rgba[i * 4 + 2] = 255;
            rgba[i * 4 + 3] = static_cast<unsigned char>(coverage);
        }
        SelectObject(mem, oldBitmap); DeleteObject(dib);
        SelectObject(mem, oldFont); DeleteObject(font); DeleteDC(mem);
        sprite.texture = CreateTexture(rgba.data(), width, height);
        if (!sprite.texture) return false;
        sprite.width = width; sprite.height = height;
        return true;
    }

    TDevice* device_ = nullptr;
    SavedState saved_;
    bool active_ = false;
    bool lostLogged_ = false;
    int width_ = 0, height_ = 0;
    DWORD fvf_ = 0;
    mutable int glyphWidth_ = 0;
    uint64_t generation_ = 1;
    struct SpriteEntry : TextSprite { std::list<std::string>::iterator lru; };
    std::unordered_map<std::string, SpriteEntry> sprites_;
    std::list<std::string> lru_;
};
