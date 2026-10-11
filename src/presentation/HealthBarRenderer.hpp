#pragma once
#include "HealthBarView.hpp"
#include <vector>

// Draw against the world's depth before the first-person weapon clears its private depth pass.
namespace HealthBarRenderer {
unsigned Draw(const std::vector<UnitHealthBar>& bars,const WorldLabelView& view,int width,int height,float nearPlane,float farPlane);
}
