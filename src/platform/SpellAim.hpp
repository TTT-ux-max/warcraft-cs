#pragma once
#include "../combat/UnitHitboxes.hpp"
#include "../combat/DestructableHitboxes.hpp"

// A spell receives either the visible nearest widget or a point intersecting the world view.
struct SpellAim {
    wc3::Handle target=0;
    float x=0,y=0;
    bool point=false;
    // Target selection for first-person interaction passes unitsOnly: a destructable handle must
    // never reach a unit API, so the reticle picker never merges both widget families.
    static SpellAim Pick(wc3::Handle actor,const float* origin,const float* direction,float distance,
        UnitHitboxes& units,DestructableHitboxes& destructables,bool includeDestructables=true);
};
