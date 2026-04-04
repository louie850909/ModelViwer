#include "pch.h"
#include "Renderer.h"
#include <stdexcept>
#include <filesystem>
#include <string>
#include <algorithm>
#include <stb_image.h>

#define CHECK(hr) if(FAILED(hr)) throw std::runtime_error("DX12 HRESULT failed")

std::wstring GetShaderPath(const std::wstring& filename) {
    wchar_t buffer[MAX_PATH];
    GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    std::filesystem::path exePath(buffer);
    return (exePath.parent_path() / L"shaders" / filename).wstring();
}

// ---------------------------------------------------------------------------
// LightPassConstants (b0) — matches DeferredLight.hlsl cbuffer LightPassConstants
// ---------------------------------------------------------------------------
struct LightPassConstants
{
    DirectX::XMFLOAT3 cameraPos;
    float _pad0;
    DirectX::XMFLOAT3 mainLightDir;
    float _pad1;
};
static_assert(sizeof(LightPassConstants) == 32, "LightPassConstants must be 32 bytes (8 DWORD)");

// ---------------------------------------------------------------------------
// Init / Shutdown / Resize
// ---------------------------------------------------------------------------
bool Renderer::Init(IUnknown* panelUnknown, int width, int height) {
    try {
        if (!m_ctx.Init(panelUnknown, width, height)) return false;

        CreateRootSignaturesAndPSOs();
        CreateLightBuffer();
        m_srvDescriptorSize = m_ctx.GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        m_gBuffer.Init(m_ctx.GetDevice(), width, height, m_ctx.GetDepthBuffer());
        m_lastFrameTime = std::chrono::high_resolution_clock::now();
        return true;
    }
    catch (...) { return false; }
}

void Renderer::Shutdown() {
    m_isShuttingDown = true;
    std::lock_guard<std::mutex> lock(m_renderMutex);
    m_ctx.WaitForGpu();
    if (m_lightBuffer && m_lightBufferMapped)
        m_lightBuffer->Unmap(0, nullptr);
    m_lightBufferMapped = nullptr;
    m_gBuffer.Shutdown();
    m_ctx.Shutdown();
}

void Renderer::Resize(int width, int height, float scale) {
    if (m_isShuttingDown) return;
    std::lock_guard<std::mutex> lock(m_renderMutex);
    if (m_isShuttingDown) { m_renderMutex.unlock(); return; }
    m_ctx.Resize(width, height, scale);
    if (m_ctx.GetDevice() != nullptr && width > 0 && height > 0)
        m_gBuffer.Resize(m_ctx.GetDevice(), m_ctx.GetWidth(), m_ctx.GetHeight(), m_ctx.GetDepthBuffer());
}

void Renderer::GetStats(int& vertices, int& polygons, int& drawCalls, float& frameTimeMs) {
    vertices    = m_statVertices.load(std::memory_order_relaxed);
    polygons    = m_statPolygons.load(std::memory_order_relaxed);
    drawCalls   = m_statDrawCalls.load(std::memory_order_relaxed);
    frameTimeMs = m_statFrameTime.load(std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
// CreateLightBuffer — upload heap, persistently mapped
// ---------------------------------------------------------------------------
void Renderer::CreateLightBuffer() {
    UINT64 size = (sizeof(LightBufferCPU) + 255) & ~255ULL; // 256-byte align
    auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    auto bufDesc   = CD3DX12_RESOURCE_DESC::Buffer(size);
    CHECK(m_ctx.GetDevice()->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &bufDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&m_lightBuffer)));
    m_lightBuffer->Map(0, nullptr, reinterpret_cast<void**>(&m_lightBufferMapped));

    // Default: one directional light pointing down-forward
    memset(m_lightBufferMapped, 0, sizeof(LightBufferCPU));
    m_lightBufferMapped->numLights      = 1;
    m_lightBufferMapped->lights[0].type      = 0;   // Directional
    m_lightBufferMapped->lights[0].intensity  = 1.0f;
    m_lightBufferMapped->lights[0].color[0]   = 1.0f;
    m_lightBufferMapped->lights[0].color[1]   = 1.0f;
    m_lightBufferMapped->lights[0].color[2]   = 1.0f;
    m_lightBufferMapped->lights[0].direction[0] =  0.3f;
    m_lightBufferMapped->lights[0].direction[1] = -1.0f;
    m_lightBufferMapped->lights[0].direction[2] =  0.5f;
}

