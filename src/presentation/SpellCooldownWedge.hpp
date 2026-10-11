#pragma once
#include <algorithm>
#include <cmath>
#include <vector>
#include <array>

// A square-bounded clockwise sweep matches Warcraft's cooldown presentation without estimating time.
inline std::vector<std::array<float,2>> SpellCooldownWedge(float remaining,float total) {
    if(!std::isfinite(remaining) || !std::isfinite(total) || remaining<=0 || total<=0)return {};
    float fraction=std::clamp(remaining/total,0.f,1.f);
    std::vector<std::array<float,2>> vertices={{{.5f,.5f}}};
    int count=std::max(1,int(std::ceil(fraction*64)));
    for(int i=0;i<=count;++i) {
        float angle=-1.57079632679f+6.28318530718f*fraction*float(i)/count;
        float x=std::cos(angle),y=std::sin(angle),scale=.5f/std::max(std::abs(x),std::abs(y));
        vertices.push_back({.5f+x*scale,.5f+y*scale});
    }
    return vertices;
}
