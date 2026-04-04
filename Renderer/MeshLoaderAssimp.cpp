#include "pch.h"
#include "MeshLoaderAssimp.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <filesystem>

namespace {
    void ParseAssimpNode(aiNode* node, int parentIndex, std::vector<SceneNode>& outNodes) {
        int currentIndex = (int)outNodes.size();
        SceneNode sn;
        sn.name = node->mName.C_Str();
        if (sn.name.empty()) sn.name = "Unnamed_Node";
        sn.parentIndex = parentIndex;

        aiVector3D scaling, position;
        aiQuaternion rotation;
        node->mTransformation.Decompose(scaling, rotation, position);

        sn.t[0] = position.x; sn.t[1] = position.y; sn.t[2] = position.z;
        sn.r[0] = rotation.x; sn.r[1] = rotation.y; sn.r[2] = rotation.z; sn.r[3] = rotation.w;
        sn.s[0] = scaling.x;  sn.s[1] = scaling.y;  sn.s[2] = scaling.z;

        for (unsigned int i = 0; i < node->mNumMeshes; i++)
            sn.subMeshIndices.push_back(node->mMeshes[i]);

        outNodes.push_back(sn);

        for (unsigned int i = 0; i < node->mNumChildren; i++)
            ParseAssimpNode(node->mChildren[i], currentIndex, outNodes);
    }
} // namespace

std::shared_ptr<Mesh> MeshLoaderAssimp::Load(const std::string& path) {
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(path,
        aiProcess_Triangulate |
        aiProcess_GenNormals   |
        aiProcess_FlipUVs      |
        aiProcess_JoinIdenticalVertices);

    if (!scene || !scene->HasMeshes()) return nullptr;

    auto mesh = std::make_shared<Mesh>();
    std::string baseDir = std::filesystem::path(path).parent_path().string() + "/";

    for (unsigned int i = 0; i < scene->mNumMaterials; i++) {
        aiMaterial* mat = scene->mMaterials[i];
        aiString texPath;
        if (mat->GetTexture(aiTextureType_DIFFUSE, 0, &texPath) == AI_SUCCESS)
            mesh->texturePaths.push_back(baseDir + texPath.C_Str());
        else
            mesh->texturePaths.push_back("");
    }

    for (unsigned int m = 0; m < scene->mNumMeshes; m++) {
        aiMesh* aiM = scene->mMeshes[m];
        SubMesh sub;
        sub.indexOffset  = (UINT)mesh->indices.size();
        sub.indexCount   = aiM->mNumFaces * 3;
        sub.materialIndex = aiM->mMaterialIndex;

        if (aiM->mMaterialIndex >= 0) {
            aiMaterial* mat = scene->mMaterials[aiM->mMaterialIndex];
            float opacity = 1.0f;
            mat->Get(AI_MATKEY_OPACITY, opacity);
            bool hasOpacityTex = (mat->GetTextureCount(aiTextureType_OPACITY) > 0);
            sub.isTransparent = (opacity < 1.0f) || hasOpacityTex;
        } else {
            sub.isTransparent = false;
        }

        UINT vertexOffset = (UINT)mesh->vertices.size();
        for (unsigned int i = 0; i < aiM->mNumVertices; i++) {
            Vertex v;
            v.position = { aiM->mVertices[i].x, aiM->mVertices[i].y, aiM->mVertices[i].z };
            v.normal   = aiM->HasNormals()
                ? DirectX::XMFLOAT3(aiM->mNormals[i].x, aiM->mNormals[i].y, aiM->mNormals[i].z)
                : DirectX::XMFLOAT3(0, 1, 0);
            v.uv       = aiM->HasTextureCoords(0)
                ? DirectX::XMFLOAT2(aiM->mTextureCoords[0][i].x, aiM->mTextureCoords[0][i].y)
                : DirectX::XMFLOAT2(0, 0);
            mesh->vertices.push_back(v);
        }

        for (unsigned int i = 0; i < aiM->mNumFaces; i++) {
            mesh->indices.push_back(aiM->mFaces[i].mIndices[0] + vertexOffset);
            mesh->indices.push_back(aiM->mFaces[i].mIndices[1] + vertexOffset);
            mesh->indices.push_back(aiM->mFaces[i].mIndices[2] + vertexOffset);
        }
        mesh->subMeshes.push_back(sub);
    }

    ParseAssimpNode(scene->mRootNode, -1, mesh->nodes);
    return mesh;
}
