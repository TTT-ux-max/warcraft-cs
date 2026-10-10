#pragma once
#include "WarcraftApi.hpp"
#include "../config/HealthBarSettings.hpp"
#include "../presentation/UnitHealthBar.hpp"
#include "../geometry/RayBounds.hpp"
#include <map>

// Sample live native units on the game thread; models and snapshots belong to the current map.
class UnitHealthBars {
public:
    void Update(wc3::Handle actor,const HealthBarSettings& settings);
    void Reset() { items_.clear();models_.clear(); }
    const std::vector<UnitHealthBar>& Items() const { return items_; }
private:
    void Anchor(wc3::Handle unit,float* position);
    std::map<int,Bounds3> models_;
    std::vector<UnitHealthBar> items_;
};
