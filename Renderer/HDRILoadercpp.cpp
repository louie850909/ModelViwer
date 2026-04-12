#include "pch.h"
#include "HDRILoader.h"
#include "stb_image.h"
#include <cmath>

// 輔助函式：建立 Structured Buffer
static void CreateBuffer(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList,
    const void* data, UINT dataSize,
    ComPtr<ID3D12Resource>& defaultBuffer, ComPtr<ID3D12Resource>& uploadBuffer)
{
    auto defaultHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    auto uploadHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(dataSize);

    device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&defaultBuffer));

    device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadBuffer));

    void* pData;
    uploadBuffer->Map(0, nullptr, &pData);
    memcpy(pData, data, dataSize);
    uploadBuffer->Unmap(0, nullptr);

    cmdList->CopyResource(defaultBuffer.Get(), uploadBuffer.Get());

    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(defaultBuffer.Get(),
        D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    cmdList->ResourceBarrier(1, &barrier);
}

std::shared_ptr<HDRIResource> HDRILoader::LoadHDR(
    ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, const std::wstring& filename)
{
    auto resource = std::make_shared<HDRIResource>();
    int width, height, channels;

    FILE* file = nullptr;
    _wfopen_s(&file, filename.c_str(), L"rb");
    if (!file) return nullptr;

    float* data = stbi_loadf_from_file(file, &width, &height, &channels, 4);
    fclose(file);
    if (!data) return nullptr;

    resource->width = width;
    resource->height = height;

    // ==========================================
    // 1. 計算 HDRI CDF (重要性採樣地圖)
    // ==========================================
    std::vector<float> conditionalCDF(width * height, 0.0f);
    std::vector<float> marginalCDF(height, 0.0f);
    float totalWeight = 0.0f;

    for (int y = 0; y < height; ++y) {
        float rowWeight = 0.0f;
        // 等距柱狀投影的緯度角度 (用來補償兩極像素被拉伸的面積)
        float theta = (y + 0.5f) / height * 3.1415926535f;
        float sinTheta = std::sin(theta);

        for (int x = 0; x < width; ++x) {
            int idx = (y * width + x) * 4;
            // 提取亮度 (Luma)
            float luma = 0.2126f * data[idx] + 0.7152f * data[idx + 1] + 0.0722f * data[idx + 2];
            rowWeight += luma * sinTheta;
            conditionalCDF[y * width + x] = rowWeight;
        }

        // 條件機率歸一化 (0.0 ~ 1.0)
        if (rowWeight > 0.0f) {
            for (int x = 0; x < width; ++x) conditionalCDF[y * width + x] /= rowWeight;
        }
        totalWeight += rowWeight;
        marginalCDF[y] = totalWeight;
    }

    // 邊緣機率歸一化 (0.0 ~ 1.0)
    if (totalWeight > 0.0f) {
        for (int y = 0; y < height; ++y) marginalCDF[y] /= totalWeight;
    }

    // 保存總能量供 HLSL 計算 PDF 使用 (立體角積分常數)
    resource->envIntegral = totalWeight * (2.0f * 3.1415926535f * 3.1415926535f) / (width * height);

    // ==========================================
    // 2. 建立 DX12 資源 (Texture + Buffers)
    // ==========================================
    // 建立 Marginal Buffer
    CreateBuffer(device, cmdList, marginalCDF.data(), (UINT)(marginalCDF.size() * sizeof(float)),
        resource->marginalCDF, resource->marginalUpload);

    // 建立 Conditional Buffer
    CreateBuffer(device, cmdList, conditionalCDF.data(), (UINT)(conditionalCDF.size() * sizeof(float)),
        resource->conditionalCDF, resource->conditionalUpload);

    // 建立原本的 HDRI Texture2D (保留原本的邏輯)
    UINT rowPitch = width * 4 * sizeof(float);
    UINT imageSize = rowPitch * height;
    auto defaultHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    auto texDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R32G32B32A32_FLOAT, width, height, 1, 1);

    device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &texDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&resource->texture));

    auto uploadHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    auto uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(imageSize);
    device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&resource->texUpload));

    void* mappedData;
    resource->texUpload->Map(0, nullptr, &mappedData);
    memcpy(mappedData, data, imageSize);
    resource->texUpload->Unmap(0, nullptr);

    D3D12_TEXTURE_COPY_LOCATION dst = {};
    dst.pResource = resource->texture.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION src = {};
    src.pResource = resource->texUpload.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    src.PlacedFootprint.Footprint.Width = width;
    src.PlacedFootprint.Footprint.Height = height;
    src.PlacedFootprint.Footprint.Depth = 1;
    src.PlacedFootprint.Footprint.RowPitch = rowPitch;

    cmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(resource->texture.Get(),
        D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    cmdList->ResourceBarrier(1, &barrier);

    stbi_image_free(data);
    return resource;
}