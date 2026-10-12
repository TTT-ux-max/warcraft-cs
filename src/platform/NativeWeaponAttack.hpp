#pragma once
#include "WarcraftApi.hpp"

// Adapt a confirmed FPS contact to the unit's own attack callbacks, without issuing a chase order.
namespace NativeWeaponAttack {
using HookInstaller=bool (*)(void*,void*,void**);
void Install(uintptr_t base,HookInstaller hook);
bool Apply(wc3::Handle attacker,wc3::Handle target,float damage,float contactDistance);
int AttackType(wc3::Handle attacker);
}
