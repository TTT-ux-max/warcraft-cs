#include "NativeSpellSlots.hpp"
#include <cstring>

namespace {
std::string ArchiveText(const char* path) {
    auto storm=GetModuleHandleA("Storm.dll");if(!storm)return {};
    auto open=reinterpret_cast<BOOL(WINAPI*)(const char*,HANDLE*)>(GetProcAddress(storm,MAKEINTRESOURCEA(267)));
    auto size=reinterpret_cast<DWORD(WINAPI*)(HANDLE,DWORD*)>(GetProcAddress(storm,MAKEINTRESOURCEA(265)));
    auto read=reinterpret_cast<BOOL(WINAPI*)(HANDLE,void*,DWORD,DWORD*,LONG)>(GetProcAddress(storm,MAKEINTRESOURCEA(269)));
    auto close=reinterpret_cast<BOOL(WINAPI*)(HANDLE)>(GetProcAddress(storm,MAKEINTRESOURCEA(253)));
    HANDLE file=nullptr;if(!open || !size || !read || !close || !open(path,&file))return {};
    DWORD length=size(file,nullptr),got=0;std::string text;
    if(length && length<4*1024*1024) {text.resize(length);if(!read(file,text.data(),length,&got,0) || got!=length)text.clear();}
    close(file);return text;
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
int NativeSpellSlots::Ability(wc3::Handle unit,int slot,const SpellSettings& settings) {
    if(slot<0 || slot>=4)return 0;
    if(!settings.ability[slot].empty())return HeroSpellSlots::Rawcode(settings.ability[slot]);
    if(!loaded_) {
        loaded_=true;slots_.LoadUnits(ArchiveText("Units\\UnitAbilities.slk"));
        for(const char* race:{"Human","Orc","Undead","NightElf","Neutral"}) {
            std::string path="Units\\"+std::string(race)+"AbilityFunc.txt";slots_.LoadPositions(ArchiveText(path.c_str()));
        }
    }
    return slots_.ForUnit(wc3::GetUnitTypeId(unit))[slot];
}
int NativeSpellSlots::Order(wc3::Handle unit,int ability) {
    if(!verified_ || !ability || slots_.Passive(ability) || wc3::GetUnitAbilityLevel(unit,ability)<=0)return 0;
    using Find=uintptr_t (__thiscall*)(uintptr_t,int,BOOL,BOOL,BOOL,BOOL);
    uintptr_t object=wc3::UnitObject(unit);if(!object)return 0;
    uintptr_t skill=reinterpret_cast<Find>(base_+0x787D0)(object,ability,FALSE,TRUE,TRUE,TRUE),table=0,method=0;
    if(!Read(skill,table) || table<base_+0x880000 || table>=base_+0xA80000 || !Read(table+0x308,method))return 0;
    if(method<base_+0x1000 || method>=base_+0x880000)return 0;
    // Stock spell order getters are mov eax,<order>; ret. Read the result without invoking aura vtables.
    std::array<unsigned char,6> code={};if(!Read(method,code) || code[0]!=0xB8 || code[5]!=0xC3)return 0;
    int order=0;std::memcpy(&order,code.data()+1,4);
    return order>=852000 && order<=853999 ? order : 0;
}
