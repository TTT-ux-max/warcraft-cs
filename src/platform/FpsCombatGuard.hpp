#pragma once
#include "WarcraftApi.hpp"

// Intercept native basic-attack validation/dispatch before pursuit, without serializing disabled counters or ranges.
namespace FpsCombatGuard {
using HookInstaller=bool (*)(void*,void*,void**);
void Install(uintptr_t base,wc3::Handle (*actor)(),bool (*passiveUnit)(uintptr_t),bool (*passiveAttack)(uintptr_t),HookInstaller hook);
void CancelOrders(wc3::Handle unit);
void CancelAttacks(wc3::Handle unit);
bool HasAttackTarget(wc3::Handle unit);
}
