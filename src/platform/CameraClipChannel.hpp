#pragma once
#include <cstdint>

namespace wc3 {
// Native generation keys allow a saved limit to be restored without dereferencing a freed map camera.
struct CameraClipChannel {
    uint32_t index=0, serial=0;
    float* maximum=nullptr;
};
}
