#include "../src/platform/NativeCameraFarClip.hpp"
#include "../src/platform/WarcraftApi.hpp"
#include "../src/presentation/FpsProjection.hpp"
#include <cassert>
#include <algorithm>
#include <cstdio>

namespace {
float limits[2]={10000,10000};
wc3::CameraClipChannel live={1,10,limits};
bool oldAlive=true;
}
namespace wc3 {
void Log(const char*,...) {}
CameraClipChannel FarClipChannel() {return live;}
float* FarClipMaximum(uint32_t index,uint32_t serial) {
    if(index==1 && serial==10 && oldAlive) return limits;
    if(index==2 && serial==20) return limits+1;
    return nullptr;
}
}
int main() {
    NativeCameraFarClip owner;
    // Reproduce the actual native scalar ceiling: the old requested-distance matcher cuts nearby ground.
    float nativeFar=std::min(15000.f,limits[0]);
    assert(nativeFar==10000 && FpsProjection::NearPlane(true,100,nativeFar,15000)==100);
    assert(FpsProjection::NearPlane(true,100,nativeFar,nativeFar)==8);
    owner.Update(true);assert(limits[0]==20000 && limits[1]==10000);
    nativeFar=std::min(15000.f,limits[0]);
    assert(nativeFar==15000 && FpsProjection::NearPlane(true,100,nativeFar,nativeFar)==8);
    assert(FpsProjection::NearPlane(true,100,1000,nativeFar)==100);
    owner.Update(true);owner.Update(false);assert(limits[0]==10000);
    owner.Update(true);live={2,20,limits+1};owner.Update(true);
    assert(limits[0]==10000 && limits[1]==20000);
    owner.Update(false);assert(limits[1]==10000);
    // Reused addresses with a stale generation must never be restored into the new camera.
    live={1,10,limits};owner.Update(true);oldAlive=false;live={1,11,limits};limits[0]=12345;
    owner.Update(false);assert(limits[0]==12345);
    oldAlive=true;live={1,10,limits};limits[0]=10000;owner.Update(true);limits[0]=14000;
    owner.Update(false);assert(limits[0]==14000);
    live={};owner.Update(true);owner.Update(false);
    std::puts("Native camera limit ownership, clamp regression, portrait isolation and stale/foreign restoration passed.");
}
