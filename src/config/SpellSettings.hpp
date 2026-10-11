#pragma once
#include <array>
#include <string>

// Empty ability slots follow the owned game's hero list; overrides support custom map abilities.
struct SpellSettings {
    bool enabled=true;
    float aimDistance=3000;
    std::array<std::string,4> ability{};
    std::array<int,4> order{};
    std::array<std::string,4> target={"auto","auto","auto","auto"};
};
