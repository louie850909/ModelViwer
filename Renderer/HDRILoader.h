#pragma once
#include "pch.h"

// 封裝 HDRI 所有相關資源
struct HDRIResource {
    Microsoft::WRL::ComPtr<ID3D12Resource> texture;
    Microsoft::WRL::ComPtr<ID3D12Resource> marginalCDF;    // 1D (高度)
    Microsoft::WRL::ComPtr<ID3D12Resource> conditionalCDF; // 2D (寬度 x 高度)

    // 用於防止資源在 GPU 複製完成前被釋放
    Microsoft::WRL::ComPtr<ID3D12Resource> texUpload;
    Microsoft::WRL::ComPtr<ID3D12Resource> marginalUpload;
    Microsoft::WRL::ComPtr<ID3D12Resource> conditionalUpload;

    uint32_t width = 0;
    uint32_t height = 0;
    float envIntegral = 0.0f; // 整個環境光的總能量，用於 PDF 計算
};

class HDRILoader {
public:
    static std::shared_ptr<HDRIResource> LoadHDR(
        ID3D12Device* device,
        ID3D12GraphicsCommandList* cmdList,
        const std::wstring& filename);
};
