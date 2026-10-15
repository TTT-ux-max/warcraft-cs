#include "ReticleView.hpp"
#include "Canvas.hpp"
#include "ReticleDiagnostics.hpp"
#include <windows.h>
#include <algorithm>

void ReticleView::DrawHipFire(Canvas& canvas, float width, float height, float recoil) {
    // Retain the original two-pixel arms and recoil gap at the true center of the FPS viewport.
    float x = width * .5f, y = height * .5f, gap = 5 + recoil * 4;
    // GL state sampling is GL-only; the D3D backends skip it (no GL context exists there).
    bool sampled = canvas.IsOpenGL() && ReticleDiagnostics::Begin("hip-fire", x, y, gap, 2, recoil, 4);
    canvas.FillRect(x - gap - 10, y - 1, 10, 2, .5f, 1, .25f, 1);
    canvas.FillRect(x + gap, y - 1, 10, 2, .5f, 1, .25f, 1);
    canvas.FillRect(x - 1, y - gap - 10, 2, 10, .5f, 1, .25f, 1);
    canvas.FillRect(x - 1, y + gap, 2, 10, .5f, 1, .25f, 1);
    if (canvas.IsOpenGL()) { ReticleDiagnostics::Prepared(sampled); ReticleDiagnostics::End(sampled); }
}

void ReticleView::DrawScope(Canvas& canvas, float cx, float cy, float radius, float thickness) {
    // The AWP keeps its black center hairs and resolution-scaled thickness using the same reliable primitive.
    float half = thickness * .5f;
    bool sampled = canvas.IsOpenGL() && ReticleDiagnostics::Begin("scope", cx, cy, radius, thickness, 0, 2);
    canvas.FillRect(cx - radius, cy - half, radius * 2, thickness, 0, 0, 0, 1);
    canvas.FillRect(cx - half, cy - radius, thickness, radius * 2, 0, 0, 0, 1);
    if (canvas.IsOpenGL()) { ReticleDiagnostics::Prepared(sampled); ReticleDiagnostics::End(sampled); }
}
