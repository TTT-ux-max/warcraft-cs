#include "GunMesh.hpp"
#include "Canvas.hpp"
#include "../platform/WarcraftApi.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>

template<class T> static void Read(std::ifstream& file, T& value) {
    file.read(reinterpret_cast<char*>(&value), sizeof(value));
    if (!file) throw std::runtime_error("truncated mesh cache");
}
bool GunMesh::Load(const std::string& path, Canvas& canvas) {
    Reset(true, &canvas);
    // Generated cache counts are bounded before allocation or GPU upload.
    try {
        std::ifstream file(path, std::ios::binary);
        if (!file) return false;
        unsigned magic, textures, meshes, sequences;
        Read(file, magic); Read(file, textures); Read(file, meshes); Read(file, sequences); Read(file, boneCount_);
        if (magic != 0x32474357 || textures > 128 || meshes > 256 || sequences > 2048 || !boneCount_ || boneCount_ > 128)
            throw std::runtime_error("invalid mesh cache header");
        for (unsigned i = 0; i < textures; ++i) {
            unsigned width, height; Read(file, width); Read(file, height);
            if (!width || !height || width > 2048 || height > 2048) throw std::runtime_error("invalid skin size");
            std::vector<unsigned char> pixels(width * height * 4);
            file.read(reinterpret_cast<char*>(pixels.data()), pixels.size());
            if (!file) throw std::runtime_error("truncated skin");
            // The canvas owns GPU upload, filtering and the backend-specific handle type.
            uintptr_t texture = canvas.CreateTexture(pixels.data(), int(width), int(height));
            if (!texture) throw std::runtime_error("skin upload failed");
            textures_.push_back(texture);
        }
        for (unsigned i = 0; i < meshes; ++i) {
            Mesh mesh; unsigned count; Read(file, mesh.texture); Read(file, count);
            if (mesh.texture >= textures || count > 100000 || count % 3) throw std::runtime_error("invalid triangle list");
            mesh.vertices.resize(count); file.read(reinterpret_cast<char*>(mesh.vertices.data()), count * sizeof(Vertex));
            for (const auto& v : mesh.vertices) if (v.bone >= boneCount_) throw std::runtime_error("invalid vertex bone");
            meshes_.push_back(std::move(mesh));
        }
        for (unsigned i = 0; i < sequences; ++i) {
            char label[32]; file.read(label, 32); label[31] = 0;
            Sequence sequence; sequence.name = label; Read(file, sequence.fps); Read(file, sequence.frames);
            if (!sequence.frames || sequence.frames > 10000 || sequence.fps <= 0 || sequence.fps > 1000)
                throw std::runtime_error("invalid animation");
            sequence.bones.resize(sequence.frames * boneCount_ * 12);
            file.read(reinterpret_cast<char*>(sequence.bones.data()), sequence.bones.size() * sizeof(float));
            if (!file) throw std::runtime_error("truncated animation");
            sequences_.push_back(std::move(sequence));
        }
        wc3::Log("viewmodel loaded %s: %u skins, %u meshes, %u sequences", path.c_str(), textures, meshes, sequences);
        return true;
    } catch (const std::exception& e) { wc3::Log("viewmodel failed: %s", e.what()); return false; }
}
void GunMesh::Reset(bool deleteTextures, Canvas* canvas) {
    // Deleted contexts own no valid names; never delete recycled names from a new context.
    if (deleteTextures && canvas)
        for (uintptr_t texture : textures_) if (texture) canvas->DestroyTexture(texture);
    textures_.clear(); meshes_.clear(); sequences_.clear(); boneCount_ = 0;
}
bool GunMesh::TexturesValid(Canvas& canvas) const {
    // A focus change alone does not invalidate textures in a surviving context/device.
    return !textures_.empty() && std::all_of(textures_.begin(), textures_.end(),
        [&](uintptr_t texture) { return canvas.TextureValid(texture); });
}
void GunMesh::Draw(const char* animation, float seconds, Canvas& canvas) {
    if (sequences_.empty()) return;
    const Sequence* sequence = &sequences_.front();
    for (const auto& item : sequences_) if (item.name == animation) { sequence = &item; break; }
    float frame = seconds * sequence->fps;
    // One-shot actions settle into idle; idle itself loops smoothly.
    if (sequence->name.find("idle") == 0) frame = std::fmod(frame, float(sequence->frames));
    else if (frame >= sequence->frames - 1) {
        for (const auto& item : sequences_) if (item.name.find("idle") == 0) { sequence = &item; break; }
        frame = std::fmod(seconds * sequence->fps, float(sequence->frames));
    }
    unsigned f0 = std::min(unsigned(frame), sequence->frames - 1), f1 = std::min(f0 + 1, sequence->frames - 1);
    float weight = frame - float(f0);
    std::vector<float> bones(boneCount_ * 12);
    for (size_t i = 0; i < bones.size(); ++i) {
        float a = sequence->bones[f0 * bones.size() + i], b = sequence->bones[f1 * bones.size() + i];
        bones[i] = a + (b - a) * weight;
    }
    std::vector<float> xyzuv;
    for (const auto& mesh : meshes_) {
        if (mesh.texture >= textures_.size() || !textures_[mesh.texture]) continue;
        xyzuv.clear(); xyzuv.reserve(mesh.vertices.size() * 5);
        for (const auto& v : mesh.vertices) {
            const float* m = &bones[v.bone * 12];
            float x = m[0] * v.x + m[1] * v.y + m[2] * v.z + m[3];
            float y = m[4] * v.x + m[5] * v.y + m[6] * v.z + m[7];
            float z = m[8] * v.x + m[9] * v.y + m[10] * v.z + m[11];
            // CS viewmodels store the left-hand pose; mirror it for the default right-hand view.
            // Vertices stay in the OpenGL camera convention; D3D backends mirror z themselves.
            xyzuv.push_back(y); xyzuv.push_back(z); xyzuv.push_back(-x); xyzuv.push_back(v.u); xyzuv.push_back(v.v);
        }
        canvas.TexturedTriangles(textures_[mesh.texture], xyzuv.data(), int(xyzuv.size() / 5), 1, 1, 1, 1);
    }
}

