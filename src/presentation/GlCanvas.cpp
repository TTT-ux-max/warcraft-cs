#include "GlCanvas.hpp"
#include "../platform/WarcraftApi.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
constexpr GLenum CubeTarget = 0x8513;
// GoldSrc face order rt/lf/bk/ft/up/dn maps onto OpenGL cube targets X/-X/Z/-Z/Y/-Y;
// SkyView rotates the up/down faces before upload, so only the target order remains here.
const GLenum CubeFaces[6] = {0x8515, 0x8516, 0x8519, 0x851A, 0x8517, 0x8518};
// OpenGL cube maps use X/Y/Z with Y as vertical; SkyView feeds already-rotated pixels.
using ActiveTextureProc = void (APIENTRY*)(GLenum);
ActiveTextureProc ActiveTexture() {
    static ActiveTextureProc proc = nullptr;
    if (!proc) {
        proc = reinterpret_cast<ActiveTextureProc>(wglGetProcAddress("glActiveTextureARB"));
        if (!proc) proc = reinterpret_cast<ActiveTextureProc>(wglGetProcAddress("glActiveTexture"));
    }
    return proc;
}
void IsolateTextureUnits(GLint& oldTexture) {
    auto activeTexture = ActiveTexture();
    oldTexture = 0x84C0;
    if (!activeTexture) return;
    GLint units = 1;
    glGetIntegerv(0x84C0, &oldTexture); glGetIntegerv(0x84E2, &units);
    for (int unit = 0; unit < units; ++unit) {
        activeTexture(0x84C0 + unit);
        glDisable(GL_TEXTURE_1D); glDisable(GL_TEXTURE_2D);
        glDisable(GL_TEXTURE_GEN_S); glDisable(GL_TEXTURE_GEN_T);
    }
    activeTexture(0x84C0);
}
void RestoreTextureUnits(GLint oldTexture) {
    auto activeTexture = ActiveTexture();
    if (activeTexture) activeTexture(oldTexture);
}
}

GLuint GlCanvas::Font(int size) {
    auto found = fonts_.find(size);
    if (found != fonts_.end()) return found->second;
    // Rasterize each size directly so enlarged status text stays sharp instead of stretching pixels.
    if (fonts_.find(20) == fonts_.end()) {
        HFONT font = CreateFontA(20, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, ANSI_CHARSET,
            OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, FF_DONTCARE, "Consolas");
        HGDIOBJ old = SelectObject(dc_, font);
        GLuint lists = glGenLists(96);
        // Use the actual monospace advance so wrapping remains correct at different Windows DPI settings.
        TEXTMETRICA metrics = {}; GetTextMetricsA(dc_, &metrics);
        hintGlyphWidth_ = std::max(1, int(metrics.tmAveCharWidth));
        wglUseFontBitmapsA(dc_, 32, 96, lists);
        SelectObject(dc_, old); DeleteObject(font);
        fonts_[20] = lists;
    }
    HFONT font = CreateFontA(size, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, ANSI_CHARSET,
        OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, FF_DONTCARE, "Consolas");
    HGDIOBJ old = SelectObject(dc_, font);
    GLuint lists = glGenLists(96);
    wglUseFontBitmapsA(dc_, 32, 96, lists);
    SelectObject(dc_, old); DeleteObject(font);
    fonts_[size] = lists;
    return lists;
}

void GlCanvas::Text(float x, float y, const char* text, int size, float r, float g, float b, float a) {
    glColor4f(r, g, b, a);
    glRasterPos2f(x, y);
    GLuint font = Font(size);
    glListBase(font - 32);
    glCallLists(GLsizei(strlen(text)), GL_UNSIGNED_BYTE, text);
}

void GlCanvas::Restore2D() {
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0, float(width_), float(height_), 0, -1, 1);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity(); glDisable(GL_TEXTURE_2D);
}

