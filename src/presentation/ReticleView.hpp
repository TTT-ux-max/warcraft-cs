#pragma once

class Canvas;

// Both aiming modes use filled geometry instead of driver-dependent line rasterization.
class ReticleView {
public:
    static void DrawHipFire(Canvas& canvas, float width, float height, float recoil);
    static void DrawScope(Canvas& canvas, float cx, float cy, float radius, float thickness);
};
