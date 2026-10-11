#pragma once
#include <algorithm>
#include <cmath>

// Scale CS tuning with live Warcraft statistics; invalid native values retain the base behavior.
namespace UnitScaling {
inline float FireInterval(float interval,bool enabled,float attackSpeed) {
    return enabled && std::isfinite(attackSpeed) && attackSpeed>0 ?
        interval/std::clamp(attackSpeed,.2f,5.f) : interval;
}
inline float MoveScale(bool enabled,float nativeSpeed,float reference,float modifier) {
    return enabled && std::isfinite(nativeSpeed) && nativeSpeed>=0 && reference>0 ?
        nativeSpeed/reference : modifier;
}
}
