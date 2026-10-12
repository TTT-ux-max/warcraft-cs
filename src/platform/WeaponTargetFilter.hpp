#pragma once
#include "WarcraftApi.hpp"

// Classify native effect units separately from physical bodies before a ray selects its nearest hit.
namespace WeaponTargetFilter {
bool PassThrough(wc3::Handle unit,uintptr_t gameBase);
}
