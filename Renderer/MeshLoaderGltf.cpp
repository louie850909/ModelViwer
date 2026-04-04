#include "pch.h"
#include "MeshLoaderGltf.h"

// tinygltf + stb definitions isolated to this TU only
#pragma warning(push)
#pragma warning(disable: 4996)
#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <tiny_gltf.h>
#pragma warning(pop)

#include <filesystem>

namespace {
    void ParseGltfNode(const tinygltf::Model& model, int nodeIndex, int parentIndex,
                       std::vector<SceneNode>& outNodes,
                       const std::vector<std::vector<int>>& gltfMeshToSubMeshes)
    {
        int currentIndex = (int)outNodes.size();
        SceneNode sn;
        const auto& gltfNode = model.nodes[nodeIndex];
        sn.name = gltfNode.name;
        if (sn.name.empty()) sn.name = "Node_" + std::to_string(nodeIndex);
        sn.parentIndex = parentIndex;

        if (gltfNode.translation.size() == 3) {
            sn.t[0] = static_cast<float>(gltfNode.translation[0]);
            sn.t[1] = static_cast<float>(gltfNode.translation[1]);
            sn.t[2] = static_cast<float>(gltfNode.translation[2]);
        }
        if (gltfNode.rotation.size() == 4) {
            sn.r[0] = static_cast<float>(gltfNode.rotation[0]);
            sn.r[1] = static_cast<float>(gltfNode.rotation[1]);
            sn.r[2] = static_cast<float>(gltfNode.rotation[2]);
            sn.r[3] = static_cast<float>(gltfNode.rotation[3]);
        }
        if (gltfNode.scale.size() == 3) {
            sn.s[0] = static_cast<float>(gltfNode.scale[0]);
            sn.s[1] = static_cast<float>(gltfNode.scale[1]);
            sn.s[2] = static_cast<float>(gltfNode.scale[2]);
        }

        if (gltfNode.mesh >= 0 && gltfNode.mesh < (int)gltfMeshToSubMeshes.size()) {
            for (int subIdx : gltfMeshToSubMeshes[gltfNode.mesh])
                sn.subMeshIndices.push_back(subIdx);
        }

        outNodes.push_back(sn);

        for (int childIndex : gltfNode.children)
            ParseGltfNode(model, childIndex, currentIndex, outNodes, gltfMeshToSubMeshes);
    }
} // namespace