bool GlCanvas::BeginFrame(HDC dc, int width, int height) {
    if (!wglGetCurrentContext()) return false;
    HGLRC current = wglGetCurrentContext();
    if (context_ != current) {
        ResetGraphics(false); context_ = current;
        wc3::Log("Overlay context acquired: %p", current);
        // Record the actual driver renderer for reports where the overlay initializes but primitives disappear.
        wc3::Log("Overlay GL vendor=%s renderer=%s version=%s lineStipple=%d",
            glGetString(GL_VENDOR), glGetString(GL_RENDERER), glGetString(GL_VERSION), glIsEnabled(GL_LINE_STIPPLE));
    }
    dc_ = dc; width_ = width; height_ = height;
    // Preserve every GL state touched so Warcraft's next frame renders normally.
    glGetIntegerv(GL_MATRIX_MODE, &oldMode_); glPushAttrib(GL_ALL_ATTRIB_BITS);
    glViewport(0, 0, width, height);
    // Warcraft uses multiple texture units; isolate the overlay on unit zero.
    IsolateTextureUnits(oldTexture_);
    glMatrixMode(GL_TEXTURE); glPushMatrix(); glLoadIdentity();
    glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glDisable(GL_LIGHTING); glDisable(GL_FOG);
    glDisable(GL_SCISSOR_TEST); glEnable(GL_ALPHA_TEST); glAlphaFunc(GL_GREATER, 0.1f);
    // Warcraft leaves stencil/clip state active; HUD primitives must not inherit its world masks.
    glDisable(GL_STENCIL_TEST);
    for (int plane = 0; plane < 6; ++plane) glDisable(GL_CLIP_PLANE0 + plane);
    glDisable(GL_TEXTURE_GEN_S); glDisable(GL_TEXTURE_GEN_T);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    Restore2D();
    return true;
}

void GlCanvas::EndFrame() {
    glPopMatrix(); // modelview
    glMatrixMode(GL_PROJECTION); glPopMatrix();
    glMatrixMode(GL_TEXTURE); glPopMatrix();
    glMatrixMode(oldMode_); glPopAttrib();
    RestoreTextureUnits(oldTexture_);
}

void GlCanvas::FillRect(float x, float y, float w, float h, float r, float g, float b, float a) {
    glColor4f(r, g, b, a);
    glBegin(GL_QUADS);
    glVertex2f(x, y); glVertex2f(x + w, y); glVertex2f(x + w, y + h); glVertex2f(x, y + h);
    glEnd();
}

void GlCanvas::OutlineRect(float x, float y, float w, float h, float r, float g, float b, float a) {
    glLineWidth(1);
    glColor4f(r, g, b, a);
    glBegin(GL_LINE_LOOP);
    glVertex2f(x, y); glVertex2f(x + w, y); glVertex2f(x + w, y + h); glVertex2f(x, y + h);
    glEnd();
}

void GlCanvas::Line(float x0, float y0, float x1, float y1, float thickness, float r, float g, float b, float a) {
    glLineWidth(thickness);
    glColor4f(r, g, b, a);
    glBegin(GL_LINES);
    glVertex2f(x0, y0); glVertex2f(x1, y1);
    glEnd();
}

void GlCanvas::TriangleStrip(const float* xy, int count, float r, float g, float b, float a) {
    glColor4f(r, g, b, a);
    glBegin(GL_QUAD_STRIP);
    for (int i = 0; i < count; ++i) glVertex2f(xy[i * 2], xy[i * 2 + 1]);
    glEnd();
}

void GlCanvas::TriangleFan(const float* xy, int count, float r, float g, float b, float a) {
    glColor4f(r, g, b, a);
    glBegin(GL_TRIANGLE_FAN);
    for (int i = 0; i < count; ++i) glVertex2f(xy[i * 2], xy[i * 2 + 1]);
    glEnd();
}

int GlCanvas::GlyphWidth(int size) const {
    // The wrap column is measured once on the 20px hint font, exactly like the previous overlay.
    (void)size;
    return hintGlyphWidth_;
}

uintptr_t GlCanvas::CreateTexture(const unsigned char* rgba, int width, int height) {
    GLuint texture = 0;
    glGenTextures(1, &texture); glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, 0x812F);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, 0x812F);
    // Retail RGBA data is tightly packed; Warcraft's pixel upload stride must not leak in.
    glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1); glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0); glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glPopClientAttrib();
    return texture;
}

void GlCanvas::DestroyTexture(uintptr_t texture) {
    GLuint name = GLuint(texture);
    if (name) glDeleteTextures(1, &name);
}

bool GlCanvas::TextureValid(uintptr_t texture) const {
    return texture != 0 && glIsTexture(GLuint(texture)) == GL_TRUE;
}

