#pragma once
#include "WarcraftApi.hpp"

// Intercept native basic-attack targets and dispatch before pursuit, preserving saved counters and ranges.
namespace FpsCombatGuard {
using HookInstaller=bool (*)(void*,void*,void**);
void Install(uintptr_t base,wc3::Handle (*actor)(),bool (*passiveUnit)(uintptr_t),bool (*passiveAttack)(uintptr_t),HookInstaller hook);
void CancelOrders(wc3::Handle unit);
void CancelAttacks(wc3::Handle unit);
bool HasAttackTarget(wc3::Handle unit);
// Native first-person play issues its own attack/smart orders from the reticle. That player intent
// passes the guard for a short window (refreshed while the order lives); map scripts, retaliation
// and auto-acquisition keep being rejected, so the unit never fights on its own.
void HoldPlayerOrder(unsigned milliseconds);
void ReleasePlayerOrder();
}
