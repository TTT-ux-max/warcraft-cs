#pragma once
#include <array>
#include <string>

// A frame snapshot contains values and paths only, never map-owned ability pointers.
struct SpellHudSlot {
    int ability=0,level=0;
    bool passive=false,cooldownKnown=false;
    float cooldown=0;
    std::string icon;
};
using SpellHudSlots=std::array<SpellHudSlot,4>;
