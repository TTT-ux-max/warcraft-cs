#include "SkyView.hpp"
#include "Canvas.hpp"
#include "../platform/WarcraftApi.hpp"
#include <fstream>
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstring>

void SkyView::Reset(bool deleteObject,Canvas* canvas) {
    // Deleted/replaced contexts invalidate the handle without transferring its ownership.
    if (deleteObject && canvas && sky_) canvas->DestroySky(sky_);
    sky_=0;name_.clear();attempted_=false;
}
bool SkyView::Valid(Canvas& canvas) const {
    return !sky_ || canvas.TextureValid(sky_);
}
bool SkyView::Load(const std::string& filename,Canvas& canvas) {
    std::ifstream file(filename,std::ios::binary);char magic[4];unsigned width=0,height=0;
    if (!file.read(magic,4) || memcmp(magic,"WCS1",4) || !file.read(reinterpret_cast<char*>(&width),4) ||
        !file.read(reinterpret_cast<char*>(&height),4) || !width || width!=height || width>2048) return false;
    const size_t size=size_t(width)*height*4;
    file.seekg(0,std::ios::end);if (file.tellg()!=std::streamoff(12+size*6)) return false;
    file.seekg(12);
    // Caches follow GoldSrc rt/lf/bk/ft/up/dn. The canvas owns the backend mapping (OpenGL
    // cube targets X/Y/Z, D3D box faces); the roof/floor rotation below is file-format
    // specific and preserves the original horizon edge joins for both backends.
    std::vector<std::vector<unsigned char>> faces(6);
    for (unsigned face=0;face<6;++face) {
        faces[face].resize(size);
        if (!file.read(reinterpret_cast<char*>(faces[face].data()),std::streamsize(size))) { Reset(true,&canvas);return false; }
        if (face>=4) {
            std::vector<unsigned char> rotated(size);
            for (unsigned y=0;y<height;++y) for (unsigned x=0;x<width;++x) {
                unsigned sourceX=face==4 ? width-1-y : y;
                unsigned sourceY=face==4 ? x : height-1-x;
                memcpy(rotated.data()+(size_t(y)*width+x)*4,faces[face].data()+(size_t(sourceY)*width+sourceX)*4,4);
            }
            faces[face].swap(rotated);
        }
    }
    const unsigned char* faceData[6];
    for (int face=0;face<6;++face) faceData[face]=faces[face].data();
    sky_=canvas.CreateSky(faceData,int(width));
    if (!sky_) return false;
    wc3::Log("CS sky loaded %s size=%u",filename.c_str(),width);return true;
}
void SkyView::Draw(Canvas& canvas,const std::string& root,const std::string& name,float yaw,float pitch,float fov,float aspect) {
    if (name_!=name) { Reset(true,&canvas);name_=name; }
    if (!attempted_) { attempted_=true;Load(root+"\\assets\\skies\\"+name+".wcs",canvas); }
    if (sky_) canvas.DrawSky(sky_,yaw,pitch,fov,aspect);
}
