#include "NativeCameraFarClip.hpp"
#include "WarcraftApi.hpp"

namespace { constexpr float supportedMaximum=20000; }
void NativeCameraFarClip::Restore() {
    if(!owned_.maximum) return;
    auto current=wc3::FarClipMaximum(owned_.index,owned_.serial);
    // Resolve the generation again: map unloads can free/reuse the old address. Preserve foreign edits.
    if(current==owned_.maximum && *current==supportedMaximum) {
        *current=previous_;
        wc3::Log("FPS camera far limit restored maximum=%.0f",previous_);
    }
    owned_={};
}
void NativeCameraFarClip::Update(bool fps) {
    if(!fps) {Restore();return;}
    auto channel=wc3::FarClipChannel();
    if(channel.index==owned_.index && channel.serial==owned_.serial && channel.maximum==owned_.maximum) return;
    Restore();
    // The verified stock channel clamps both writes and evaluated values to 10000. Raise its local
    // ceiling only while FPS owns it; the configurable requested distance is still set by the camera.
    if(!channel.maximum || *channel.maximum!=10000) return;
    owned_=channel;previous_=*channel.maximum;*channel.maximum=supportedMaximum;
    wc3::Log("FPS camera far limit raised maximum=%.0f->%.0f channel=%08X/%08X",
        previous_,supportedMaximum,channel.index,channel.serial);
}
