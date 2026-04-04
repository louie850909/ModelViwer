#include "pch.h"
#include "GBuffer.h"

#define CHECK(hr) if(FAILED(hr)) throw std::runtime_error("DX12 GBuffer failed")

void GBuffer::Init(ID3D12Device* device, int width, int height,
                   ID3D12Resource* sharedDepthBuffer) {
    m_rtvDescSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    m_srvDescSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    CreateResources(device, width, height);
    CreateHeapsAndViews(device, sharedDepthBuffer);
}

void GBuffer::Resize(ID3D12Device* device, int width, int height,
                     ID3D12Resource* sharedDepthBuffer) {
    m_albedo.Reset();
    m_normal.Reset();
    // Heaps are reused; views are recreated.
    CreateResources(device, width, height);
    CreateHeapsAndViews(device, sharedDepthBuffer);
}

void GBuffer::Shutdown() {
    m_albedo.Reset();
    m_normal.Reset();
    m_rtvHeap.Reset();
    m_srvHeap.Reset();
}

void GBuffer::CreateResources(ID3D12Device* device, int width, int height) {
    auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

    // RT0: Albedo (RGB) + Roughness (A)
    D3D12_CLEAR_VALUE clearZero = {};
    clearZero.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    auto descAlbedo = CD3DX12_RESOURCE_DESC::Tex2D(
        DXGI_FORMAT_R8G8B8A8_UNORM, width, height, 1, 0, 1, 0,
        D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
    CHECK(device->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &descAlbedo,
        D3D12_RESOURCE_STATE_RENDER_TARGET, &clearZero,
        IID_PPV_ARGS(&m_albedo)));

    // RT1: Normal (RGB, needs negative values) + Metallic (A)
    D3D12_CLEAR_VALUE clearNormal = {};
    clearNormal.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    auto descNormal = CD3DX12_RESOURCE_DESC::Tex2D(
        DXGI_FORMAT_R16G16B16A16_FLOAT, width, height, 1, 0, 1, 0,
        D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
    CHECK(device->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &descNormal,
        D3D12_RESOURCE_STATE_RENDER_TARGET, &clearNormal,
        IID_PPV_ARGS(&m_normal)));

    // WorldPos RT removed; world position is reconstructed in the lighting
    // shader from the depth buffer + inv-view-proj matrix.
}

void GBuffer::CreateHeapsAndViews(ID3D12Device* device,
                                   ID3D12Resource* sharedDepthBuffer) {
    // RTV heap: 2 slots (albedo, normal)
    if (!m_rtvHeap) {
        D3D12_DESCRIPTOR_HEAP_DESC rtvDesc = {};
        rtvDesc.NumDescriptors = TargetCount; // 2
        rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        CHECK(device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&m_rtvHeap)));
    }

    // SRV heap: 3 slots (albedo, normal, depth-as-SRV)
    if (!m_srvHeap) {
        D3D12_DESCRIPTOR_HEAP_DESC srvDesc = {};
        srvDesc.NumDescriptors = 3; // albedo + normal + depth SRV
        srvDesc.Type  = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        CHECK(device->CreateDescriptorHeap(&srvDesc, IID_PPV_ARGS(&m_srvHeap)));
    }

    // Bind RTVs
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());
    device->CreateRenderTargetView(m_albedo.Get(), nullptr, rtvHandle);
    rtvHandle.Offset(1, m_rtvDescSize);
    device->CreateRenderTargetView(m_normal.Get(), nullptr, rtvHandle);

    // Bind SRVs
    CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(m_srvHeap->GetCPUDescriptorHandleForHeapStart());
    device->CreateShaderResourceView(m_albedo.Get(), nullptr, srvHandle);
    srvHandle.Offset(1, m_srvDescSize);
    device->CreateShaderResourceView(m_normal.Get(), nullptr, srvHandle);
    srvHandle.Offset(1, m_srvDescSize);

    // Depth SRV: bind the shared D32_FLOAT depth buffer as R32_FLOAT SRV
    D3D12_SHADER_RESOURCE_VIEW_DESC depthSrvDesc = {};
    depthSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    depthSrvDesc.Format                  = DXGI_FORMAT_R32_FLOAT; // depth typeless read
    depthSrvDesc.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
    depthSrvDesc.Texture2D.MipLevels     = 1;
    device->CreateShaderResourceView(sharedDepthBuffer, &depthSrvDesc, srvHandle);
}
