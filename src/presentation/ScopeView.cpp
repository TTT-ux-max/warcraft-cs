#include "ScopeView.hpp"
#include "Canvas.hpp"
#include "ReticleView.hpp"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <vector>

void ScopeView::Draw(Canvas& canvas, float width, float height) {
    float cx = width * 0.5f, cy = height * 0.5f;
    float radius = std::min(width, height) * 0.46f, outside = std::hypot(width, height);
    // An opaque annulus removes peripheral vision, including widescreen side regions. The
    // vertex order is the quad strip the GL backend used; D3D renders the same ring as a strip.
    std::vector<float> xy;
    xy.reserve((256 + 1) * 2 * 2);
    for (int i = 0; i <= 256; ++i) {
        float angle = i * 6.28318530718f / 256, x = std::cos(angle), y = std::sin(angle);
        xy.push_back(cx + x * radius); xy.push_back(cy + y * radius);
        xy.push_back(cx + x * outside); xy.push_back(cy + y * outside);
    }
    canvas.TriangleStrip(xy.data(), int(xy.size() / 2), 0, 0, 0, 1);
    // Scope hairs share the filled reticle path so inherited line stipple cannot hide aiming marks.
    ReticleView::DrawScope(canvas, cx, cy, radius, std::max(1.0f, height / 720.0f));
}
