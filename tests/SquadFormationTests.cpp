#include "../src/squad/SquadFormation.hpp"
#include <cassert>
#include <cstdio>
#include <initializer_list>

int main() {
    // All slots remain strictly behind at every heading and recruit count, including strafing/backward travel.
    for(float yaw:{0.f,45.f,90.f,180.f,270.f,359.f})for(size_t i=0;i<64;++i) {
        float a=yaw*.01745329252f,fx=std::cos(a),fy=std::sin(a);
        auto p=SquadFormation::Destination(i,20,-30,-500,-500,yaw,0,0,180,100,350,true,false);
        assert((p.x-20)*fx+(p.y+30)*fy<=-179.99f);
        auto moving=SquadFormation::Destination(i,0,0,-500,-500,yaw,0,-300,180,100,350,true,false);
        assert(moving.y>0);
    }
    auto left=SquadFormation::Destination(0,0,0,130,20,0,0,0,180,100,350,true,true);
    auto right=SquadFormation::Destination(1,0,0,130,-20,0,0,0,180,100,350,true,true);
    assert(left.yielding && right.yielding && left.y>=150 && right.y<=-150);
    // After leaving the corridor, rejoin behind without crossing the leader; turning reverses the corridor.
    auto rejoin=SquadFormation::Destination(0,0,0,left.x,left.y,0,0,0,180,100,350,true,true);
    assert(!rejoin.yielding && rejoin.x<0);
    auto turn=SquadFormation::Destination(0,0,0,-130,20,180,0,0,180,100,350,true,true);
    assert(turn.yielding && turn.y>150);
    auto ahead=SquadFormation::Destination(0,0,0,600,0,0,0,0,180,100,350,true,true);assert(!ahead.yielding && ahead.x<0);
    std::puts("PASS rear formations across headings/counts and stable left/right corridor yielding.");
}
