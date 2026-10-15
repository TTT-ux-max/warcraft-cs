#pragma once
#include "../application/ShooterController.hpp"
#include <functional>

class Canvas;

namespace BuyMenuView {
// Text carries its own color: the old overlay set glColor4f before each call and the
// backend-agnostic canvas needs the tint at the call site.
using Text=std::function<void(float,float,const char*,float,float,float,float)>;
using Preview=std::function<void(int,float,float,float,float)>;
// Draw keyboard/mouse rows from the same layout used by input hit-testing.
void Draw(Canvas& canvas,const ShooterController& controller,int width,int height,HWND window,const Text& text,const Text& titleText,const Preview& preview);
}
