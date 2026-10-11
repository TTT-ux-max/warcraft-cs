#pragma once
#include <algorithm>
#include <cmath>

// Native cameras prepare projection outside the world batch callback.
class FpsProjection {
public:
    static float NearPlane(bool fps, float nativeNear, float nativeFar, float worldFarClip) {
        // Match the live native world-camera field, which may clamp or interpolate the INI request.
        // Shortening its near plane keeps terrain visible below an eye only 96 units above the ground.
        return fps && std::abs(nativeFar-worldFarClip)<0.01f ? std::min(nativeNear,8.0f) : nativeNear;
    }
};