void GlCanvas::TextureQuad(uintptr_t texture, float x, float y, float w, float h,
    float r, float g, float b, float a, bool flipVertical) {
    glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, GLuint(texture));
    glColor4f(r, g, b, a);
    float u0 = 0, v0 = flipVertical ? 1.f : 0.f, u1 = 1, v1 = flipVertical ? 0.f : 1.f;
    glBegin(GL_QUADS);
    glTexCoord2f(u0, v0); glVertex2f(x, y);
    glTexCoord2f(u1, v0); glVertex2f(x + w, y);
    glTexCoord2f(u1, v1); glVertex2f(x + w, y + h);
    glTexCoord2f(u0, v1); glVertex2f(x, y + h);
    glEnd();
    glDisable(GL_TEXTURE_2D);
}

void GlCanvas::TexturedTriangles(uintptr_t texture, const float* xyzuv, int vertexCount,
    float r, float g, float b, float a) {
    glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, GLuint(texture));
    glColor4f(r, g, b, a);
    glBegin(GL_TRIANGLES);
    for (int i = 0; i < vertexCount; ++i) {
        const float* v = xyzuv + i * 5;
        glTexCoord2f(v[3], v[4]); glVertex3f(v[0], v[1], v[2]);
    }
    glEnd();
    glDisable(GL_TEXTURE_2D);
}

void GlCanvas::PreviewTriangles(uintptr_t texture, const float* xyzuv, int vertexCount,
    float r, float g, float b, float a) {
    glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, GLuint(texture));
    glColor4f(r, g, b, a);
    glBegin(GL_TRIANGLES);
    for (int i = 0; i < vertexCount; ++i) {
        const float* v = xyzuv + i * 5;
        // The HUD ortho spans z [-1,1] mapped to depth [0,1]; the [0,1] fraction maps back.
        glTexCoord2f(v[3], v[4]); glVertex3f(v[0], v[1], 2 * v[2] - 1);
    }
    glEnd();
    glDisable(GL_TEXTURE_2D);
}

void GlCanvas::BeginBars(int width, int height) {
    (void)width; (void)height; // The 2D ortho installed by BeginFrame is already current.
    glDisable(GL_TEXTURE_2D); glDisable(GL_LIGHTING); glDisable(GL_FOG); glDisable(GL_CULL_FACE);
    glDisable(GL_ALPHA_TEST); glDisable(GL_SCISSOR_TEST); glDisable(GL_STENCIL_TEST); glDisable(GL_BLEND);
    glDisable(GL_POLYGON_STIPPLE); glDisable(GL_COLOR_LOGIC_OP); glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    for (int plane = 0; plane < 6; ++plane) glDisable(GL_CLIP_PLANE0 + plane);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE); glDepthRange(0, 1);
}

void GlCanvas::BarQuad(float x, float y, float w, float h, float z, float r, float g, float b, float a) {
    // glOrtho(-1..1) plus glDepthRange(0,1) maps vertex z=1-2z to the [0,1] depth fraction z.
    glColor4f(r, g, b, a);
    glBegin(GL_QUADS);
    glVertex3f(x, y, 1 - 2 * z); glVertex3f(x + w, y, 1 - 2 * z);
    glVertex3f(x + w, y + h, 1 - 2 * z); glVertex3f(x, y + h, 1 - 2 * z);
    glEnd();
}

void GlCanvas::EndBars() {
    glDisable(GL_DEPTH_TEST); glDepthMask(GL_TRUE); glDepthRange(0, 1);
    // Re-assert the overlay 2D state BeginFrame established.
    glEnable(GL_ALPHA_TEST); glAlphaFunc(GL_GREATER, 0.1f);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void GlCanvas::BeginWeapon(int width, int height, float projectionScale, float nearPlane, float farPlane) {
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    // Slightly tighten USP/AWP framing while preserving the original lower-edge arm origins.
    float top = std::tan(45.0f * 0.01745329252f) * projectionScale;
    float side = top * float(width) / float(height);
    glFrustum(-side, side, -top, top, nearPlane, farPlane);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    // Give the weapon its own depth pass: back faces must not paint over the skin.
    glDepthMask(GL_TRUE); glClearDepth(1); glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL); glDepthRange(0, 1);
}

void GlCanvas::EndWeapon() {
    glDisable(GL_DEPTH_TEST);
    Restore2D();
}

void GlCanvas::BeginPreview() {
    glPushAttrib(GL_ENABLE_BIT | GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT | GL_TEXTURE_BIT | GL_CURRENT_BIT);
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL); glDepthMask(GL_TRUE);
    glDepthRange(0, 1); glClearDepth(1); glClear(GL_DEPTH_BUFFER_BIT);
    glColor4f(1, 1, 1, 1);
}

