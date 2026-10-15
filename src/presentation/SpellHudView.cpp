#include "SpellHudView.hpp"
#include "Canvas.hpp"
#include "SpellCooldownWedge.hpp"
#include "../platform/NativeArchive.hpp"
#include "../platform/SpellIconImage.hpp"
#include "../platform/WarcraftApi.hpp"
#include <cstdio>
#include <cmath>

void SpellHudView::Reset(bool deleteObjects, Canvas* canvas) {
    // Map imports can reuse a path with different pixels; contexts can reuse numeric texture IDs.
    if (deleteObjects && canvas)
        for (const auto& icon : icons_) if (icon.second) canvas->DestroyTexture(icon.second);
    icons_.clear();
}
bool SpellHudView::Valid(Canvas& canvas) const {
    for (const auto& icon : icons_) if (icon.second && !canvas.TextureValid(icon.second)) return false;
    return true;
}
uintptr_t SpellHudView::Texture(const std::string& path, Canvas& canvas) {
    if (path.empty()) return 0;
    auto found = icons_.find(path); if (found != icons_.end()) return found->second;
    if (icons_.size() >= 512) return 0;
    SpellIconImage image; uintptr_t texture = 0;
    // Failed/missing optional art is cached too, so archive reads cannot stall every render frame.
    if (image.Decode(NativeArchive::Read(path)))
        texture = canvas.CreateTexture(image.pixels.data(), image.width, image.height);
    wc3::Log("Spell icon loaded=%d size=%ux%u path=%s", texture != 0, image.width, image.height, path.c_str());
    icons_[path] = texture; return texture;
}
void SpellHudView::Draw(Canvas& canvas,const SpellHudSlots& slots,const std::array<std::string,4>& keys,const SpellHudLayout& layout,
    bool cooldowns,const std::function<void(float,float,const char*)>& text) {
    if (layout.side <= 0) return;
    for (int i = 0; i < 4; ++i) {
        const auto& slot = slots[i];
        float x = float(layout.x + i * (layout.side + layout.gap)), y = float(layout.y), side = float(layout.side);
        canvas.FillRect(x - 2, y - 2, side + 4, side + 4, .02f, .02f, .02f, .9f);
        uintptr_t icon = Texture(slot.icon, canvas);
        if (icon) {
            // Disabled/unlearned skills dim; mana shortage uses Warcraft's blue visual cue.
            float light = slot.level && !slot.disabled ? 1.f : .35f;
            float r = slot.noMana ? light * .4f : light, g = slot.noMana ? light * .4f : light;
            canvas.TextureQuad(icon, x, y, side, side, r, g, light, 1);
        }
        if (cooldowns && slot.level && !slot.passive && slot.cooldownKnown && slot.cooldown > .05f) {
            // Only a live native countdown darkens an icon; a rejected order cannot start a fake cooldown.
            auto wedge = SpellCooldownWedge(slot.cooldown, slot.cooldownTotal);
            if (wedge.empty()) canvas.FillRect(x, y, side, side, 0, 0, 0, .65f);
            else {
                std::vector<float> xy;
                xy.reserve(wedge.size() * 2);
                for (const auto& p : wedge) { xy.push_back(x + p[0] * side); xy.push_back(y + p[1] * side); }
                canvas.TriangleFan(xy.data(), int(xy.size() / 2), 0, 0, 0, .65f);
            }
        }
        text(x + 3, y - 7, keys[i].c_str());
        char label[32];
        if (!slot.ability) sprintf_s(label, "EMPTY");
        else if (!slot.level) sprintf_s(label, "LV0");
        else if (slot.passive) sprintf_s(label, "PASS");
        else if (slot.disabled) sprintf_s(label, "DISABLED");
        else if (slot.noMana && (!slot.cooldownKnown || slot.cooldown <= .05f)) sprintf_s(label, "NO MANA");
        else if (!cooldowns) continue;
        // Charge spells may cast repeatedly until empty; their map timer must never be invented locally.
        else if (slot.charges >= 0) sprintf_s(label, "%d CHG", slot.charges);
        else if (!slot.cooldownKnown) sprintf_s(label, "?");
        else if (slot.cooldown >= 10) sprintf_s(label, "%.0fs", std::ceil(slot.cooldown));
        else if (slot.cooldown > .05f) sprintf_s(label, "%.1fs", slot.cooldown);
        else sprintf_s(label, "READY");
        text(x + 3, y + side + 19, label);
    }
}