// ---------------------------------------------------------------------------
// RenderFrame
// ---------------------------------------------------------------------------
void Renderer::RenderFrame() {
    if (m_isShuttingDown) return;

    auto now = std::chrono::high_resolution_clock::now();
    std::chrono::duration<float, std::milli> dt = now - m_lastFrameTime;
    m_lastFrameTime = now;
    m_statFrameTime.store(dt.count(), std::memory_order_relaxed);

    int currentDrawCalls = 0;
    int totalVerts = 0, totalPolys = 0;

    m_renderMutex.lock();
    if (m_isShuttingDown) { m_renderMutex.unlock(); return; }

    m_ctx.ResetCommandList();
    auto cmdList = m_ctx.GetCommandList();
    auto vp = m_ctx.GetViewport();
    auto sc = m_ctx.GetScissorRect();
    cmdList->RSSetViewports(1, &vp);
    cmdList->RSSetScissorRects(1, &sc);

    // ==========================================
    // Pass 1: Geometry Pass
    // ==========================================
    D3D12_CPU_DESCRIPTOR_HANDLE gbufferRTVs = m_gBuffer.GetRtvStart();
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = m_ctx.GetDSV();
    cmdList->OMSetRenderTargets(GBuffer::TargetCount, &gbufferRTVs, TRUE, &dsv);

    float clearGBuffer[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(gbufferRTVs);
    UINT rtvStride = m_ctx.GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    for (int i = 0; i < GBuffer::TargetCount; ++i) {
        cmdList->ClearRenderTargetView(rtvHandle, clearGBuffer, 0, nullptr);
        rtvHandle.Offset(1, rtvStride);
    }
    cmdList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    cmdList->SetGraphicsRootSignature(m_geomRootSig.Get());
    cmdList->SetPipelineState(m_geomPSO.Get());
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    using namespace DirectX;
    XMMATRIX rotation = XMMatrixRotationRollPitchYaw(m_scene.GetPitch(), m_scene.GetYaw(), 0.0f);
    XMVECTOR eye     = XMLoadFloat3(&m_scene.GetCameraPos());
    XMVECTOR forward = XMVector3TransformNormal(XMVectorSet(0, 0, 1, 0), rotation);
    XMVECTOR up      = XMVectorSet(0, 1, 0, 0);
    XMMATRIX view    = XMMatrixLookAtLH(eye, eye + forward, up);
    XMMATRIX proj    = XMMatrixPerspectiveFovLH(XMConvertToRadians(45.f), vp.Width / vp.Height, 0.1f, 5000.f);

    for (auto& inst : m_scene.GetMeshes()) {
        auto& mesh = inst.mesh;
        if (!mesh) continue;

        std::vector<XMMATRIX> globalTransforms(mesh->nodes.size());
        for (size_t i = 0; i < mesh->nodes.size(); ++i) {
            const auto& node = mesh->nodes[i];
            XMMATRIX local =
                XMMatrixScaling(node.s[0], node.s[1], node.s[2]) *
                XMMatrixRotationQuaternion(XMVectorSet(node.r[0], node.r[1], node.r[2], node.r[3])) *
                XMMatrixTranslation(node.t[0], node.t[1], node.t[2]);
            globalTransforms[i] = (node.parentIndex >= 0)
                ? local * globalTransforms[node.parentIndex]
                : local;
        }

        cmdList->IASetVertexBuffers(0, 1, &mesh->vbView);
        cmdList->IASetIndexBuffer(&mesh->ibView);
        ID3D12DescriptorHeap* heaps[] = { inst.srvHeap.Get() };
        cmdList->SetDescriptorHeaps(1, heaps);

        for (size_t n = 0; n < mesh->nodes.size(); ++n) {
            const auto& node = mesh->nodes[n];
            if (node.subMeshIndices.empty()) continue;

            XMMATRIX modelMat = globalTransforms[n];
            SceneConstants cb = {};
            XMStoreFloat4x4(&cb.mvp,          XMMatrixTranspose(modelMat * view * proj));
            XMStoreFloat4x4(&cb.modelMatrix,  XMMatrixTranspose(modelMat));
            XMStoreFloat4x4(&cb.normalMatrix, XMMatrixInverse(nullptr, modelMat));
            XMStoreFloat3(&cb.lightDir,       XMVector3Normalize(forward));
            cb.cameraPos = m_scene.GetCameraPos();
            cmdList->SetGraphicsRoot32BitConstants(0, sizeof(SceneConstants) / 4, &cb, 0);

            for (int subIdx : node.subMeshIndices) {
                const auto& sub = mesh->subMeshes[subIdx];
                if (sub.isTransparent) continue;

                int matIdx = (sub.materialIndex >= 0 && sub.materialIndex < (int)mesh->texturePaths.size()) ? sub.materialIndex : 0;
                CD3DX12_GPU_DESCRIPTOR_HANDLE srvHandle(
                    inst.srvHeap->GetGPUDescriptorHandleForHeapStart(),
                    matIdx * 2, m_srvDescriptorSize);
                cmdList->SetGraphicsRootDescriptorTable(1, srvHandle);
                cmdList->DrawIndexedInstanced(sub.indexCount, 1, sub.indexOffset, 0, 0);
                currentDrawCalls++;
            }
        }
        totalVerts += (int)mesh->vertices.size();
        totalPolys += (int)mesh->indices.size() / 3;
    }

    // ==========================================
    // Transition: RENDER_TARGET / DEPTH_WRITE -> PIXEL_SHADER_RESOURCE
    // ==========================================
    D3D12_RESOURCE_BARRIER barriersToSRV[] = {
        CD3DX12_RESOURCE_BARRIER::Transition(
            m_gBuffer.GetAlbedo(),
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
        CD3DX12_RESOURCE_BARRIER::Transition(
            m_gBuffer.GetNormal(),
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
        CD3DX12_RESOURCE_BARRIER::Transition(
            m_ctx.GetDepthBuffer(),
            D3D12_RESOURCE_STATE_DEPTH_WRITE,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
    };
    cmdList->ResourceBarrier(_countof(barriersToSRV), barriersToSRV);

    // ==========================================
    // Pass 2: Lighting Pass
    // ==========================================
    D3D12_CPU_DESCRIPTOR_HANDLE backBufferRTV = m_ctx.GetCurrentBackBufferRTV();
    cmdList->OMSetRenderTargets(1, &backBufferRTV, FALSE, nullptr);
    cmdList->SetGraphicsRootSignature(m_lightRootSig.Get());
    cmdList->SetPipelineState(m_lightPSO.Get());

    ID3D12DescriptorHeap* gbufferHeaps[] = { m_gBuffer.GetSrvHeap() };
    cmdList->SetDescriptorHeaps(1, gbufferHeaps);

    // slot 0: b0 LightPassConstants (8 DWORD)
    LightPassConstants lightPassCb = {};
    lightPassCb.cameraPos   = m_scene.GetCameraPos();
    XMStoreFloat3(&lightPassCb.mainLightDir, XMVector3Normalize(forward));
    cmdList->SetGraphicsRoot32BitConstants(0, sizeof(LightPassConstants) / 4, &lightPassCb, 0);

    // slot 1: t0-t2 GBuffer SRVs (descriptor table)
    cmdList->SetGraphicsRootDescriptorTable(1, m_gBuffer.GetSrvStart());

    // slot 2: b1 LightBuffer CBV
    cmdList->SetGraphicsRootConstantBufferView(2, m_lightBuffer->GetGPUVirtualAddress());

    // slot 3: b2 ReconstructConstants (invViewProj, 16 DWORD)
    XMMATRIX invViewProj = XMMatrixInverse(nullptr, view * proj);
    DirectX::XMFLOAT4X4 invVP;
    XMStoreFloat4x4(&invVP, XMMatrixTranspose(invViewProj));
    cmdList->SetGraphicsRoot32BitConstants(3, 16, &invVP, 0);

    cmdList->DrawInstanced(3, 1, 0, 0);
    currentDrawCalls++;

    // ==========================================
    // Transition back: PIXEL_SHADER_RESOURCE -> original
    // ==========================================
    D3D12_RESOURCE_BARRIER barriersToRTV[] = {
        CD3DX12_RESOURCE_BARRIER::Transition(
            m_gBuffer.GetAlbedo(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_RENDER_TARGET),
        CD3DX12_RESOURCE_BARRIER::Transition(
            m_gBuffer.GetNormal(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_RENDER_TARGET),
        CD3DX12_RESOURCE_BARRIER::Transition(
            m_ctx.GetDepthBuffer(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_DEPTH_WRITE),
    };
    cmdList->ResourceBarrier(_countof(barriersToRTV), barriersToRTV);

    // ==========================================
    // Pass 3: Forward Transparent Pass
    // ==========================================
    cmdList->OMSetRenderTargets(1, &backBufferRTV, FALSE, &dsv);
    cmdList->SetGraphicsRootSignature(m_forwardRootSig.Get());
    cmdList->SetPipelineState(m_transparentPSO.Get());

    for (auto& inst : m_scene.GetMeshes()) {
        auto& mesh = inst.mesh;
        if (!mesh) continue;

        std::vector<XMMATRIX> globalTransforms(mesh->nodes.size());
        for (size_t i = 0; i < mesh->nodes.size(); ++i) {
            const auto& node = mesh->nodes[i];
            XMMATRIX local =
                XMMatrixScaling(node.s[0], node.s[1], node.s[2]) *
                XMMatrixRotationQuaternion(XMVectorSet(node.r[0], node.r[1], node.r[2], node.r[3])) *
                XMMatrixTranslation(node.t[0], node.t[1], node.t[2]);
            globalTransforms[i] = (node.parentIndex >= 0)
                ? local * globalTransforms[node.parentIndex]
                : local;
        }

        cmdList->IASetVertexBuffers(0, 1, &mesh->vbView);
        cmdList->IASetIndexBuffer(&mesh->ibView);
        ID3D12DescriptorHeap* heaps[] = { inst.srvHeap.Get() };
        cmdList->SetDescriptorHeaps(1, heaps);

        for (size_t n = 0; n < mesh->nodes.size(); ++n) {
            const auto& node = mesh->nodes[n];
            if (node.subMeshIndices.empty()) continue;

            XMMATRIX modelMat = globalTransforms[n];
            SceneConstants cb = {};
            XMStoreFloat4x4(&cb.mvp,          XMMatrixTranspose(modelMat * view * proj));
            XMStoreFloat4x4(&cb.modelMatrix,  XMMatrixTranspose(modelMat));
            XMStoreFloat4x4(&cb.normalMatrix, XMMatrixInverse(nullptr, modelMat));
            XMStoreFloat3(&cb.lightDir,       XMVector3Normalize(forward));
            cb.cameraPos = m_scene.GetCameraPos();
            cmdList->SetGraphicsRoot32BitConstants(0, sizeof(SceneConstants) / 4, &cb, 0);

            for (int subIdx : node.subMeshIndices) {
                const auto& sub = mesh->subMeshes[subIdx];
                if (!sub.isTransparent) continue;

                int matIdx = (sub.materialIndex >= 0 && sub.materialIndex < (int)mesh->texturePaths.size()) ? sub.materialIndex : 0;
                CD3DX12_GPU_DESCRIPTOR_HANDLE srvHandle(
                    inst.srvHeap->GetGPUDescriptorHandleForHeapStart(),
                    matIdx * 2, m_srvDescriptorSize);
                cmdList->SetGraphicsRootDescriptorTable(1, srvHandle);
                cmdList->DrawIndexedInstanced(sub.indexCount, 1, sub.indexOffset, 0, 0);
                currentDrawCalls++;
            }
        }
    }

    m_statVertices.store(totalVerts,        std::memory_order_relaxed);
    m_statPolygons.store(totalPolys,        std::memory_order_relaxed);
    m_statDrawCalls.store(currentDrawCalls, std::memory_order_relaxed);

    m_ctx.ExecuteCommandListAndPresent();
    m_renderMutex.unlock();
}

// ---------------------------------------------------------------------------
// UploadMeshToGpu
// ---------------------------------------------------------------------------
void Renderer::UploadMeshToGpu(std::shared_ptr<Mesh> mesh, int meshId) {
    struct TextureCpuData {
        int width = 1, height = 1;
        UINT mipLevels = 1;
        std::vector<std::vector<uint8_t>> mipData;
    };

    auto PrepareTextureData = [](const std::string& path, uint32_t defaultColor) -> TextureCpuData {
        TextureCpuData data;
        int texChannels = 4;
        stbi_uc* pixels = path.empty() ? nullptr : stbi_load(path.c_str(), &data.width, &data.height, &texChannels, 4);
        if (!pixels) {
            data.width = 1; data.height = 1;
            pixels = (stbi_uc*)&defaultColor;
        }
        data.mipLevels = 1;
        UINT tempW = data.width, tempH = data.height;
        while (tempW > 1 || tempH > 1) { data.mipLevels++; tempW = (std::max)(1u, tempW / 2); tempH = (std::max)(1u, tempH / 2); }
        data.mipData.resize(data.mipLevels);
        data.mipData[0].assign(pixels, pixels + (data.width * data.height * 4));
        UINT currW = data.width, currH = data.height;
        for (UINT m = 1; m < data.mipLevels; ++m) {
            UINT prevW = currW, prevH = currH;
            currW = (std::max)(1u, currW / 2); currH = (std::max)(1u, currH / 2);
            data.mipData[m].resize(currW * currH * 4);
            const uint8_t* src = data.mipData[m - 1].data();
            uint8_t* dst = data.mipData[m].data();
            for (UINT y = 0; y < currH; ++y)
                for (UINT x = 0; x < currW; ++x) {
                    UINT sx = x * 2, sy = y * 2,
                         sx1 = (std::min)(sx + 1, prevW - 1),
                         sy1 = (std::min)(sy + 1, prevH - 1);
                    for (int c = 0; c < 4; ++c)
                        dst[(y * currW + x) * 4 + c] =
                            (src[(sy  * prevW + sx ) * 4 + c] +
                             src[(sy  * prevW + sx1) * 4 + c] +
                             src[(sy1 * prevW + sx ) * 4 + c] +
                             src[(sy1 * prevW + sx1) * 4 + c]) / 4;
                }
        }
        if (pixels != (stbi_uc*)&defaultColor) stbi_image_free(pixels);
        return data;
    };

    UINT numMaterials = (std::max)(1, (int)mesh->texturePaths.size());
    std::vector<TextureCpuData> baseColors(numMaterials), metallicRoughness(numMaterials);
    for (size_t i = 0; i < numMaterials; i++) {
        baseColors[i]        = PrepareTextureData(mesh->texturePaths[i], 0xFFFFFFFF);
        metallicRoughness[i] = PrepareTextureData(
            i < mesh->metallicRoughnessPaths.size() ? mesh->metallicRoughnessPaths[i] : "",
            0xFF00FF00);
    }

    std::lock_guard<std::mutex> lock(m_renderMutex);
    m_ctx.WaitForGpu();

    MeshInstance inst;
    inst.meshId = meshId;
    inst.mesh   = mesh;

    auto device   = m_ctx.GetDevice();
    auto cmdList  = m_ctx.GetCommandList();
    auto cmdQueue = m_ctx.GetCommandQueue();

    auto uploadHeap  = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    auto defaultHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

    UINT64 vbSize = mesh->vertices.size() * sizeof(Vertex);
    UINT64 ibSize = mesh->indices.size()  * sizeof(uint32_t);

    auto vbDesc = CD3DX12_RESOURCE_DESC::Buffer(vbSize);
    device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &vbDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&mesh->vertexBuffer));
    device->CreateCommittedResource(&uploadHeap,  D3D12_HEAP_FLAG_NONE, &vbDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&inst.vbUpload));

    void* mapped;
    inst.vbUpload->Map(0, nullptr, &mapped);
    memcpy(mapped, mesh->vertices.data(), vbSize);
    inst.vbUpload->Unmap(0, nullptr);

    auto ibDesc = CD3DX12_RESOURCE_DESC::Buffer(ibSize);
    device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &ibDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&mesh->indexBuffer));
    device->CreateCommittedResource(&uploadHeap,  D3D12_HEAP_FLAG_NONE, &ibDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&inst.ibUpload));

    inst.ibUpload->Map(0, nullptr, &mapped);
    memcpy(mapped, mesh->indices.data(), ibSize);
    inst.ibUpload->Unmap(0, nullptr);

    m_ctx.ResetCommandList();
    cmdList->CopyResource(mesh->vertexBuffer.Get(), inst.vbUpload.Get());
    cmdList->CopyResource(mesh->indexBuffer.Get(),  inst.ibUpload.Get());
    D3D12_RESOURCE_BARRIER barriers[2] = {
        CD3DX12_RESOURCE_BARRIER::Transition(mesh->vertexBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER),
        CD3DX12_RESOURCE_BARRIER::Transition(mesh->indexBuffer.Get(),  D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDEX_BUFFER),
    };
    cmdList->ResourceBarrier(2, barriers);
    cmdList->Close();
    ID3D12CommandList* lists[] = { cmdList };
    cmdQueue->ExecuteCommandLists(1, lists);
    m_ctx.WaitForGpu();

    mesh->vbView = { mesh->vertexBuffer->GetGPUVirtualAddress(), (UINT)vbSize, sizeof(Vertex) };
    mesh->ibView = { mesh->indexBuffer->GetGPUVirtualAddress(),  (UINT)ibSize, DXGI_FORMAT_R32_UINT };

    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.NumDescriptors = numMaterials * 2;
    srvHeapDesc.Type  = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    CHECK(device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&inst.srvHeap)));

    m_ctx.ResetCommandList();
    CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(inst.srvHeap->GetCPUDescriptorHandleForHeapStart());
    std::vector<ComPtr<ID3D12Resource>> uploadBuffers;

    auto UploadToGPU = [&](const TextureCpuData& cpuData) {
        inst.textures.emplace_back();
        uploadBuffers.emplace_back();
        auto texDesc = CD3DX12_RESOURCE_DESC::Tex2D(
            DXGI_FORMAT_R8G8B8A8_UNORM,
            cpuData.width, cpuData.height, 1, (UINT16)cpuData.mipLevels);

        auto dh = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        device->CreateCommittedResource(&dh, D3D12_HEAP_FLAG_NONE, &texDesc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&inst.textures.back()));

        UINT64 uploadSize = 0;
        device->GetCopyableFootprints(&texDesc, 0, cpuData.mipLevels, 0,
            nullptr, nullptr, nullptr, &uploadSize);

        auto uh      = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        auto bufDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadSize);
        device->CreateCommittedResource(&uh, D3D12_HEAP_FLAG_NONE, &bufDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadBuffers.back()));

        std::vector<D3D12_SUBRESOURCE_DATA> subresources(cpuData.mipLevels);
        for (UINT m = 0; m < cpuData.mipLevels; ++m) {
            UINT cw = (std::max)(1u, (UINT)(cpuData.width  >> m));
            UINT ch = (std::max)(1u, (UINT)(cpuData.height >> m));
            subresources[m] = { cpuData.mipData[m].data(),
                (LONG_PTR)(cw * 4), (LONG_PTR)(cw * 4 * ch) };
        }

        UpdateSubresources(cmdList, inst.textures.back().Get(),
            uploadBuffers.back().Get(), 0, 0, cpuData.mipLevels, subresources.data());
        auto bar = CD3DX12_RESOURCE_BARRIER::Transition(
            inst.textures.back().Get(),
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        cmdList->ResourceBarrier(1, &bar);

        D3D12_SHADER_RESOURCE_VIEW_DESC sv = {};
        sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        sv.Format = texDesc.Format;
        sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        sv.Texture2D.MipLevels = cpuData.mipLevels;
        device->CreateShaderResourceView(inst.textures.back().Get(), &sv, srvHandle);
        srvHandle.Offset(1, m_srvDescriptorSize);
    };

    for (size_t i = 0; i < numMaterials; i++) {
        UploadToGPU(baseColors[i]);
        UploadToGPU(metallicRoughness[i]);
    }

    cmdList->Close();
    ID3D12CommandList* cmds[] = { cmdList };
    cmdQueue->ExecuteCommandLists(1, cmds);
    m_ctx.WaitForGpu();

    m_scene.AddMeshInstance(std::move(inst));
}

