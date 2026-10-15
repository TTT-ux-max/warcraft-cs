#pragma once
#include "Canvas.hpp"
#include <gl/GL.h>
#include <map>

// OpenGL backend. Every GL call below mirrors the previous immediate-mode overlay exactly
// (state isolation, texture-unit handling, display-list bitmap fonts and restore order), so
// the proven 1.26a/1.27a OpenGL path keeps its behavior while the rest of the HUD moves to
// the shared Canvas contract.
class GlCanvas final : public Canvas {
public:
    const char* Name() const override { return "opengl"; }
    bool IsOpenGL() const override { return true; }
    uint64_t Generation() const override { return generation_; }

    bool BeginFrame(HDC dc, int width, int height) override;
    void EndFrame() override;

    void FillRect(float x, float y, float w, float h, float r, float g, float b, float a) override;
    void OutlineRect(float x, float y, float w, float h, float r, float g, float b, float a) override;
    void Line(float x0, float y0, float x1, float y1, float thickness, float r, float g, float b, float a) override;
    void TriangleStrip(const float* xy, int count, float r, float g, float b, float a) override;
    void TriangleFan(const float* xy, int count, float r, float g, float b, float a) override;
    void Text(float x, float y, const char* text, int size, float r, float g, float b, float a) override;
    int GlyphWidth(int size) const override;

    uintptr_t CreateTexture(const unsigned char* rgba, int width, int height) override;
    void DestroyTexture(uintptr_t texture) override;
    bool TextureValid(uintptr_t texture) const override;
    void TextureQuad(uintptr_t texture, float x, float y, float w, float h,
        float r, float g, float b, float a, bool flipVertical) override;
    void TexturedTriangles(uintptr_t texture, const float* xyzuv, int vertexCount,
        float r, float g, float b, float a) override;
    void PreviewTriangles(uintptr_t texture, const float* xyzuv, int vertexCount,
        float r, float g, float b, float a) override;

    void BeginBars(int width, int height) override;
    void BarQuad(float x, float y, float w, float h, float z, float r, float g, float b, float a) override;
    void EndBars() override;

    void BeginWeapon(int width, int height, float projectionScale, float nearPlane, float farPlane) override;
    void EndWeapon() override;

    void BeginPreview() override;
    void EndPreview() override;

    uintptr_t CreateSky(const unsigned char* const* faces, int side) override;
    void DrawSky(uintptr_t sky, float yaw, float pitch, float fov, float aspect) override;
    void DestroySky(uintptr_t sky) override;

    void ResetGraphics(bool deleteObjects) override;
    bool ResourcesValid() const override;
    unsigned SampleError() const override;
    bool ContextMatches(HGLRC context) const override;

    // The runtime forwards native context deletion so numeric names are forgotten, never
    // deleted, after the driver has already destroyed the context.
    void ContextDeleted(HGLRC context);

private:
    GLuint Font(int size);
    void Restore2D();
    HGLRC context_ = nullptr;
    HDC dc_ = nullptr;
    int width_ = 0, height_ = 0;
    GLint oldMode_ = GL_MODELVIEW;
    GLint oldTexture_ = 0x84C0;
    bool activeTexture_ = false;
    std::map<int, GLuint> fonts_;
    int hintGlyphWidth_ = 12;
    uint64_t generation_ = 1;
};
