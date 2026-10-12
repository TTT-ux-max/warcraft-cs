#pragma once
#include <array>
#include <cstdint>

// Actual learned ability instances, sampled without retaining pointers beyond this call.
namespace NativeAbilitySlots {
std::array<int,4> Read(uintptr_t base,uintptr_t unit);
}
