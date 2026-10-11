#pragma once
#include "../presentation/SpellHudSlot.hpp"
#include <cstdint>

// Read live map overrides and the simulation's cooldown clock without changing ability state.
namespace NativeAbilityInfo {
void Read(uintptr_t ability,SpellHudSlot& slot,float mana=-1);
bool Definition(uintptr_t base,int ability,int& column,int& row,std::string& icon);
}