void GlCanvas::EndPreview() {
    glPopAttrib();
}

uintptr_t GlCanvas::CreateSky(const unsigned char* const* faces, int side) {
    GLuint texture = 0;
    glGenTextures(1, &texture); glBindTexture(CubeTarget, texture);
    glTexParameteri(CubeTarget, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(CubeTarget, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(CubeTarget, GL_TEXTURE_WRAP_S, 0x812F);
    glTexParameteri(CubeTarget, GL_TEXTURE_WRAP_T, 0x812F);
    glTexParameteri(CubeTarget, 0x8072, 0x812F); // GL_TEXTURE_WRAP_R
    for (int face = 0; face < 6; ++face) {
        // Warcraft can retain unpack stride/skips after Alt-Tab; tightly packed buffers must never inherit them.
        glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1); glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0); glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
        glTexImage2D(CubeFaces[face], 0, GL_RGBA, side, side, 0, GL_RGBA, GL_UNSIGNED_BYTE, faces[face]);
        glPopClientAttrib();
    }
    return texture;
}

void GlCanvas::DrawSky(uintptr_t sky, float yaw, float pitch, float fov, float aspect) {
    // Matrix stacks are private; the caller isolates all GL attributes/texture units from Warcraft.
    glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
    glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
    if (!sky) { glPopMatrix(); glMatrixMode(GL_MODELVIEW); glPopMatrix(); return; }
    glDisable(GL_TEXTURE_2D); glEnable(CubeTarget); glBindTexture(CubeTarget, GLuint(sky)); glColor4f(1, 1, 1, 1);
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL); glDepthMask(GL_FALSE); glDepthRange(1, 1);
    constexpr float rad = 0.01745329252f;
    float y = yaw * rad, p = std::clamp(pitch, -89.0f, 89.0f) * rad;
    float forward[] = {std::cos(p) * std::cos(y), std::cos(p) * std::sin(y), std::sin(p)};
    float right[] = {std::sin(y), -std::cos(y), 0};
    float up[] = {-std::sin(p) * std::cos(y), -std::sin(p) * std::sin(y), std::cos(p)};
    // Warcraft uses a horizontal camera FOV; vertical rays follow the window aspect ratio.
    float horizontal = std::tan(fov * rad * 0.5f), vertical = horizontal / aspect;
    const float corners[][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
    glBegin(GL_QUADS);
    for (auto& corner : corners) {
        float direction[3];
        for (int i = 0; i < 3; ++i)
            direction[i] = forward[i] + right[i] * corner[0] * horizontal + up[i] * corner[1] * vertical;
        // Warcraft's Z-up world must become the cube's Y-up frame, keeping every horizon horizontal.
        glTexCoord3f(direction[0], direction[2], -direction[1]);
        glVertex3f(corner[0], corner[1], 1);
    }
    glEnd();
    glDisable(CubeTarget); glDisable(GL_DEPTH_TEST); glDepthMask(GL_TRUE); glDepthRange(0, 1);
    glPopMatrix(); glMatrixMode(GL_MODELVIEW); glPopMatrix();
}

void GlCanvas::DestroySky(uintptr_t sky) {
    GLuint name = GLuint(sky);
    if (name) glDeleteTextures(1, &name);
}

void GlCanvas::ResetGraphics(bool deleteObjects) {
    if (deleteObjects) {
        for (const auto& entry : fonts_) glDeleteLists(entry.second, 96);
    }
    fonts_.clear(); hintGlyphWidth_ = 12;
    ++generation_;
}

bool GlCanvas::ResourcesValid() const {
    if (!context_) return false;
    for (const auto& entry : fonts_)
        if (glIsList(entry.second) != GL_TRUE) return false;
    return true;
}

unsigned GlCanvas::SampleError() const {
    // Only meaningful with a current context; the runtime calls it inside the overlay frame.
    return wglGetCurrentContext() ? glGetError() : 0;
}

bool GlCanvas::ContextMatches(HGLRC context) const {
    return context_ == context;
}

void GlCanvas::ContextDeleted(HGLRC context) {
    if (context_ != context) return;
    ResetGraphics(false); context_ = nullptr;
    wc3::Log("Overlay context deleted; GPU names forgotten");
}
