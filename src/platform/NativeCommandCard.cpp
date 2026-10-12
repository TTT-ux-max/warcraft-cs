#include "NativeCommandCard.hpp"
#include <windows.h>
#include <cmath>

namespace {
template<class T> bool Load(uintptr_t at,T& value) {
    SIZE_T got=0;return at && ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(at),&value,sizeof(value),&got) && got==sizeof(value);
}
std::string Icon(uintptr_t at) {
    std::string result;
    for(size_t i=0;i<260;++i) {char c=0;if(!Load(at+i,c))return {};if(!c)return result;result+=c;}
    return {};
}
}
bool NativeCommandCard::Apply(uintptr_t ui,uintptr_t abilityObject,float mana,SpellHudSlot& slot) {
    if(!ui || !abilityObject || !slot.ability)return false;
    uintptr_t bar=0,items=0,rows=0,buttons=0;
    if(!Load(ui+0x3C8,bar) || !Load(bar+0x154,items) || !Load(items+8,rows) || !Load(rows,buttons))return false;
    for(int i=0;i<12;++i) {
        uintptr_t data=0,object=0;int id=0,cost=0;
        // A selected ally, research submenu or stale command card must not overwrite our hero's slots.
        if(!Load(buttons+i*0x1C0+0x190,data) || !Load(data+0x6D4,object) || object!=abilityObject ||
            !Load(data+4,id) || id!=slot.ability)continue;
        auto path=Icon(data+0x5C8);if(!path.empty())slot.icon=path;
        if(Load(data+0x594,cost) && cost>=0 && cost<=1000000 && std::isfinite(mana))slot.noMana=float(cost)>mana+.01f;
        slot.commandKnown=true;return true;
    }
    return false;
}
