#pragma once
#include "Mesh.h"
#include <string>
#include <memory>

class MeshLoaderGltf {
public:
    static std::shared_ptr<Mesh> Load(const std::string& path);
};
