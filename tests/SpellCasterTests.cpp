#include "../src/platform/SpellCaster.hpp"
#include <cassert>
#include <cstring>
#include <cstdio>

namespace {int level=1,current=0,stops=0;bool unitAccepted=false,pointAccepted=false,immediateAccepted=false;
int calls=0;wc3::Handle lastTarget=0;float lastX=0;SpellCaster* activeCaster=nullptr;}
namespace wc3 {
void Log(const char*,...) {}
float Real(Bits) {return 100;}
int (__cdecl* GetUnitAbilityLevel)(Handle,int)=[](Handle,int) {return level;};
int (__cdecl* GetUnitTypeId)(Handle)=[](Handle) {return 1;};
int (__cdecl* GetUnitCurrentOrder)(Handle)=[](Handle) {return current;};
Bits (__cdecl* GetUnitState)(Handle,int)=[](Handle,int)->Bits {return 0;};
BOOL (__cdecl* IssueTargetOrderById)(Handle,int,Handle)=[](Handle,int order,Handle target)->BOOL {
    // The native motor can consult ownership synchronously inside this dispatch.
    assert(activeCaster && activeCaster->Active());++calls;lastTarget=target;if(unitAccepted)current=order;return unitAccepted;};
BOOL (__cdecl* IssuePointOrderById)(Handle,int,float*,float*)=[](Handle,int order,float* x,float*)->BOOL {++calls;lastX=*x;if(pointAccepted)current=order;return pointAccepted;};
BOOL (__cdecl* IssueImmediateOrderById)(Handle,int)=[](Handle,int order)->BOOL {
    if(order==851972) {++stops;current=0;return TRUE;}++calls;if(immediateAccepted)current=order;return immediateAccepted;};
}
int NativeSpellSlots::Ability(wc3::Handle,int slot,const SpellSettings&,bool) {return slot==3 ? 0 : slot+1;}
int NativeSpellSlots::Order(wc3::Handle,int ability) {return ability==3 ? 0 : 852095;}
int main() {
    SpellCaster caster;SpellSettings settings;SpellAim aim;aim.target=7;aim.point=true;aim.x=120;
    activeCaster=&caster;
    unitAccepted=true;caster.Cast(1,0,settings,aim,false);assert(caster.Active() && lastTarget==7 && calls==1);
    // An active native order survives updates; only a deliberate cancel sends stop.
    caster.Update();assert(caster.Active() && stops==0);caster.Cancel();assert(!caster.Active() && stops==1);
    unitAccepted=false;pointAccepted=true;calls=0;caster.Cast(1,0,settings,aim,false);assert(caster.Active() && calls==2 && lastX==120);
    caster.Reset();assert(!caster.Active() && stops==1); // Map reset never accesses unloaded actor handles.
    pointAccepted=false;immediateAccepted=true;calls=0;caster.Cast(1,0,settings,{},false);assert(calls==1 && caster.Active());caster.Cancel();
    unitAccepted=true;caster.Cast(1,0,settings,aim,true);assert(lastTarget==1);caster.Cancel();
    unitAccepted=pointAccepted=immediateAccepted=false;calls=0;caster.Cast(1,0,settings,aim,false);assert(!caster.Active() && calls==3);
    level=0;calls=0;caster.Cast(1,0,settings,aim,false);assert(calls==0 && !caster.Active());level=1;
    caster.Cast(1,2,settings,aim,false);caster.Cast(1,3,settings,aim,false);caster.Cast(1,-1,settings,aim,false);assert(calls==0);
    settings.enabled=false;caster.Cast(1,0,settings,aim,false);assert(calls==0);
    settings.enabled=true;settings.target[0]="unit";caster.Cast(1,0,settings,{},false);assert(calls==0);
    settings.target[0]="immediate";immediateAccepted=true;caster.Cast(1,0,settings,aim,false);assert(calls==1 && caster.Active());
    Sleep(110);current=0;caster.Update();assert(!caster.Active());
    std::puts("PASS native spell target forms, self cast, passive/unlearned rejection, order ownership and reset.");
}
