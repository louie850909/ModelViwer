#include "pch.h"
#include "MeshLoader.h"
#include "MeshLoaderAssimp.h"
#include "MeshLoaderGltf.h"
#include <filesystem>

std::shared_ptr<Mesh> MeshLoader::Load(const std::string& path) {
    std::string ext = std::filesystem::path(path).extension().string();
    for (auto& c : ext) c = (char)tolower(c);

    if (ext == ".gltf" || ext == ".glb")
        return MeshLoaderGltf::Load(path);
    else
        return MeshLoaderAssimp::Load(path); // .fbx, .obj, .vrm ...
}
