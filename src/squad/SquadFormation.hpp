#pragma once
#include <cmath>
#include <cstddef>
#include <algorithm>

struct SquadDestination {float x=0,y=0;bool yielding=false;};

// Rear slots and side waypoints are portable geometry; Warcraft still owns unit pathfinding.
struct SquadFormation {
    static SquadDestination Destination(size_t index,float x,float y,float ux,float uy,float yaw,
        float vx,float vy,float follow,float clearance,float ahead,bool rear,bool yield) {
        float length=std::hypot(vx,vy),angle=yaw*.01745329252f;
        float fx=length>1 ? vx/length : std::cos(angle),fy=length>1 ? vy/length : std::sin(angle);
        float rx=-fy,ry=fx,dx=ux-x,dy=uy-y,front=dx*fx+dy*fy,lateral=dx*rx+dy*ry;
        float side=index%2 ? -1.f : 1.f;
        // A follower in the travel corridor first exits sideways, then returns behind the leader.
        // Preserve its current side so a turn cannot send it across the player's feet.
        if(yield && front>-clearance && front<ahead && std::abs(lateral)<clearance) {
            if(std::abs(lateral)>1)side=lateral>0 ? 1.f : -1.f;
            float along=std::max(0.f,front);
            return {x+fx*along+rx*side*clearance*1.6f,y+fy*along+ry*side*clearance*1.6f,true};
        }
        if(!rear) {float a=float(index)*2.39996323f,r=follow+float(index/8)*60;return {x+std::cos(a)*r,y+std::sin(a)*r,false};}
        float back=follow+float(index/4)*clearance,lane=side*(clearance*.75f+float((index/2)%2)*clearance);
        // Every destination has negative forward projection, even with dozens of followers.
        return {x-fx*back+rx*lane,y-fy*back+ry*lane,false};
    }
};
