#include "SpellCaster.hpp"

void SpellCaster::Cancel() {
    // A new deliberate player action interrupts windup/channeling, without issuing stop to idle actors.
    if(order_ && unit_ && wc3::GetUnitTypeId(unit_))wc3::IssueImmediateOrderById(unit_,851972);
    if(order_)wc3::Log("Spell interrupted unit=%08X order=%d",unit_,order_);
    unit_=0;order_=0;
}
void SpellCaster::Update() {
    if(!order_)return;
    int current=wc3::GetUnitCurrentOrder(unit_);
    // Give the engine one short dispatch window; thereafter completion/cancellation returns FPS ownership.
    if(current!=order_ && GetTickCount()-requested_>=100) {
        wc3::Log("Spell order finished unit=%08X issued=%d current=%d mana=%.1f",unit_,order_,current,wc3::Real(wc3::GetUnitState(unit_,2)));
        unit_=0;order_=0;
    }
}
const char* SpellCaster::Cast(wc3::Handle unit,int slot,const SpellSettings& settings,const SpellAim& aim,bool self) {
    if(slot<0 || slot>=4)return "INVALID SPELL SLOT";
    if(!settings.enabled)return "SPELLS DISABLED IN CONFIG";
    int ability=slots_.Ability(unit,slot,settings);
    if(!ability || wc3::GetUnitAbilityLevel(unit,ability)<=0) {
        wc3::Log("Spell unavailable unit=%08X slot=%d ability=%08X reason=empty-or-unlearned",unit,slot+1,ability);
        return "SPELL SLOT EMPTY OR NOT LEARNED";
    }
    int order=settings.order[slot] ? settings.order[slot] : slots_.Order(unit,ability);
    if(!order) {wc3::Log("Spell unavailable unit=%08X slot=%d ability=%08X reason=passive-or-unsupported",unit,slot+1,ability);return "PASSIVE OR UNSUPPORTED SPELL";}
    Cancel();
    const auto& mode=settings.target[slot];BOOL accepted=FALSE;
    // Auto tries legal target forms only. Native rejection never grants mana or resets cooldowns.
    wc3::Handle target=self || mode=="self" ? unit : aim.target;
    float x=aim.x,y=aim.y;
    if((mode=="unit" || mode=="self" || mode=="auto") && target)accepted=wc3::IssueTargetOrderById(unit,order,target);
    if(!accepted && !self && (mode=="point" || mode=="auto") && aim.point)accepted=wc3::IssuePointOrderById(unit,order,&x,&y);
    if(!accepted && (mode=="immediate" || mode=="auto"))accepted=wc3::IssueImmediateOrderById(unit,order);
    wc3::Log("Spell requested unit=%08X slot=%d ability=%08X order=%d mode=%s target=%08X point=%d x=%.1f y=%.1f accepted=%d mana=%.1f",
        unit,slot+1,ability,order,mode.c_str(),target,aim.point,x,y,accepted,wc3::Real(wc3::GetUnitState(unit,2)));
    if(!accepted)return "SPELL REJECTED: CHECK TARGET, MANA OR COOLDOWN";
    unit_=unit;order_=order;requested_=GetTickCount();return "SPELL ORDER ISSUED";
}
