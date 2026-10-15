#pragma once
#include <windows.h>
#include <cstdint>

// Backend-neutral drawing surface behind the FPS overlay. Views (reticle, scope, health
// bars, spell icons, buy menu, weapon, sky) draw through this interface so the runtime can
// present the same HUD on OpenGL or Direct3D 9 without duplicating layout or composition
// logic. Each backend owns its GPU resources and its own frame isolation.
class Canvas {
public:
    virtual ~Canvas() = default;
    virtual const char* Name() const = 0;
    virtual bool IsOpenGL() const { return false; }
    // A generation increments whenever the rendering context/device is replaced; owners of
    // canvas textures must forget (not delete) their handles when it changes.
    virtual uint64_t Generation() const = 0;

    // Opens a 2D overlay frame: isolates backend state and leaves a pixel-projection where
    // (0,0) is the top-left, x grows right and y grows down. The caller restores nothing
    // itself; EndFrame restores every touched backend state. Returns false when the
    // context/device is not usable (the caller skips the frame).
    virtual bool BeginFrame(HDC dc, int width, int height) = 0;
    virtual void EndFrame() = 0;

    // 2D primitives (y-down pixels, alpha blended over the world).
    virtual void FillRect(float x, float y, float w, float h, float r, float g, float b, float a) = 0;
    virtual void OutlineRect(float x, float y, float w, float h, float r, float g, float b, float a) = 0;
    virtual void Line(float x0, float y0, float x1, float y1, float thickness, float r, float g, float b, float a) = 0;
    virtual void TriangleStrip(const float* xy, int count, float r, float g, float b, float a) = 0;
    virtual void TriangleFan(const float* xy, int count, float r, float g, float b, float a) = 0;

    // Monospace bold text; size is the pixel height. GlyphWidth reports the cached advance
    // of the default hint font used for wrapping (backend-dependent, as today).
    virtual void Text(float x, float y, const char* text, int size, float r, float g, float b, float a) = 0;
    virtual int GlyphWidth(int size) const = 0;

    // Textures: opaque handles created from tightly packed RGBA pixels. Handle validity is
    // only meaningful in the same device/context that created it.
    virtual uintptr_t CreateTexture(const unsigned char* rgba, int width, int height) = 0;
    virtual void DestroyTexture(uintptr_t texture) = 0;
    virtual bool TextureValid(uintptr_t texture) const = 0;
    virtual void TextureQuad(uintptr_t texture, float x, float y, float w, float h,
        float r, float g, float b, float a, bool flipVertical = false) = 0;
    // Untransformed 3D textured triangles under the projection installed by BeginWeapon.
    // xyzuv is interleaved x,y,z,u,v per vertex in the OpenGL camera convention (looking
    // down -z); non-OpenGL backends mirror z into their own projection.
    virtual void TexturedTriangles(uintptr_t texture, const float* xyzuv, int vertexCount,
        float r, float g, float b, float a) = 0;
    // 2D textured triangles for buy-menu previews: x,y are pixels (y-down) and z is the
    // [0,1] depth fraction used only to resolve the preview's own faces. It never touches
    // the world depth and must not be handed to TexturedTriangles.
    virtual void PreviewTriangles(uintptr_t texture, const float* xyzuv, int vertexCount,
        float r, float g, float b, float a) = 0;

    // World-depth pass for health bars: the world depth buffer is intact, so bars drawn at
    // the [0,1] perspective depth fraction (0 near / 1 far) are occluded by opaque terrain.
    virtual void BeginBars(int width, int height) = 0;
    virtual void BarQuad(float x, float y, float w, float h, float z, float r, float g, float b, float a) = 0;
    virtual void EndBars() = 0;

    // First-person weapon pass: perspective projection and a private depth clear.
    virtual void BeginWeapon(int width, int height, float projectionScale, float nearPlane, float farPlane) = 0;
    virtual void EndWeapon() = 0;

    // Depth write pass used by buy-menu previews (keeps the 2D projection, clears depth).
    virtual void BeginPreview() = 0;
    virtual void EndPreview() = 0;

    // Sky: created from the six GoldSrc faces (rt/lf/bk/ft/up/dn), `side` pixels each,
    // tightly packed RGBA. The backend decides its own texture representation.
    virtual uintptr_t CreateSky(const unsigned char* const* faces, int side) = 0;
    virtual void DrawSky(uintptr_t sky, float yaw, float pitch, float fov, float aspect) = 0;
    virtual void DestroySky(uintptr_t sky) = 0;

    // Resource lifecycle. ResetGraphics(false) forgets handles without deleting (the
    // context/device was replaced); true deletes owned resources (device loss or teardown).
    virtual void ResetGraphics(bool deleteObjects) = 0;
    // True when fonts/textures created in this context still exist there.
    virtual bool ResourcesValid() const = 0;

    // Backend error sampling for frame diagnostics; 0 means no error observed. The OpenGL
    // backend drains glGetError (its context is current during the overlay), while Direct3D
    // backends have no error queue and report 0; the runtime's own hooks sample D3D results.
    virtual unsigned SampleError() const { return 0; }
    // True when the given OpenGL context is the one this canvas draws into. Used by the
    // runtime's wglDeleteContext hook so only the active GL canvas forgets its resources.
    virtual bool ContextMatches(HGLRC) const { return false; }
};
