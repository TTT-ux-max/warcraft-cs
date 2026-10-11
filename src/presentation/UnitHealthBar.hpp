#pragma once

// A frame owns values, not native handles: rendering cannot dereference an unloaded unit.
struct UnitHealthBar {
    float position[3]={};
    float health=0, maximum=0;
    bool ally=false;
};
