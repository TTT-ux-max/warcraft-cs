#include "HealthBarRenderer.hpp"
#include "Canvas.hpp"
#include <windows.h>
#include <algorithm>

unsigned HealthBarRenderer::Draw(Canvas& canvas,const std::vector<UnitHealthBar>& bars,const WorldLabelView& view,int width,int height,float nearPlane,float farPlane) {
    if (bars.empty() || !view.active) return 0;
    canvas.BeginBars(width, height);
    unsigned submitted=0;float scale=std::clamp(height/1080.f,.65f,2.f),halfWidth=48*scale,halfHeight=5*scale;
    for (const auto& bar:bars) {
        HealthBarPlacement at;if(!HealthBarView::Project(bar,view,float(width),float(height),nearPlane,farPlane,at)) continue;
        // Constant screen size keeps distant bars readable; Z still belongs to their world anchor.
        float left=at.x-halfWidth,top=at.y-halfHeight,z=at.depth;
        auto quad=[&](float x,float y,float w,float h,float r,float g,float b) {
            canvas.BarQuad(x,y,w,h,z,r,g,b,1);
        };
        quad(left-scale,top-scale,halfWidth*2+scale*2,halfHeight*2+scale*2,.03f,.03f,.03f);
        quad(left,top,halfWidth*2,halfHeight*2,.18f,.18f,.18f);
        if(bar.ally) quad(left,top,halfWidth*2*at.fraction,halfHeight*2,.2f,.9f,.25f);
        else quad(left,top,halfWidth*2*at.fraction,halfHeight*2,.95f,.2f,.15f);
        ++submitted;
    }
    canvas.EndBars();
    return submitted;
}
