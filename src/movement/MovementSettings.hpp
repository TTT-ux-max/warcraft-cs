#pragma once

// GoldSrc units are converted only by physics; collision uses Warcraft world units.
struct MovementSettings {
    // Optional absolute unit-speed scaling replaces the legacy relative slow/haste modifier.
    bool scaleFromUnitMoveSpeed=false;
    float unitMoveSpeedReference=300;
    bool bunnyHop = true;
    bool autoJump = true;
    // Classic strafing supplies the default gain; a separate arcade takeoff boost remains optional.
    float jumpBoostPercent = 0;
    float maxBunnySpeed = 1000;
    float airAcceleration = 10;
    float jumpSpeed = 268.328f;
    float gravity = 800;
    float stepHeight = 27;
};
