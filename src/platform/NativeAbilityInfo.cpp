#include "NativeAbilityInfo.hpp"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
template<class T> bool Load(uintptr_t at,T& value) {
    SIZE_T got=0;return at && ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(at),&value,sizeof(value),&got) && got==sizeof(value);
}
std::string Text(uintptr_t at) {
    std::string result;
    for(unsigned i=0;at && i<512;++i) {char c=0;if(!Load(at+i,c))return {};if(!c)return result;result+=c;}
    return {};
}
bool Timer(uintptr_t at) {
    // Check the actual RTTI before interpreting a polymorphic ability's optional timer field.
    uintptr_t table=0,locator=0,type=0;
    return Load(at,table) && Load(table-sizeof(uintptr_t),locator) && Load(locator+12,type) &&
        Text(type+8)==".?AVCAgentTimer@@";
}
}
void NativeAbilityInfo::Read(uintptr_t ability,SpellHudSlot& slot) {
    if(!ability)return;
    uintptr_t data=0,ui=0,items=0,art=0;unsigned levels=0;
    // The native per-level Art array includes custom object editor/runtime overrides.
    if(Load(ability+0x54,data) && Load(data+0x28,ui) && Load(ui+0xB0,levels) &&
        levels>0 && levels<=100 && Load(ui+0xB4,items) &&
        Load(items+sizeof(uintptr_t)*size_t(std::clamp(slot.level,1,int(levels))-1),art)) {
        auto icon=Text(art);if(!icon.empty())slot.icon=icon;
    }
    uintptr_t running=0,clock=0;
    if(!Timer(ability+0xD0))return;
    slot.cooldownKnown=true;slot.cooldown=0;
    if(!Load(ability+0xDC,running)) {slot.cooldownKnown=false;return;}
    if(!running)return;
    float timeout=0,current=0;
    if(!Load(running+4,timeout) || !Load(running+0xC,clock) || !Load(clock+0x40,current) ||
        !std::isfinite(timeout) || !std::isfinite(current) || timeout-current>86400) {slot.cooldownKnown=false;return;}
    // Native time pauses with Warcraft and includes cooldown resets; wall-clock estimates would diverge.
    slot.cooldown=std::max(0.f,timeout-current);
}
