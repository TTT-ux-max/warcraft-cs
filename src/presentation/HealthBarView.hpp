#pragma once
#include "UnitHealthBar.hpp"
#include "WorldLabelView.hpp"
#include <algorithm>
#include <cmath>

struct HealthBarPlacement { float x=0,y=0,depth=0,fraction=0; };
namespace HealthBarView {
inline bool Project(const UnitHealthBar& bar,const WorldLabelView& view,float width,float height,
    float nearPlane,float farPlane,HealthBarPlacement& result) {
    if(!view.active || width<=0 || height<=0 || !std::isfinite(bar.health) || !std::isfinite(bar.maximum) ||
        bar.health<=.405f || bar.maximum<=0 || nearPlane<=0 || farPlane<=nearPlane) return false;
    constexpr float radians=.01745329252f;
    float x=bar.position[0]-view.eye[0],y=bar.position[1]-view.eye[1],z=bar.position[2]-view.eye[2];
    if(!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) ||
        x*x+y*y+z*z>view.maximumDistance*view.maximumDistance) return false;
    float cy=std::cos(view.yaw*radians),sy=std::sin(view.yaw*radians),cp=std::cos(view.pitch*radians),sp=std::sin(view.pitch*radians);
    float along=x*cy+y*sy,forward=along*cp+z*sp,right=x*sy-y*cy,up=-along*sp+z*cp;
    if(forward<=nearPlane || forward>=farPlane) return false;
    float tangent=std::tan(view.verticalFov*radians*.5f);
    if(!std::isfinite(tangent) || tangent<=0 || view.aspect<=0) return false;
    result.x=width*.5f*(1+right/(forward*tangent*view.aspect));
    result.y=height*.5f*(1-up/(forward*tangent));
    // Match the native perspective depth so opaque terrain/models can occlude the billboard.
    result.depth=farPlane/(farPlane-nearPlane)-farPlane*nearPlane/((farPlane-nearPlane)*forward);
    result.fraction=std::clamp(bar.health/bar.maximum,0.f,1.f);
    return result.x>=0 && result.x<=width && result.y>=0 && result.y<=height;
}
}
