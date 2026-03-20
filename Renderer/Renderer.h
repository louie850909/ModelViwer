#pragma once
#include "pch.h"

class Renderer {
public:
    bool Init(IUnknown* panelUnknown, int width, int height);
    void Resize(int width, int height);
    void RenderFrame();
    void Shutdown();

private:
    void CreateDeviceAndQueue();
    void CreateSwapChain(IUnknown* panelUnknown, int width, int height);
    void CreateRTV();
    void WaitForGpu();

    static constexpr UINT FRAME_COUNT = 3;

    ComPtr<ID3D12Device>              m_device;
    ComPtr<ID3D12CommandQueue>        m_cmdQueue;
    ComPtr<IDXGISwapChain4>           m_swapChain;
    ComPtr<ID3D12DescriptorHeap>      m_rtvHeap;
    ComPtr<ID3D12Resource>            m_renderTargets[FRAME_COUNT];
    ComPtr<ID3D12CommandAllocator>    m_cmdAllocators[FRAME_COUNT];
    ComPtr<ID3D12GraphicsCommandList> m_cmdList;
    ComPtr<ID3D12Fence>               m_fence;

    UINT   m_rtvDescSize = 0;
    UINT   m_frameIndex = 0;
    HANDLE m_fenceEvent = nullptr;
    UINT64 m_fenceValues[FRAME_COUNT] = {};

    int m_width = 0;
    int m_height = 0;

    // 每幀循環色相，純色 Clear 展示用
    float m_hue = 0.0f;
};