// ---------------------------------------------------------------------------
// CreateRootSignaturesAndPSOs
// ---------------------------------------------------------------------------
void Renderer::CreateRootSignaturesAndPSOs() {
    auto device = m_ctx.GetDevice();

    // ==========================================
    // 1. Geometry Pass Root Signature & PSO
    // ==========================================
    CD3DX12_DESCRIPTOR_RANGE1 geomSrvRange;
    geomSrvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0);
    CD3DX12_ROOT_PARAMETER1 geomParams[2];
    geomParams[0].InitAsConstants(sizeof(SceneConstants) / 4, 0);
    geomParams[1].InitAsDescriptorTable(1, &geomSrvRange, D3D12_SHADER_VISIBILITY_PIXEL);

    CD3DX12_STATIC_SAMPLER_DESC sampler(0, D3D12_FILTER_ANISOTROPIC);
    sampler.MaxAnisotropy = 16;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC geomRsDesc;
    geomRsDesc.Init_1_1(2, geomParams, 1, &sampler,
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
    ComPtr<ID3DBlob> sigBlob, errBlob;
    D3DX12SerializeVersionedRootSignature(&geomRsDesc, D3D_ROOT_SIGNATURE_VERSION_1_1, &sigBlob, &errBlob);
    device->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(), IID_PPV_ARGS(&m_geomRootSig));

    ComPtr<ID3DBlob> geomVS, geomPS;
    D3DReadFileToBlob(GetShaderPath(L"GBuffer_VS.cso").c_str(), &geomVS);
    D3DReadFileToBlob(GetShaderPath(L"GBuffer_PS.cso").c_str(), &geomPS);

    D3D12_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC geomPsoDesc = {};
    geomPsoDesc.InputLayout         = { layout, _countof(layout) };
    geomPsoDesc.pRootSignature      = m_geomRootSig.Get();
    geomPsoDesc.VS                  = CD3DX12_SHADER_BYTECODE(geomVS.Get());
    geomPsoDesc.PS                  = CD3DX12_SHADER_BYTECODE(geomPS.Get());
    geomPsoDesc.RasterizerState     = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    geomPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    geomPsoDesc.BlendState          = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    geomPsoDesc.DepthStencilState   = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    geomPsoDesc.SampleMask          = UINT_MAX;
    geomPsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    geomPsoDesc.NumRenderTargets    = GBuffer::TargetCount;
    geomPsoDesc.RTVFormats[0]       = DXGI_FORMAT_R8G8B8A8_UNORM;
    geomPsoDesc.RTVFormats[1]       = DXGI_FORMAT_R16G16B16A16_FLOAT;
    geomPsoDesc.DSVFormat           = DXGI_FORMAT_D32_FLOAT;
    geomPsoDesc.SampleDesc.Count    = 1;
    device->CreateGraphicsPipelineState(&geomPsoDesc, IID_PPV_ARGS(&m_geomPSO));

    // ==========================================
    // 2. Lighting Pass Root Signature & PSO
    //
    //  slot 0: b0  LightPassConstants  (InitAsConstants, 8 DWORD)
    //  slot 1: t0-t2  GBuffer SRVs     (DescriptorTable, 1 DWORD)
    //  slot 2: b1  LightBuffer         (InitAsConstantBufferView, 2 DWORD)
    //  slot 3: b2  ReconstructConstants(InitAsConstants, 16 DWORD)
    //  Total: 8+1+2+16 = 27 DWORD  (limit: 64)
    // ==========================================
    CD3DX12_DESCRIPTOR_RANGE1 lightSrvRange;
    lightSrvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 0); // t0 albedo, t1 normal, t2 depth

    CD3DX12_ROOT_PARAMETER1 lightParams[4];
    lightParams[0].InitAsConstants(sizeof(LightPassConstants) / 4, 0, 0,  // b0: 8 DWORD
        D3D12_SHADER_VISIBILITY_PIXEL);
    lightParams[1].InitAsDescriptorTable(1, &lightSrvRange,               // t0-t2
        D3D12_SHADER_VISIBILITY_PIXEL);
    lightParams[2].InitAsConstantBufferView(1, 0,                         // b1: LightBuffer CBV
        D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE,
        D3D12_SHADER_VISIBILITY_PIXEL);
    lightParams[3].InitAsConstants(16, 2, 0,                              // b2: invViewProj 16 DWORD
        D3D12_SHADER_VISIBILITY_PIXEL);

    CD3DX12_STATIC_SAMPLER_DESC lightSampler(0, D3D12_FILTER_MIN_MAG_MIP_POINT);
    lightSampler.AddressU = lightSampler.AddressV = lightSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC lightRsDesc;
    lightRsDesc.Init_1_1(4, lightParams, 1, &lightSampler,
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
    CHECK(D3DX12SerializeVersionedRootSignature(&lightRsDesc, D3D_ROOT_SIGNATURE_VERSION_1_1, &sigBlob, &errBlob));
    CHECK(device->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(), IID_PPV_ARGS(&m_lightRootSig)));

    ComPtr<ID3DBlob> lightVS, lightPS;
    CHECK(D3DReadFileToBlob(GetShaderPath(L"DeferredLight_VS.cso").c_str(), &lightVS));
    CHECK(D3DReadFileToBlob(GetShaderPath(L"DeferredLight_PS.cso").c_str(), &lightPS));

    D3D12_GRAPHICS_PIPELINE_STATE_DESC lightPsoDesc = {};
    lightPsoDesc.pRootSignature       = m_lightRootSig.Get();
    lightPsoDesc.VS                   = CD3DX12_SHADER_BYTECODE(lightVS.Get());
    lightPsoDesc.PS                   = CD3DX12_SHADER_BYTECODE(lightPS.Get());
    lightPsoDesc.RasterizerState      = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    lightPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    lightPsoDesc.BlendState           = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    lightPsoDesc.DepthStencilState.DepthEnable    = FALSE;
    lightPsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    lightPsoDesc.SampleMask           = UINT_MAX;
    lightPsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    lightPsoDesc.NumRenderTargets     = 1;
    lightPsoDesc.RTVFormats[0]        = DXGI_FORMAT_R8G8B8A8_UNORM;
    lightPsoDesc.SampleDesc.Count     = 1;
    CHECK(device->CreateGraphicsPipelineState(&lightPsoDesc, IID_PPV_ARGS(&m_lightPSO)));

    // ==========================================
    // 3. Forward Transparent Pass
    // ==========================================
    CD3DX12_DESCRIPTOR_RANGE1 fwdSrvRange;
    fwdSrvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0);
    CD3DX12_ROOT_PARAMETER1 fwdParams[2];
    fwdParams[0].InitAsConstants(sizeof(SceneConstants) / 4, 0);
    fwdParams[1].InitAsDescriptorTable(1, &fwdSrvRange, D3D12_SHADER_VISIBILITY_PIXEL);

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC fwdRsDesc;
    fwdRsDesc.Init_1_1(2, fwdParams, 1, &sampler,
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
    D3DX12SerializeVersionedRootSignature(&fwdRsDesc, D3D_ROOT_SIGNATURE_VERSION_1_1, &sigBlob, &errBlob);
    device->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(), IID_PPV_ARGS(&m_forwardRootSig));

    ComPtr<ID3DBlob> fwdVS, fwdPS;
    D3DReadFileToBlob(GetShaderPath(L"ForwardTransparent_VS.cso").c_str(), &fwdVS);
    D3DReadFileToBlob(GetShaderPath(L"ForwardTransparent_PS.cso").c_str(), &fwdPS);

    D3D12_GRAPHICS_PIPELINE_STATE_DESC transPsoDesc = geomPsoDesc;
    transPsoDesc.pRootSignature    = m_forwardRootSig.Get();
    transPsoDesc.VS                = CD3DX12_SHADER_BYTECODE(fwdVS.Get());
    transPsoDesc.PS                = CD3DX12_SHADER_BYTECODE(fwdPS.Get());
    transPsoDesc.NumRenderTargets  = 1;
    transPsoDesc.RTVFormats[0]     = DXGI_FORMAT_R8G8B8A8_UNORM;
    transPsoDesc.RTVFormats[1]     = DXGI_FORMAT_UNKNOWN;
    transPsoDesc.RTVFormats[2]     = DXGI_FORMAT_UNKNOWN;

    D3D12_RENDER_TARGET_BLEND_DESC blendDesc = {};
    blendDesc.BlendEnable           = TRUE;
    blendDesc.SrcBlend              = D3D12_BLEND_SRC_ALPHA;
    blendDesc.DestBlend             = D3D12_BLEND_INV_SRC_ALPHA;
    blendDesc.BlendOp               = D3D12_BLEND_OP_ADD;
    blendDesc.SrcBlendAlpha         = D3D12_BLEND_ONE;
    blendDesc.DestBlendAlpha        = D3D12_BLEND_ZERO;
    blendDesc.BlendOpAlpha          = D3D12_BLEND_OP_ADD;
    blendDesc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    transPsoDesc.BlendState.RenderTarget[0] = blendDesc;

    transPsoDesc.DepthStencilState.DepthEnable    = TRUE;
    transPsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    transPsoDesc.DepthStencilState.DepthFunc      = D3D12_COMPARISON_FUNC_LESS_EQUAL;

    device->CreateGraphicsPipelineState(&transPsoDesc, IID_PPV_ARGS(&m_transparentPSO));
}
