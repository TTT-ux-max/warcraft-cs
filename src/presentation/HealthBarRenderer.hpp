#pragma once
#include "HealthBarView.hpp"
#include <vector>

class Canvas;

// Draw against the world's depth before the first-person weapon clears its private depth pass.
namespace HealthBarRenderer {
unsigned Draw(Canvas& canvas,const std::vector<UnitHealthBar>& bars,const WorldLabelView& view,int width,int height,float nearPlane,float farPlane);
}
