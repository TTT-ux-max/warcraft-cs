#pragma once

// One validated snapshot supplies native frame pacing and the FPS world camera, reloaded on F8.
struct RenderSettings {
    int maxFPS=144;
    float drawDistance=10000;
};
