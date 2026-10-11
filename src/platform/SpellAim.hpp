#pragma once
#include "../combat/UnitHitboxes.hpp"
#include "../combat/DestructableHitboxes.hpp"

// A spell receives either the visible nearest widget or a point intersecting the world view.
struct SpellAim {
    wc3::Handle target=0;
    float x=0,y=0;
    bool point=false;
    static SpellAim Pick(wc3::Handle actor,const float* origin,const float* direction,float distance,
        UnitHitboxes& units,DestructableHitboxes& destructables);
};
