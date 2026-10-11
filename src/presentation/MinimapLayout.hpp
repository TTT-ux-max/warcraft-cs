#pragma once
#include <algorithm>
#include <cmath>

// Shared pixel bounds keep top hints out of the minimap, including resized windows.
struct MinimapLayout {
    int x=0,y=0,side=0,padding=0;
    static MinimapLayout Calculate(int width,int height,float percent) {
        if (width<4 || height<4) return {};
        if (!std::isfinite(percent)) percent=22;
        MinimapLayout result;
        result.padding=std::max(1,std::min({std::max(8,int(std::lround(height*.018f))),width/4,height/4}));
        result.side=std::max(1,std::min({int(std::lround(height*std::clamp(percent,10.f,40.f)*.01f)),
            width-2*result.padding,height-2*result.padding}));
        result.x=width-result.padding-result.side;result.y=result.padding;
        return result;
    }
};
