#pragma once

class Canvas;

// Scope presentation is independent of weapon state and Warcraft camera control.
class ScopeView {
public:
    static void Draw(Canvas& canvas, float width, float height);
};
