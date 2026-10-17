#include "Overlay.hpp"
#include "ScopeView.hpp"
#include "ReticleView.hpp"
#include "ReticleDiagnostics.hpp"
#include "BuyMenuView.hpp"
#include "HudHints.hpp"
#include "../platform/NativeMinimap.hpp"
#include "../platform/DiagnosticLog.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

void Overlay::ResetGraphics(bool deleteObjects, Canvas* canvas) {
    spellHud_.Reset(deleteObjects, canvas);
    sky_.Reset(deleteObjects, canvas);
    // GPU names belong to one context/device; never delete old numeric IDs in a replacement.
    for (int i = 0; i < WeaponSlots::Count; ++i) { guns_[i].Reset(deleteObjects, canvas); loaded_[i] = attempted_[i] = false; }
    // Menu models obey the same context ownership rules as first-person weapons.
    for (int i = 0; i < WeaponSlots::Count; ++i) { buyPreviews_[i].Reset(deleteObjects, canvas); previewLoaded_[i] = previewAttempted_[i] = false; }
}
void Overlay::ContextDeleted(HGLRC context) {
    // Only the canvas that actually draws into the deleted context forgets its handles.
    if (!canvas_ || !canvas_->ContextMatches(context)) return;
    ResetGraphics(false, nullptr); canvas_ = nullptr; generation_ = 0;
    wc3::Log("Overlay context deleted; GPU names forgotten");
}
void Overlay::Draw(Canvas& canvas, HDC dc, HWND window, int width, int height,
    const ShooterController& controller, NativeMinimap& minimap, const std::vector<UnitHealthBar>& bars,
    const WorldLabelView& barView, float nearPlane, float farPlane) {
    // Diagnosis escape hatch: WCS_SKIP_STAGES is a bitmask (1 bars, 2 weapon, 4 reticle,
    // 8 status+hints, 16 spell icons) that silently drops those draws for backend bisection.
    static unsigned skipStages = [] {
        char buffer[8] = {};
        return GetEnvironmentVariableA("WCS_SKIP_STAGES", buffer, sizeof(buffer)) ? unsigned(atoi(buffer)) : 0u;
    }();
    // Explain early returns as well as submitted geometry when investigating a missing aiming mark.
    if (!controller.Visible()) { ReticleDiagnostics::Skip("fps-inactive"); return; }
    if (canvas_ != &canvas) {
        wc3::Log("Overlay backend now %s (was %s)", canvas.Name(), canvas_ ? canvas_->Name() : "none");
        canvas_ = &canvas; generation_ = 0;
    }
    if (!canvas.BeginFrame(dc, width, height)) { ReticleDiagnostics::Skip("frame-unavailable", width, height); return; }
    // A replaced context/device increments the canvas generation; forget (not delete) owned handles.
    if (generation_ != canvas.Generation()) {
        ResetGraphics(false, &canvas); generation_ = canvas.Generation();
        wc3::Log("Overlay resources forgotten on %s generation %llu", canvas.Name(),
            static_cast<unsigned long long>(generation_));
    }
    if (refreshPending_) {
        // Keep surviving resources: blind deletion after Alt-Tab can touch IDs recycled by the game.
        bool valid = canvas.ResourcesValid() && sky_.Valid(canvas) && spellHud_.Valid(canvas);
        for (int i = 0; i < WeaponSlots::Count; ++i) if (loaded_[i] && !guns_[i].TexturesValid(canvas)) valid = false;
        for (int i = 0; i < WeaponSlots::Count; ++i) if (previewLoaded_[i] && !buyPreviews_[i].TexturesValid(canvas)) valid = false;
        if (!valid) { ResetGraphics(false, &canvas); wc3::Log("Overlay lost resources forgotten after focus return"); }
        else wc3::Log("Overlay resources retained after focus return");
    }
    refreshPending_ = false;
    // Imports can keep the same path across maps; reload those textures in the active context.
    if (spellIconsRefresh_) { spellHud_.Reset(true, &canvas); spellIconsRefresh_ = false; }
    // Sky touches only far-depth pixels before the weapon's private depth clear, preserving native scenery.
    if (controller.Settings().csSky && !controller.Status().contained && !controller.Status().hidden)
        sky_.Draw(canvas, root_, controller.SkyName(), controller.ViewYaw(), controller.ViewPitch(), controller.ViewFov(), float(width) / height);
    // World depth still exists here; the first-person weapon clears it in the next pass.
    unsigned barCount = 0;
    if (!(skipStages & 1u) && !controller.Status().contained && !controller.Status().hidden && !controller.Buying())
        barCount = HealthBarRenderer::Draw(canvas, bars, barView, width, height, nearPlane, farPlane);
    int index = controller.WeaponIndex();
    // Slightly tighten USP/AWP framing while preserving the original lower-edge arm origins.
    static constexpr float projectionScales[] = {0.72f, 0.78f, 0.94f, 0.73f, 1.0f, 0.95f, 0.85f};
    // ViewWeapon=false keeps the bare first-person eyes of the selected unit: no CS arms, no
    // skin pass, and no private depth clear — the world depth stays exactly as the engine left it.
    // A third-person orbit has no face to put arms on either, so the pass only runs on the eyes.
    if (controller.Settings().viewWeapon && controller.FirstPerson()) {
    canvas.BeginWeapon(width, height, projectionScales[index], 1.0f, 200.0f);
    if (!(skipStages & 2u)) {
    if (!attempted_[index]) {
        attempted_[index] = true;
        static const char* names[] = {"ak47", "m4a1", "usp", "awp", "knife", "c4", "sword"};
        loaded_[index] = guns_[index].Load(root_ + "\\assets\\" + names[index] + ".wcg", canvas);
    }
    if (loaded_[index] && !controller.Status().contained && !controller.Status().hidden && !controller.Scoped() && (index != WeaponSlots::C4 || controller.Ammo() > 0))
        // Give the weapon its own depth pass: back faces must not paint over the skin.
        guns_[index].Draw(controller.Animation(), controller.AnimationTime(), canvas);
    }
    canvas.EndWeapon();
    }
    // A swallowed/transported unit has no world viewpoint: hide equipment and retain the health/status HUD.
    bool unavailableView = controller.Status().contained || controller.Status().hidden;
    if (unavailableView) {
        ReticleDiagnostics::Skip(controller.Status().contained ? "unit-contained" : "unit-hidden", width, height);
        canvas.FillRect(0, 0, float(width), float(height), 0.015f, 0.015f, 0.015f, 1);
        canvas.Text(25, float(height) * .5f, controller.Status().devoured ?
            "SWALLOWED: DIGESTING | F6: RETURN TO RTS TO RESCUE THIS UNIT" : "UNIT IS OUTSIDE THE WORLD | F6: RETURN TO RTS",
            20, 1, .8f, .2f, 1);
    }
    else if (controller.Scoped()) ScopeView::Draw(canvas, float(width), float(height));
    else if (!(skipStages & 4u)) ReticleView::DrawHipFire(canvas, float(width), float(height), controller.Recoil());
    // Scale the bottom HUD from a readable 42-pixel font at 1080p, with safe edge padding.
    int statusSize = std::clamp(int(std::lround(height * (42.0f / 1080.0f))), 26, 84);
    if (!(skipStages & 8u)) {
    controller.Hits().Draw(canvas, float(width), float(height));
    char status[160]; sprintf_s(status, "HP %.0f   %s   %d / %d   GOLD %d", controller.Health(),
        ShooterController::weapons[index].name, controller.Ammo(), controller.Reserve(), controller.Gold());
    // Melee equipment has no magazine; show only its name instead of a misleading empty-ammo counter.
    if (WeaponSlots::Melee(index)) sprintf_s(status, "HP %.0f   %s   GOLD %d", controller.Health(), ShooterController::weapons[index].name, controller.Gold());
    float statusPad = statusSize * 0.75f, statusY = height - statusPad;
    canvas.Text(statusPad, statusY, status, statusSize, 1.0f, 0.85f, 0.3f, 1);
    // Installation progress and fuse are independent of which weapon is currently equipped.
    if (controller.PlantProgress() > 0) {
        char message[80]; sprintf_s(message, "PLANTING C4  %.0f%%", controller.PlantProgress() / 3 * 100);
        canvas.Text(width * 0.5f - 180, height * 0.65f, message, statusSize, 1.0f, 0.85f, 0.3f, 1);
    }
    if (controller.BombRemaining() >= 0) {
        char message[80]; sprintf_s(message, "C4 x%u  %.0fs", unsigned(controller.BombCount()), double(std::ceil(controller.BombRemaining())));
        canvas.Text(float(width - statusSize * 9), statusY, message, statusSize, 1.0f, 0.85f, 0.3f, 1);
    }
    // Compact hints reflect the configured keys and wrap before the native minimap.
    auto key = [&](ShortcutAction action) { return controller.Shortcut(action); };
    auto mapLayout = MinimapLayout::Calculate(width, height, controller.Settings().minimapSizePercent);
    // The GL minimap capturer is a GL-submission adapter; D3D backends have no capture path yet.
    bool minimapDrawn = controller.MinimapVisible() && canvas.IsOpenGL();
    if (minimapDrawn) minimap.Draw(mapLayout);
    std::string controls = "Warcraft CS by Yazgul | " + key(ShortcutAction::ToggleFPS) + ": RTS | " + key(ShortcutAction::ReloadSettings) + ": CONFIG | " + key(ShortcutAction::Minimap) + ": MINIMAP | " + key(ShortcutAction::Speed) + ": SPEED | " + key(ShortcutAction::Fog) + ": FOG";
    std::string equipment = std::string(controller.Settings().wheelZoom ? "WHEEL: ZOOM" : "WHEEL: WEAPONS") + " | " + key(ShortcutAction::Weapon6) + ": C4 | " + key(ShortcutAction::Pickup) + ": ITEM | " + key(ShortcutAction::Buy) + ": BUY | " + key(ShortcutAction::BuyAmmo) + ": AMMO";
    std::string abilities = key(ShortcutAction::HealthBars) + ": HEALTH BARS | " + key(ShortcutAction::Spell1) + "/" + key(ShortcutAction::Spell2) + "/" + key(ShortcutAction::Spell3) + "/" + key(ShortcutAction::Spell4) + ": SPELLS | " + key(ShortcutAction::SelfCast) + ": SELF";
    char squad[256]; sprintf_s(squad, "%s: FIGHT | %s: FOLLOW | %s: RELEASE | SQUAD %u %s | %s: FREE AMMO | %s: ALL WEAPONS",
        key(ShortcutAction::SquadFight).c_str(), key(ShortcutAction::SquadFollow).c_str(), key(ShortcutAction::SquadRelease).c_str(), unsigned(controller.SquadCount()), controller.SquadCount() ? (controller.SquadPassive() ? "FOLLOW" : "COMBAT") : "", key(ShortcutAction::Refill).c_str(), key(ShortcutAction::AllWeapons).c_str());
    int hintRight = minimapDrawn ? mapLayout.x - mapLayout.padding : width - 25;
    int columns = std::max(1, (hintRight - 25) / canvas.GlyphWidth(20));
    float hintY = 27;
    for (const auto& group : {controls, equipment, abilities, std::string(squad)}) {
        for (const auto& row : HudHints::Wrap(group, columns)) { canvas.Text(25, hintY, row.c_str(), 20, 1.0f, 0.85f, 0.3f, 1); hintY += 23; }
    }
    if (controller.RefillNotice()) canvas.Text(statusPad, statusY - statusSize - 16, controller.AmmoMessage(), statusSize, 1.0f, 0.85f, 0.3f, 1);
    if (!(skipStages & 16u) && controller.Settings().showSpellIcons && !unavailableView && !controller.Buying()) {
        std::array<std::string, 4> keys;
        for (int i = 0; i < 4; ++i) keys[i] = key(ShortcutAction(int(ShortcutAction::Spell1) + i));
        auto layout = SpellHudLayout::Calculate(width, height, controller.Settings().spellIconSizePercent);
        // Match labels to icon size so READY/countdowns do not overlap neighboring slots after resize.
        int size = std::clamp(layout.side / 3, 12, 28);
        spellHud_.Draw(canvas, controller.SpellHud(), keys, layout, controller.Settings().showSpellCooldowns,
            [&](float x, float y, const char* label) { canvas.Text(x, y, label, size, 1, .88f, .35f, 1); });
    }
    // Horizontal CS units match MaxBunnySpeed, excluding vertical falling/jumping velocity.
    // Stack above notices/status so the optional counter never covers health or ammunition.
    if (controller.SpeedVisible()) {
        char speed[64]; sprintf_s(speed, "SPEED %.0f u/s (CS)", controller.DisplaySpeed());
        int rows = 1 + (controller.RefillNotice() ? 1 : 0) + (*controller.Status().Label() ? 1 : 0);
        canvas.Text(statusPad, statusY - (statusSize + 16) * rows, speed, statusSize, 1.0f, 0.85f, 0.3f, 1);
    }
    // Ground items advertise interaction without issuing a walk-to-item RTS order.
    if (controller.ItemNearby()) { controls = key(ShortcutAction::Pickup) + ": PICK UP ITEM"; canvas.Text(width * .5f - 120, height * .72f, controls.c_str(), statusSize, 1.0f, 0.85f, 0.3f, 1); }
    // Explain blocked controls next to the status HUD while the corresponding Warcraft effect lasts.
    if (*controller.Status().Label()) {
        char effect[96]; sprintf_s(effect, "%s", controller.Status().Label());
        if (!controller.Status().immobilized && std::abs(controller.Status().speedScale - 1) >= 0.01f)
            sprintf_s(effect, "%s  %.0f%% SPEED", controller.Status().Label(), controller.Status().speedScale * 100);
        canvas.Text(statusPad, statusY - (statusSize + 16) * (controller.RefillNotice() ? 2 : 1), effect, statusSize, 1.0f, 0.85f, 0.3f, 1);
    }
    } // end status+hints stage
    BuyMenuView::Draw(canvas, controller, width, height, window,
        [&](float x, float y, const char* text, float r, float g, float b, float a) { canvas.Text(x, y, text, 20, r, g, b, a); },
        [&](float x, float y, const char* text, float r, float g, float b, float a) { canvas.Text(x, y, text, statusSize, r, g, b, a); },
        [&](int slot, float x, float y, float w, float h) {
            auto item = BuyCatalog::Slot(slot); if (!item) return;
            // Private previews load lazily; absent optional art never prevents a purchase.
            if (!previewAttempted_[slot]) {
                previewAttempted_[slot] = true;
                previewLoaded_[slot] = buyPreviews_[slot].Load(root_ + "\\assets\\buy\\" + item->cache + ".wcg", canvas);
            }
            if (previewLoaded_[slot]) {
                canvas.BeginPreview();
                buyPreviews_[slot].DrawPreview(x, y, w, h, canvas);
                canvas.EndPreview();
            }
        });
    canvas.EndFrame();
    // Report submission decisions, not pixel visibility; sampled backend errors can originate in the host frame.
    if (DiagnosticLog::Due(lastDiagnostic_)) {
        wc3::Trace("health bars allies=%d enemies=%d candidates=%u submitted=%u distance=%.0f",
            controller.Settings().healthBars.allies, controller.Settings().healthBars.enemies, unsigned(bars.size()), barCount, controller.Settings().healthBars.distance);
        // Sample backend submission with the same bounded cadence as existing overlay diagnostics.
        wc3::Trace("overlay speed visible=%d speedCS=%.1f", controller.SpeedVisible(), controller.DisplaySpeed());
        unsigned observedError = canvas.SampleError();
        wc3::Trace("overlay backend=%s submitted size=%dx%d weapon=%d loaded=%d reticle=%s recoil=%.2f observedError=%04X",
            canvas.Name(), width, height, index + 1, loaded_[index], unavailableView ? "hidden-unit" : controller.Scoped() ? "scope" : "hip-fire",
            controller.Recoil(), observedError);
        if (observedError != 0 && canvas.IsOpenGL())
            wc3::LogError("OpenGL error observed after overlay=%04X (host origin possible)", observedError);
        wc3::Trace("player hp=%.1f ammo=%d reserve=%d contained=%d hidden=%d disabled=%d disarmed=%d status=%s",
            controller.Health(), controller.Ammo(), controller.Reserve(), controller.Status().contained, controller.Status().hidden,
            controller.Status().incapacitated, controller.Status().disarmed, controller.Status().Label());
    }
}
