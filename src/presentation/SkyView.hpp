#pragma once
#include "Canvas.hpp"
#include <string>

// Render CS art into untouched far-depth pixels; terrain/buildings keep their native depth and textures.
class SkyView {
public:
    void Draw(Canvas& canvas,const std::string& root,const std::string& name,float yaw,float pitch,float fov,float aspect);
    void Reset(bool deleteObject,Canvas* canvas);
    bool Valid(Canvas& canvas) const;
private:
    bool Load(const std::string& filename,Canvas& canvas);
    uintptr_t sky_=0;
    std::string name_;
    bool attempted_=false;
};
