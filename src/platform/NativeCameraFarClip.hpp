#pragma once
#include "CameraClipChannel.hpp"

// Own only the active FPS camera channel's upper bound; preserve other cameras and native constants.
class NativeCameraFarClip {
public:
    void Update(bool fps);
private:
    void Restore();
    wc3::CameraClipChannel owned_;
    float previous_=0;
};
