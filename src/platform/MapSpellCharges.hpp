#pragma once
#include "WarcraftApi.hpp"
#include "../presentation/SpellHudSlot.hpp"

// DotA's Shrapnel uses map-owned charge-marker abilities, rather than a native cooldown timer.
// Only recognize the original hero/skill with an existing marker; unrelated maps keep native behavior.
namespace MapSpellCharges {
inline void Read(wc3::Handle unit,SpellHudSlot& slot) {
    if(slot.ability!=0x41303634 || wc3::GetUnitTypeId(unit)!=0x5573796C || slot.level<=0)return;
    for(int count=0;count<=6;++count)if(wc3::GetUnitAbilityLevel(unit,0x41325932+count)>0) {
        slot.charges=count;return;
    }
}
}
