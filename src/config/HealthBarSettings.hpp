#pragma once

// Retain existing team keys so config migration preserves older preferences.
struct HealthBarSettings {
    // First-person play reads the world like an RPG: both teams show health unless the player
    // changes the saved preference or presses K.
    bool allies=true, enemies=true;
    float distance=2000;

    void ToggleAll() {
        // A mixed saved state first enables both teams; the next press hides both.
        bool enabled=!(allies && enemies);
        allies=enemies=enabled;
    }
};
