#pragma once
#include "Mesh.h"
#include <string>
#include <memory>
#include <vector>

class MeshLoaderAssimp {
public:
    static std::shared_ptr<Mesh> Load(const std::string& path);

private:
    // Forward declaration; defined in .cpp alongside assimp headers
    struct aiNode;
    static void ParseNode(aiNode* node, int parentIndex, std::vector<SceneNode>& outNodes);
};
