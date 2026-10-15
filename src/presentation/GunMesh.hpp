#pragma once
#include <windows.h>
#include <string>
#include <vector>

class Canvas;

// A weapon/buy-preview mesh cache rendered through the backend-neutral Canvas. GPU handles are
// opaque (GL names or D3D textures); ownership follows the active canvas's generation, so a
// Reset with deleteTextures needs the canvas that created them.
class GunMesh {
public:
    bool Load(const std::string& path, Canvas& canvas);
    void Draw(const char* animation, float seconds, Canvas& canvas);
    // Hand-free private caches fit the complete weapon in the buy menu's preview rectangle.
    void DrawPreview(float x,float y,float width,float height,Canvas& canvas);
    void Reset(bool deleteTextures, Canvas* canvas);
    bool TexturesValid(Canvas& canvas) const;
private:
    struct Vertex { float x, y, z, u, v; unsigned bone; };
    struct Mesh { unsigned texture; std::vector<Vertex> vertices; };
    struct Sequence { std::string name; float fps; unsigned frames; std::vector<float> bones; };
    unsigned boneCount_ = 0;
    std::vector<uintptr_t> textures_;
    std::vector<Mesh> meshes_;
    std::vector<Sequence> sequences_;
};
