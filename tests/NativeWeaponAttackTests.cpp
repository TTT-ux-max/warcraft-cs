#include "../src/platform/NativeWeaponAttack.hpp"
#include <array>
#include <cassert>
#include <cstring>
#include <limits>

namespace {
std::array<unsigned char,0x300> actor{},victim{},attack{};
bool alive=true,removeDuringPrepare=false,removeDuringFinish=false,evade=false;
int prepared=0,finished=0,impacts=0;
using RollHook=float* (__fastcall*)(uintptr_t,uintptr_t,float*);
RollHook rollHook=nullptr;
uintptr_t Actor() {return reinterpret_cast<uintptr_t>(actor.data());}
uintptr_t Victim() {return reinterpret_cast<uintptr_t>(victim.data());}
uintptr_t Attack() {return reinterpret_cast<uintptr_t>(attack.data());}
float* __fastcall Roll(uintptr_t,uintptr_t,float* result) {*result=42;return result;}
bool Hook(void*,void* callback,void** original) {rollHook=reinterpret_cast<RollHook>(callback);*original=reinterpret_cast<void*>(Roll);return true;}
void __fastcall Prepare(uintptr_t,uintptr_t,uintptr_t,float*,float*) {
    ++prepared;float result=0;
    // An attack callback may query another attack roll; it must not consume this contact's replacement.
    rollHook(Attack(),0,&result);assert(result==42);
    if(removeDuringPrepare)alive=false;
}
void __fastcall Finish(uintptr_t,uintptr_t,uintptr_t) {++finished;if(removeDuringFinish)alive=false;}
void __fastcall Impact(uintptr_t component,uintptr_t,uintptr_t,int,int,int,int,int) {
    ++impacts;
    if(evade)return;
    float result=0;rollHook(component+4,0,&result);assert(result==42);
    rollHook(component,0,&result);assert(result==123);
    rollHook(component,0,&result);assert(result==42); // No recursive/secondary hit receives the weapon base twice.
}
void Jump(uintptr_t address,void* destination) {
    *reinterpret_cast<unsigned char*>(address)=0xE9;
    *reinterpret_cast<int*>(address+1)=int(reinterpret_cast<uintptr_t>(destination)-address-5);
    FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(address),5);
}
}
namespace wc3 {
uintptr_t UnitObject(Handle handle) {return handle==1 && alive ? Actor() : handle==2 ? Victim() : 0;}
void Log(const char*,...) {}
}
int main() {
    auto memory=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x282000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));assert(memory);
    uintptr_t base=reinterpret_cast<uintptr_t>(memory);
    *reinterpret_cast<uintptr_t*>(actor.data()+0x1E8)=Attack();
    *reinterpret_cast<int*>(attack.data()+0xDC)=2;
    // Direct damage keeps the unit's native armor class even when passive dispatch is disabled.
    *reinterpret_cast<int*>(attack.data()+0xF4)=6;assert(NativeWeaponAttack::AttackType(1)==6);
    *reinterpret_cast<int*>(attack.data()+0xF4)=99;assert(NativeWeaponAttack::AttackType(1)==0);
    *reinterpret_cast<int*>(attack.data()+0xF4)=6;
    assert(!NativeWeaponAttack::Apply(1,2,123));
    // Layout mismatch must disable the adapter, preserving the caller's ordinary direct-damage fallback.
    NativeWeaponAttack::Install(base,Hook);assert(!NativeWeaponAttack::Apply(1,2,123));
    const unsigned char impact[]={0x6A,0xFF,0x68},roll[]={0x51,0x56,0x57,0x68},cleanup[]={0xC2,0x18,0};
    const unsigned char prepare[]={0x51,0x57,0x8B,0xF9},finish[]={0x8B,0x81,0xE0,1,0,0};
    memcpy(memory+0xCF660,impact,sizeof(impact));memcpy(memory+0xC6940,roll,sizeof(roll));
    memcpy(memory+0xD0225,cleanup,sizeof(cleanup));memcpy(memory+0x281580,prepare,sizeof(prepare));memcpy(memory+0x276330,finish,sizeof(finish));
    *reinterpret_cast<int*>(memory+0xCF82B)=0xC6940-0xCF82F;
    NativeWeaponAttack::Install(base,Hook);
    Jump(base+0xCF660,reinterpret_cast<void*>(Impact));Jump(base+0x281580,reinterpret_cast<void*>(Prepare));Jump(base+0x276330,reinterpret_cast<void*>(Finish));
    assert(!NativeWeaponAttack::Apply(0,2,123) && !NativeWeaponAttack::Apply(1,0,123));
    assert(!NativeWeaponAttack::Apply(1,2,0) && !NativeWeaponAttack::Apply(1,2,std::numeric_limits<float>::quiet_NaN()));
    assert(NativeWeaponAttack::Apply(1,2,123) && prepared==1 && impacts==1 && finished==1);
    assert(*reinterpret_cast<int*>(attack.data()+0xDC)==2);
    float ordinary=0;rollHook(Attack(),0,&ordinary);assert(ordinary==42);
    // A native miss has already dispatched effects: returning handled prevents a second fallback damage event.
    evade=true;assert(NativeWeaponAttack::Apply(1,2,123) && impacts==2 && finished==2);
    assert(*reinterpret_cast<int*>(attack.data()+0xDC)==2);
    // Cleanup callbacks may also remove the actor; their former component must not be restored afterwards.
    evade=false;removeDuringFinish=true;assert(NativeWeaponAttack::Apply(1,2,123));
    assert(impacts==3 && finished==3 && *reinterpret_cast<int*>(attack.data()+0xDC)==4);
    alive=true;removeDuringFinish=false;*reinterpret_cast<int*>(attack.data()+0xDC)=2;
    // Transformation/removal during attack callbacks cancels impact and never writes the former component.
    evade=false;removeDuringPrepare=true;assert(NativeWeaponAttack::Apply(1,2,123));
    assert(impacts==3 && finished==3);
    VirtualFree(memory,0,MEM_RELEASE);
}
