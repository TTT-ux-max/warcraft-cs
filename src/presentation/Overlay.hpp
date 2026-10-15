#pragma once
#include "../combat/WeaponSlots.hpp"
#include "GunMesh.hpp"
#include "../application/ShooterController.hpp"
#include "SkyView.hpp"
#include "HealthBarRenderer.hpp"
#include "SpellHudView.hpp"
#include "Canvas.hpp"
class NativeMinimap;

// The overlay draws through a backend-neutral Canvas. GPU handles owned by the views follow
// the canvas generation: whenever the context/device behind the active canvas is replaced, the
// canvas increments its generation and the overlay forgets (never deletes) its own handles.
class Overlay {
public:
    void Configure(const char* root) { root_ = root; }
    void Draw(Canvas& canvas, HDC dc, HWND window, int width, int height,
        const ShooterController& controller, NativeMinimap& minimap, const std::vector<UnitHealthBar>& bars,
        const WorldLabelView& barView, float nearPlane, float farPlane);
    void ContextDeleted(HGLRC context);
    void RefreshAfterFocus() { refreshPending_ = true; }
    void MapChanged() { spellIconsRefresh_ = true; }
private:
    void ResetGraphics(bool deleteObjects, Canvas* canvas);
    SpellHudView spellHud_;
    bool spellIconsRefresh_ = false;
    std::string root_;
    GunMesh guns_[WeaponSlots::Count];
    GunMesh buyPreviews_[WeaponSlots::Count];
    bool previewLoaded_[WeaponSlots::Count] = {}, previewAttempted_[WeaponSlots::Count] = {};
    bool loaded_[WeaponSlots::Count] = {}, attempted_[WeaponSlots::Count] = {};
    Canvas* canvas_ = nullptr;
    uint64_t generation_ = 0;
    bool refreshPending_ = false;
    DWORD lastDiagnostic_ = 0;
    SkyView sky_;
};