std::shared_ptr<Mesh> MeshLoaderGltf::Load(const std::string& path) {
    tinygltf::TinyGLTF loader;
    tinygltf::Model    model;
    std::string err, warn;

    bool ok = path.ends_with(".glb")
        ? loader.LoadBinaryFromFile(&model, &err, &warn, path)
        : loader.LoadASCIIFromFile(&model, &err, &warn, path);
    if (!ok) return nullptr;

    auto mesh = std::make_shared<Mesh>();
    std::string baseDir = std::filesystem::path(path).parent_path().string() + "/";

    for (const auto& mat : model.materials) {
        std::string texPath, mrPath;
        if (mat.pbrMetallicRoughness.baseColorTexture.index >= 0) {
            int texIdx = mat.pbrMetallicRoughness.baseColorTexture.index;
            int imgIdx = model.textures[texIdx].source;
            texPath = baseDir + model.images[imgIdx].uri;
        }
        if (mat.pbrMetallicRoughness.metallicRoughnessTexture.index >= 0) {
            int texIdx = mat.pbrMetallicRoughness.metallicRoughnessTexture.index;
            int imgIdx = model.textures[texIdx].source;
            mrPath = baseDir + model.images[imgIdx].uri;
        }
        mesh->texturePaths.push_back(texPath);
        mesh->metallicRoughnessPaths.push_back(mrPath);
    }

    auto GetStride = [](const tinygltf::Accessor& acc, const tinygltf::BufferView& view) -> size_t {
        if (view.byteStride != 0) return view.byteStride;
        return tinygltf::GetComponentSizeInBytes(acc.componentType) *
               tinygltf::GetNumComponentsInType(acc.type);
    };

    std::vector<std::vector<int>> gltfMeshToSubMeshes(model.meshes.size());
    int currentSubMeshIdx = 0;

    for (size_t m = 0; m < model.meshes.size(); ++m) {
        for (const auto& prim : model.meshes[m].primitives) {
            UINT vertexOffset = (UINT)mesh->vertices.size();

            auto& posAcc    = model.accessors[prim.attributes.at("POSITION")];
            auto& posView   = model.bufferViews[posAcc.bufferView];
            size_t posStride = GetStride(posAcc, posView);
            const uint8_t* posBase = model.buffers[posView.buffer].data.data()
                                   + posView.byteOffset + posAcc.byteOffset;

            const uint8_t* nrmBase  = nullptr; size_t nrmStride = 0;
            if (prim.attributes.count("NORMAL")) {
                auto& nrmAcc  = model.accessors[prim.attributes.at("NORMAL")];
                auto& nrmView = model.bufferViews[nrmAcc.bufferView];
                nrmStride = GetStride(nrmAcc, nrmView);
                nrmBase   = model.buffers[nrmView.buffer].data.data()
                          + nrmView.byteOffset + nrmAcc.byteOffset;
            }

            const uint8_t* uvBase = nullptr; size_t uvStride = 0;
            if (prim.attributes.count("TEXCOORD_0")) {
                auto& uvAcc  = model.accessors[prim.attributes.at("TEXCOORD_0")];
                auto& uvView = model.bufferViews[uvAcc.bufferView];
                uvStride = GetStride(uvAcc, uvView);
                uvBase   = model.buffers[uvView.buffer].data.data()
                         + uvView.byteOffset + uvAcc.byteOffset;
            }

            for (size_t i = 0; i < posAcc.count; i++) {
                Vertex v;
                const float* p = (const float*)(posBase + i * posStride);
                v.position = { p[0], p[1], p[2] };
                v.normal   = nrmBase
                    ? [&]{ const float* n = (const float*)(nrmBase + i * nrmStride); return DirectX::XMFLOAT3(n[0], n[1], n[2]); }()
                    : DirectX::XMFLOAT3(0, 1, 0);
                v.uv       = uvBase
                    ? [&]{ const float* u = (const float*)(uvBase + i * uvStride); return DirectX::XMFLOAT2(u[0], u[1]); }()
                    : DirectX::XMFLOAT2(0, 0);
                mesh->vertices.push_back(v);
            }

            SubMesh sub;
            sub.indexOffset   = (UINT)mesh->indices.size();
            sub.materialIndex = prim.material;
            sub.isTransparent = (prim.material >= 0 &&
                model.materials[prim.material].alphaMode == "BLEND");

            auto& idxAcc  = model.accessors[prim.indices];
            auto& idxView = model.bufferViews[idxAcc.bufferView];
            const uint8_t* idxRaw = model.buffers[idxView.buffer].data.data()
                                  + idxView.byteOffset + idxAcc.byteOffset;
            sub.indexCount = (UINT)idxAcc.count;

            for (size_t i = 0; i < idxAcc.count; i++) {
                if (idxAcc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
                    mesh->indices.push_back(((const uint16_t*)idxRaw)[i] + vertexOffset);
                else if (idxAcc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT)
                    mesh->indices.push_back(((const uint32_t*)idxRaw)[i] + vertexOffset);
            }
            mesh->subMeshes.push_back(sub);
            gltfMeshToSubMeshes[m].push_back(currentSubMeshIdx++);
        }
    }

    int defaultScene = model.defaultScene > -1 ? model.defaultScene : 0;
    if (defaultScene < (int)model.scenes.size()) {
        for (int nodeIdx : model.scenes[defaultScene].nodes)
            ParseGltfNode(model, nodeIdx, -1, mesh->nodes, gltfMeshToSubMeshes);
    }
    return mesh;
}
