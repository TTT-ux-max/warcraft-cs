#pragma once
#include <cstdint>
#include <vector>

// Decode owned archive images to bounded RGBA data, independent of OpenGL resource lifetime.
struct SpellIconImage {
    unsigned width=0,height=0;
    std::vector<uint8_t> pixels;
    bool Decode(const std::vector<uint8_t>& bytes);
};
