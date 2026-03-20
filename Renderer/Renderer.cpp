#include "pch.h"
#include "Renderer.h"
#include <stdexcept>

#define CHECK(hr) if(FAILED(hr)) throw std::runtime_error("DX12 HRESULT failed")

// 簡易 HSV→RGB，用來讓背景顏色動起來
static void HsvToRgb(float h, float& r, float& g, float& b) {
    h = fmodf(h, 360.f);
    float s = 0.7f, v = 0.3f;
    int   i = (int)(h / 60.f);
    float f = h / 60.f - i;
    float p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
    switch (i % 6) {
    case 0: r = v; g = t; b = p; break; case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = t; break; case 3: r = p; g = q; b = v; break;
    case 4: r = t; g = p; b = v; break; default:r = v; g = p; b = q; break;
    }
}

bool Renderer::Init(IUnknown* panelUnknown, int width, int height) {
    m_width = width; m_height = height;
    try {
        CreateDeviceAndQueue();
        CreateSwapChain(panelUnknown, width, height);
        CreateRTV();

        // CommandAllocator + CommandList
        for (UINT i = 0; i < FRAME_COUNT; i++) {
            CHECK(m_device->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_cmdAllocators[i])));
        }
        CHECK(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            m_cmdAllocators[0].Get(), nullptr, IID_PPV_ARGS(&m_cmdList)));
        CHECK(m_cmdList->Close());

        // Fence
        CHECK(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
        m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
        return true;
    }
    catch (...) { return false; }
}

void Renderer::CreateDeviceAndQueue() {
#ifdef _DEBUG
    ComPtr<ID3D12Debug> debug;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) debug->EnableDebugLayer();
#endif
    ComPtr<IDXGIFactory7> factory;
    CHECK(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)));
    CHECK(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&m_device)));

    D3D12_COMMAND_QUEUE_DESC qDesc = {};
    qDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    qDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    CHECK(m_device->CreateCommandQueue(&qDesc, IID_PPV_ARGS(&m_cmdQueue)));
}

void Renderer::CreateSwapChain(IUnknown* panelUnknown, int width, int height) {
    ComPtr<IDXGIFactory7> factory;
    CHECK(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)));

    DXGI_SWAP_CHAIN_DESC1 desc = {};
    desc.Width = (UINT)width;
    desc.Height = (UINT)height;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferCount = FRAME_COUNT;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.SampleDesc = { 1, 0 };
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED; // Composition 必要

    ComPtr<IDXGISwapChain1> sc1;
    // CreateSwapChainForComposition，不是 ForHwnd！
    CHECK(factory->CreateSwapChainForComposition(m_cmdQueue.Get(), &desc, nullptr, &sc1));
    CHECK(sc1.As(&m_swapChain));

    // 綁定到 SwapChainPanel
    ComPtr<ISwapChainPanelNative> panelNative;
    CHECK(panelUnknown->QueryInterface(IID_PPV_ARGS(&panelNative)));
    CHECK(panelNative->SetSwapChain(m_swapChain.Get()));
}

void Renderer::CreateRTV() {
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.NumDescriptors = FRAME_COUNT;
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    CHECK(m_device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_rtvHeap)));
    m_rtvDescSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());
    for (UINT i = 0; i < FRAME_COUNT; i++) {
        CHECK(m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_renderTargets[i])));
        m_device->CreateRenderTargetView(m_renderTargets[i].Get(), nullptr, rtvHandle);
        rtvHandle.Offset(1, m_rtvDescSize);
    }
}

void Renderer::RenderFrame() {
    m_hue += 0.3f; // 每幀推進色相

    auto& alloc = m_cmdAllocators[m_frameIndex];
    alloc->Reset();
    m_cmdList->Reset(alloc.Get(), nullptr);

    // Barrier: Present → RenderTarget
    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        m_renderTargets[m_frameIndex].Get(),
        D3D12_RESOURCE_STATE_PRESENT,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    m_cmdList->ResourceBarrier(1, &barrier);

    // Clear
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtv(
        m_rtvHeap->GetCPUDescriptorHandleForHeapStart(),
        (INT)m_frameIndex, m_rtvDescSize);
    float r, g, b;
    HsvToRgb(m_hue, r, g, b);
    float clearColor[] = { r, g, b, 1.0f };
    m_cmdList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);

    // Barrier: RenderTarget → Present
    barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        m_renderTargets[m_frameIndex].Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PRESENT);
    m_cmdList->ResourceBarrier(1, &barrier);
    m_cmdList->Close();

    ID3D12CommandList* lists[] = { m_cmdList.Get() };
    m_cmdQueue->ExecuteCommandLists(1, lists);
    m_swapChain->Present(1, 0);

    // 推進 Fence
    const UINT64 val = ++m_fenceValues[m_frameIndex];
    m_cmdQueue->Signal(m_fence.Get(), val);
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
    if (m_fence->GetCompletedValue() < m_fenceValues[m_frameIndex]) {
        m_fence->SetEventOnCompletion(m_fenceValues[m_frameIndex], m_fenceEvent);
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }
    m_fenceValues[m_frameIndex] = val;
}

void Renderer::Resize(int width, int height) {
    if (width == m_width && height == m_height) return;
    WaitForGpu();
    for (auto& rt : m_renderTargets) rt.Reset();
    m_swapChain->ResizeBuffers(FRAME_COUNT, (UINT)width, (UINT)height,
        DXGI_FORMAT_R8G8B8A8_UNORM, 0);
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
    CreateRTV();
    m_width = width; m_height = height;
}

void Renderer::WaitForGpu() {
    const UINT64 val = ++m_fenceValues[m_frameIndex];
    m_cmdQueue->Signal(m_fence.Get(), val);
    m_fence->SetEventOnCompletion(val, m_fenceEvent);
    WaitForSingleObject(m_fenceEvent, INFINITE);
}

void Renderer::Shutdown() {
    WaitForGpu();
    CloseHandle(m_fenceEvent);
}