#pragma once
#include "WarcraftApi.hpp"
#include "../config/HeroSpellSlots.hpp"
#include "../config/SpellSettings.hpp"

// Resolve learned skills afresh; no native ability pointer survives a frame or map transition.
class NativeSpellSlots {
public:
    void Configure(uintptr_t base);
    void Reset() {loaded_=false;slots_=HeroSpellSlots();}
    int Ability(wc3::Handle unit,int slot,const SpellSettings& settings);
    int Order(wc3::Handle unit,int ability);
private:
    bool loaded_=false,verified_=false;
    uintptr_t base_=0;
    HeroSpellSlots slots_;
};
