#pragma once
#include "NativeSpellSlots.hpp"
#include "SpellAim.hpp"

// Own only explicitly issued spell orders; native mana/cooldown/target validation remains authoritative.
class SpellCaster {
public:
    void Configure(uintptr_t base) {slots_.Configure(base);}
    void Reset() {unit_=0;order_=0;slots_.Reset();}
    void Cancel();
    bool Active() const {return order_!=0;}
    void Update();
    const char* Cast(wc3::Handle unit,int slot,const SpellSettings& settings,const SpellAim& aim,bool self);
private:
    NativeSpellSlots slots_;
    wc3::Handle unit_=0;
    int order_=0;
    DWORD requested_=0;
};
