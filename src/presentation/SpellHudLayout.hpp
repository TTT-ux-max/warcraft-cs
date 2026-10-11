#pragma once
#include <algorithm>
#include <cmath>

struct SpellHudLayout {
    int x=0,y=0,side=0,gap=0;
    static SpellHudLayout Calculate(int width,int height,float percent) {
        // Leave the existing bottom status/fuse row clear, and fit all four slots on narrow windows.
        if(width<160 || height<160)return {};
        int pad=std::max(12,int(height*.022f)),gap=std::max(4,int(height*.009f));
        int side=std::min(int(std::lround(height*percent/100)),(width-2*pad-3*gap)/4);
        side=std::max(16,side);int bottom=std::max(54,int(height*.06f));
        return {width-pad-4*side-3*gap,height-bottom-side,side,gap};
    }
};
