#include "HitFeedback.hpp"
#include "Canvas.hpp"
#include <algorithm>
#include <cmath>

void HitFeedback::Record() { tick_ = GetTickCount(); active_ = true; }
void HitFeedback::Clear() { active_ = false; }
void HitFeedback::Draw(Canvas& canvas, float width, float height) const {
    float age = (GetTickCount() - tick_) * 0.001f;
    if (!active_ || age >= 0.65f) return;
    float scale = std::clamp(height / 548.0f, 0.6f, 2.5f), cx = width * 0.5f, cy = height * 0.5f;
    // Only four fading crosshair strokes confirm damage, regardless of target classification.
    float alpha = 1 - age / 0.65f;
    for (int i = 0; i < 4; ++i) {
        float angle = 0.78539816f + i * 1.57079633f;
        float dx = std::cos(angle), dy = std::sin(angle);
        canvas.Line(cx + dx * 13 * scale, cy + dy * 13 * scale,
            cx + dx * 22 * scale, cy + dy * 22 * scale, 3 * scale, .95f, .95f, .95f, alpha);
    }
}
