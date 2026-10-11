#include "NativeWeaponAttack.hpp"
#include <cmath>
#include <cstring>

namespace {
uintptr_t gameBase=0;
bool available=false;
struct Contact {uintptr_t attack;float damage;bool substituted,inImpact;};
thread_local Contact* contact=nullptr;
using Roll=float* (__thiscall*)(uintptr_t,float*);
Roll originalRoll=nullptr;
float* __fastcall DamageRoll(uintptr_t attack,uintptr_t,float* result) {
    auto value=originalRoll(attack,result);
    // Replace only this contact's base roll; native crits, orbs and damage triggers still process it afterwards.
    if(contact && contact->inImpact && contact->attack==attack && !contact->substituted) {
        *value=contact->damage;contact->substituted=true;
    }
    return value;
}
template<class T> bool Read(uintptr_t address,T& value) {
    SIZE_T got=0;return address && ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),&value,sizeof(value),&got) && got==sizeof(value);
}
bool Execute(wc3::Handle attacker,wc3::Handle victim,uintptr_t object,uintptr_t attack,uintptr_t target,float damage,int kind) {
    Contact current={attack,damage,false,false};Contact* previous=contact;
    using Impact=void (__thiscall*)(uintptr_t,uintptr_t,int,int,int,int,int);
    using Prepare=void (__thiscall*)(uintptr_t,uintptr_t,float*,float*);
    using Finish=void (__thiscall*)(uintptr_t,uintptr_t);
    float zero=0;int instantKind=kind==1 ? 1 : 4;
    contact=&current;
    __try {
        // 1 is the native melee impact; 4 is instant ranged. Keep melee-only passive eligibility native.
        // Only the synchronous impact changes kind: no additional projectile, animation delay or AI order.
        *reinterpret_cast<int*>(attack+0xDC)=instantKind;
        // Native wind-up rolls Bash/critical chances before impact. Its callbacks also notify map attack triggers.
        reinterpret_cast<Prepare>(gameBase+0x281580)(object,target,&zero,&zero);
        uintptr_t currentAttack=0;
        if(wc3::UnitObject(attacker)==object && wc3::UnitObject(victim)==target &&
            Read(object+0x1E8,currentAttack) && currentAttack==attack) {
            current.inImpact=true;
            reinterpret_cast<Impact>(gameBase+0xCF660)(attack,target,0,0,1,0,1);
        }
    } __finally {
        contact=previous;
        uintptr_t stillOwned=0;
        // Attack callbacks can transform/remove the hero; never restore a freed/replaced attack component.
        if(wc3::UnitObject(attacker)==object) {
            // Release native per-attack flags/counters, just as the ordinary attack animation ends.
            reinterpret_cast<Finish>(gameBase+0x276330)(object,wc3::UnitObject(victim)==target ? target : 0);
            int currentKind=0;
            if(wc3::UnitObject(attacker)==object && Read(object+0x1E8,stillOwned) && stillOwned==attack &&
                Read(attack+0xDC,currentKind) && currentKind==instantKind)
                *reinterpret_cast<int*>(attack+0xDC)=kind;
        }
    }
    return current.substituted;
}
}
void NativeWeaponAttack::Install(uintptr_t base,HookInstaller hook) {
    gameBase=base;
    const unsigned char impact[]={0x6A,0xFF,0x68};
    const unsigned char roll[]={0x51,0x56,0x57,0x68};
    const unsigned char cleanup[]={0xC2,0x18,0x00};int relative=0;
    const unsigned char prepare[]={0x51,0x57,0x8B,0xF9};
    const unsigned char finish[]={0x8B,0x81,0xE0,0x01,0x00,0x00};
    std::memcpy(&relative,reinterpret_cast<void*>(base+0xCF82B),4);
    bool verified=!std::memcmp(reinterpret_cast<void*>(base+0xCF660),impact,sizeof(impact)) &&
        !std::memcmp(reinterpret_cast<void*>(base+0xC6940),roll,sizeof(roll)) &&
        !std::memcmp(reinterpret_cast<void*>(base+0x281580),prepare,sizeof(prepare)) &&
        !std::memcmp(reinterpret_cast<void*>(base+0x276330),finish,sizeof(finish)) &&
        !std::memcmp(reinterpret_cast<void*>(base+0xD0225),cleanup,sizeof(cleanup)) && 0xCF82F+relative==0xC6940;
    available=verified && hook(reinterpret_cast<void*>(base+0xC6940),reinterpret_cast<void*>(DamageRoll),reinterpret_cast<void**>(&originalRoll));
    wc3::Log("Native weapon attack effects verified=%d installed=%d",verified,available);
}
bool NativeWeaponAttack::Apply(wc3::Handle attacker,wc3::Handle target,float damage) {
    if(!available || contact || !std::isfinite(damage) || damage<=0)return false;
    uintptr_t object=wc3::UnitObject(attacker),victim=wc3::UnitObject(target),attack=0;int kind=0;
    if(!object || !victim || !Read(object+0x1E8,attack) || !Read(attack+0xDC,kind) || kind<1 || kind>8)return false;
    // Once the pipeline has run, even an evaded/immune hit is handled: never add a fallback second hit.
    bool substituted=Execute(attacker,target,object,attack,victim,damage,kind);
    wc3::Log("Weapon attack effects attacker=%08X target=%08X base=%.1f substituted=%d nativeKind=%d",attacker,target,damage,substituted,kind);
    return true;
}
int NativeWeaponAttack::AttackType(wc3::Handle attacker) {
    uintptr_t object=wc3::UnitObject(attacker),attack=0;int type=0;
    // Disabling passives must not switch the armor multiplier from the creature's attack type to spell damage.
    return object && Read(object+0x1E8,attack) && Read(attack+0xF4,type) && type>=0 && type<=6 ? type : 0;
}
