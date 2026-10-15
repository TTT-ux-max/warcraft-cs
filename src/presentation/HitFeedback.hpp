#pragma once
#include <windows.h>

class Canvas;

// Confirmed hits refresh one neutral marker without blood or particle allocations.
class HitFeedback {
public:
    void Record();
    void Clear();
    void Draw(Canvas& canvas, float width, float height) const;
private:
    DWORD tick_ = 0;
    bool active_ = false;
};