void GunMesh::DrawPreview(float x,float y,float width,float height,Canvas& canvas) {
    if (sequences_.empty() || meshes_.empty()) return;
    // Freeze the original unsilenced idle pose, then fit its actual geometry rather than fixed weapon offsets.
    const Sequence* pose=&sequences_.front();
    for (const auto& sequence : sequences_) if (sequence.name=="idle_unsil" || sequence.name=="idle1_unsil") { pose=&sequence;break; }
    struct PreviewVertex { float x,y,z,u,v; };
    std::vector<std::vector<PreviewVertex>> geometry;
    float minimum[3]={1e9f,1e9f,1e9f},maximum[3]={-1e9f,-1e9f,-1e9f};
    for (const auto& mesh : meshes_) {
        std::vector<PreviewVertex> vertices;
        for (const auto& vertex : mesh.vertices) {
            const float* m=&pose->bones[vertex.bone*12];
            float p[3]={m[0]*vertex.x+m[1]*vertex.y+m[2]*vertex.z+m[3],
                m[4]*vertex.x+m[5]*vertex.y+m[6]*vertex.z+m[7],
                m[8]*vertex.x+m[9]*vertex.y+m[10]*vertex.z+m[11]};
            // A slight side angle shows thickness while keeping the familiar horizontal CS preview.
            p[0]+=.22f*p[1];
            for (int axis=0;axis<3;++axis) { minimum[axis]=std::min(minimum[axis],p[axis]);maximum[axis]=std::max(maximum[axis],p[axis]); }
            vertices.push_back({p[0],p[1],p[2],vertex.u,vertex.v});
        }
        geometry.push_back(std::move(vertices));
    }
    float scale=std::min(width/std::max(1.0f,maximum[0]-minimum[0]),height/std::max(1.0f,maximum[2]-minimum[2]))*.86f;
    std::vector<float> xyzuv;
    for (size_t i=0;i<meshes_.size();++i) {
        if (meshes_[i].texture >= textures_.size() || !textures_[meshes_[i].texture]) continue;
        xyzuv.clear(); xyzuv.reserve(geometry[i].size() * 5);
        for (const auto& vertex : geometry[i]) {
            // Screen-space placement with the [0,1] depth fraction used by the preview's own faces.
            xyzuv.push_back(x+width*.5f+(vertex.x-(maximum[0]+minimum[0])*.5f)*scale);
            xyzuv.push_back(y+height*.5f-(vertex.z-(maximum[2]+minimum[2])*.5f)*scale);
            xyzuv.push_back((vertex.y-(maximum[1]+minimum[1])*.5f)/std::max(1.0f,maximum[1]-minimum[1]));
            xyzuv.push_back(vertex.u); xyzuv.push_back(vertex.v);
        }
        canvas.PreviewTriangles(textures_[meshes_[i].texture], xyzuv.data(), int(xyzuv.size() / 5), 1, 1, 1, 1);
    }
}
