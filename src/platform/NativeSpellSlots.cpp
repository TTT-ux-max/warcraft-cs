#include "NativeSpellSlots.hpp"
#include "NativeAbilityInfo.hpp"
#include "NativeArchive.hpp"
#include <cstring>

namespace {
std::string ArchiveText(const char* path) {
    auto bytes=NativeArchive::Read(path,4*1024*1024);return std::string(bytes.begin(),bytes.end());
}
template<class T> bool Read(uintptr_t at,T& result) {
    SIZE_T got=0;return at && ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(at),&result,sizeof(result),&got) && got==sizeof(result);
}
}
void NativeSpellSlots::Configure(uintptr_t base) {
    base_=base;
    // Same lookup used by GetUnitAbilityLevel; fail closed on an unsupported native implementation.
    const unsigned char signature[]={0x8B,0x44,0x24,0x04,0x3D,0x75,0x62,0x4E,0x41};
    verified_=!memcmp(reinterpret_cast<void*>(base+0x787D0),signature,sizeof(signature));
    wc3::Log("Native spell ability lookup verified=%d",verified_);
}
int NativeSpellSlots::Ability(wc3::Handle unit,int slot,const SpellSettings& settings,bool diagnostics) {
    if(slot<0 || slot>=4)return 0;
    if(!loaded_) {
        loaded_=true;slots_.LoadUnits(ArchiveText("Units\\UnitAbilities.slk"));
        for(const char* race:{"Human","Orc","Undead","NightElf","Neutral"}) {
            std::string path="Units\\"+std::string(race)+"AbilityFunc.txt";slots_.LoadPositions(ArchiveText(path.c_str()));
        }
    }
    // Load passive metadata for configured rawcodes as well as automatically selected hero slots.
    if(!settings.ability[slot].empty())return HeroSpellSlots::Rawcode(settings.ability[slot]);
    auto resolved=slots_.ForUnit(wc3::GetUnitTypeId(unit),[&](int ability){return wc3::GetUnitAbilityLevel(unit,ability)>0;});
    if(diagnostics)wc3::Log("Hero spell slots unit=%08X type=%08X resolved=%08X,%08X,%08X,%08X requestedSlot=%d",
        unit,wc3::GetUnitTypeId(unit),resolved[0],resolved[1],resolved[2],resolved[3],slot+1);
    return resolved[slot];
}
int NativeSpellSlots::Order(wc3::Handle unit,int ability) {
    if(!verified_ || !ability || slots_.Passive(ability) || wc3::GetUnitAbilityLevel(unit,ability)<=0)return 0;
    uintptr_t skill=Object(unit,ability),table=0,method=0;
    if(!Read(skill,table) || !Read(table+0x308,method))return 0;
    if(method<base_+0x1000 || method>=base_+0x880000)return 0;
    // Stock spell order getters are mov eax,<order>; ret. Read the result without invoking aura vtables.
    std::array<unsigned char,6> code={};if(!Read(method,code) || code[0]!=0xB8 || code[5]!=0xC3)return 0;
    int order=0;std::memcpy(&order,code.data()+1,4);
    return order>=852000 && order<=853999 ? order : 0;
}
uintptr_t NativeSpellSlots::Object(wc3::Handle unit,int ability) const {
    if(!verified_ || !ability)return 0;
    using Find=uintptr_t (__thiscall*)(uintptr_t,int,BOOL,BOOL,BOOL,BOOL);
    uintptr_t object=wc3::UnitObject(unit);if(!object)return 0;
    uintptr_t skill=reinterpret_cast<Find>(base_+0x787D0)(object,ability,FALSE,TRUE,TRUE,TRUE),table=0;
    return Read(skill,table) && table>=base_+0x880000 && table<base_+0xA80000 ? skill : 0;
}
SpellHudSlots NativeSpellSlots::Snapshot(wc3::Handle unit,const SpellSettings& settings) {
    SpellHudSlots result={};if(!unit)return result;
    // Sample all learned/passive gaps using the very same mapping consumed by spell casting.
    for(int i=0;i<4;++i) {
        auto& slot=result[i];slot.ability=Ability(unit,i,settings,false);
        if(!slot.ability)continue;
        slot.level=wc3::GetUnitAbilityLevel(unit,slot.ability);slot.passive=slots_.Passive(slot.ability);
        slot.icon=slots_.Icon(slot.ability);NativeAbilityInfo::Read(Object(unit,slot.ability),slot);
    }
    return result;
}
