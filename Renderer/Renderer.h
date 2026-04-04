#pragma once
#include "pch.h"
#include "GraphicsContext.h"
#include "Scene.h"
#include <atomic>
#include <chrono>
#include <mutex>
#include "GBuffer.h"

// Must match LightDef.hlsli (16-byte aligned)
struct LightCPU
{
    int   type;       // 0=Dir 1=Point 2=Spot
    float intensity;
    float coneAngle;  // radians
    float _pad;
    float color[3];
    float _pad2;
    float position[3];
    float _pad3;
    float direction[3];
    float _pad4;
};
static_assert(sizeof(LightCPU) == 64, "LightCPU size mismatch");

struct LightBufferCPU
{
    int   numLights;
    float _pad[3];
    LightCPU lights[16];
};
static_assert(sizeof(LightBufferCPU) == 16 + 64 * 16, "LightBufferCPU size mismatch");

class Renderer {
public:
    std::mutex m_renderMutex;

    bool Init(IUnknown* panelUnknown, int width, int height);
    void Resize(int width, int height, float scale);
    void RenderFrame();
    void Shutdown();

    int AddMesh(std::shared_ptr<Mesh> mesh) { return m_scene.AddMesh(mesh); }
    void RemoveMeshById(int meshId) {
        std::lock_guard<std::mutex> lock(m_renderMutex);
        m_ctx.WaitForGpu();
        m_scene.RemoveMeshById(meshId);
    }

    void UploadMeshToGpu(std::shared_ptr<Mesh> mesh, int meshId);

    void SetCameraTransform(float px, float py, float pz, float pitch, float yaw) { m_scene.SetCameraTransform(px, py, pz, pitch, yaw); }
    void GetStats(int& vertices, int& polygons, int& drawCalls, float& frameTimeMs);

    int  GetTotalNodeCount() { return m_scene.GetTotalNodeCount(); }
    bool GetNodeInfo(int globalIndex, std::string& outName, int& outParentGlobal) { return m_scene.GetNodeInfo(globalIndex, outName, outParentGlobal); }
    bool GetNodeTransform(int globalIndex, float* outT, float* outR, float* outS) { return m_scene.GetNodeTransform(globalIndex, outT, outR, outS); }
    bool SetNodeTransform(int globalIndex, const float* inT, const float* inR, const float* inS) { return m_scene.SetNodeTransform(globalIndex, inT, inR, inS); }
    std::shared_ptr<Mesh> GetMesh() const { return m_scene.GetMesh(); }

private:
    void CreateRootSignaturesAndPSOs();
    void CreateLightBuffer();  // allocates m_lightBuffer upload heap

    GraphicsContext m_ctx;
    Scene           m_scene;
    GBuffer         m_gBuffer;

    // Geometry Pass
    ComPtr<ID3D12RootSignature> m_geomRootSig;
    ComPtr<ID3D12PipelineState> m_geomPSO;

    // Lighting Pass
    ComPtr<ID3D12RootSignature> m_lightRootSig;
    ComPtr<ID3D12PipelineState> m_lightPSO;
    ComPtr<ID3D12Resource>      m_lightBuffer;        // upload heap, persistently mapped
    LightBufferCPU*             m_lightBufferMapped = nullptr;

    // Forward Transparent Pass
    ComPtr<ID3D12RootSignature> m_forwardRootSig;
    ComPtr<ID3D12PipelineState> m_transparentPSO;

    UINT m_srvDescriptorSize = 0;

    std::atomic<bool>  m_isShuttingDown{ false };
    std::atomic<int>   m_statVertices{ 0 };
    std::atomic<int>   m_statPolygons{ 0 };
    std::atomic<int>   m_statDrawCalls{ 0 };
    std::atomic<float> m_statFrameTime{ 0.0f };
    std::chrono::high_resolution_clock::time_point m_lastFrameTime;
};
