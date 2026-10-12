#pragma once
#include "SpellHudSlot.hpp"
#include "SpellHudLayout.hpp"
#include <windows.h>
#include <gl/GL.h>
#include <functional>
#include <unordered_map>

// Presentation owns icon textures; the simulation supplies live cooldown values and shortcut labels.
class SpellHudView {
public:
    void Reset(bool deleteObjects);
    bool Valid() const;
    void Draw(const SpellHudSlots& slots,const std::array<std::string,4>& keys,const SpellHudLayout& layout,
        bool cooldowns,const std::function<void(float,float,const char*)>& text);
private:
    GLuint Texture(const std::string& path);
    std::unordered_map<std::string,GLuint> icons_;
};
