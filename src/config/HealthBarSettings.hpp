#pragma once

// Retain existing team keys so config migration preserves older preferences.
struct HealthBarSettings {
    bool allies=false, enemies=false;
    float distance=2000;

    void ToggleAll() {
        // A mixed saved state first enables both teams; the next press hides both.
        bool enabled=!(allies && enemies);
        allies=enemies=enabled;
    }
};
