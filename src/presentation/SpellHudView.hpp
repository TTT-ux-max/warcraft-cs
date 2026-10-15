#pragma once
#include "SpellHudSlot.hpp"
#include "SpellHudLayout.hpp"
#include <windows.h>
#include <functional>
#include <string>
#include <unordered_map>

class Canvas;

// Presentation owns icon textures; the simulation supplies live cooldown values and shortcut labels.
class SpellHudView {
public:
    void Reset(bool deleteObjects, Canvas* canvas);
    bool Valid(Canvas& canvas) const;
    void Draw(Canvas& canvas,const SpellHudSlots& slots,const std::array<std::string,4>& keys,const SpellHudLayout& layout,
        bool cooldowns,const std::function<void(float,float,const char*)>& text);
private:
    uintptr_t Texture(const std::string& path, Canvas& canvas);
    std::unordered_map<std::string,uintptr_t> icons_;
};
