cbuffer Constants : register(b0)
{
    uint width;
    uint height;
    uint stepSize;
    uint passIndex; // 當前是第幾個 Pass
    uint isLastPass; // 1 表示最後一個 Pass，0 則否
};

RWTexture2D<float4> OutputGI : register(u0);
Texture2D<float4> InputGI : register(t0);
Texture2D<float4> NormalMap : register(t1);
Texture2D<float4> WorldPosMap : register(t2);
Texture2D<float4> AlbedoMap : register(t3); // 新增 Albedo 貼圖

[numthreads(8, 8, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= width || DTid.y >= height)
        return;

    float3 centerNormal = NormalMap[DTid.xy].xyz;
    float3 centerPos = WorldPosMap[DTid.xy].xyz;
    
    // 取得中心點的 Albedo，使用 max 避免除以 0 導致 NaN
    float3 centerAlbedo = max(AlbedoMap[DTid.xy].rgb, 0.001f);
    
    // 如果打到天空，略過降噪與解調直接輸出
    if (length(centerNormal) < 0.1f)
    {
        OutputGI[DTid.xy] = InputGI[DTid.xy];
        return;
    }

    float4 sumColor = 0.0f;
    float sumWeight = 0.0f;

    // 半徑為 2，在 À-Trous 中代表 5x5 的卷積矩陣
    const int radius = 2;
    
    for (int y = -radius; y <= radius; ++y)
    {
        for (int x = -radius; x <= radius; ++x)
        {
            // 將基礎座標偏移乘上 stepSize 實現範圍膨脹
            int2 offset = int2(x, y) * stepSize;
            int2 samplePos = clamp(int2(DTid.xy) + offset, int2(0, 0), int2(width - 1, height - 1));

            float4 sampleColor = InputGI[samplePos];
            
            // 解調 Demodulation】
            // 如果是第一個 Pass，從輸入的帶紋理光照中「剔除」紋理，轉換為純淨光照
            if (passIndex == 0)
            {
                float3 sampleAlbedo = max(AlbedoMap[samplePos].rgb, 0.001f);
                sampleColor.rgb /= sampleAlbedo;
            }

            float3 sampleNormal = NormalMap[samplePos].xyz;
            float3 samplePosWorld = WorldPosMap[samplePos].xyz;

            // 權重計算保持不變
            float spatialWeight = exp(-(x * x + y * y) / 4.5f);

            // 法線權重 (法線差異過大代表轉角或不同面，停止模糊)
            float normalWeight = pow(max(0.0f, dot(centerNormal, sampleNormal)), 64.0f);

            // 深度/平面權重 (最強大的改進：將距離投影到法線上)
            // 這讓我們在同一個平坦表面上即使採樣跨度很大，平面距離也幾乎為 0，允許盡情模糊；
            // 但只要稍微跨出邊緣或遇到凹凸，權重就會瞬間暴跌至 0。
            float planeDist = abs(dot(centerNormal, centerPos - samplePosWorld));
            float posWeight = exp(-planeDist * 10.0f);

            float w = spatialWeight * normalWeight * posWeight;
            
            sumColor += sampleColor * w;
            sumWeight += w;
        }
    }

    float4 finalColor = sumColor / max(sumWeight, 0.0001f);
    
    // 如果是最後一個 Pass，光照已經完全平滑，這時將清晰的紋理「乘回」畫面上
    if (isLastPass == 1)
    {
        finalColor.rgb *= centerAlbedo;
    }

    OutputGI[DTid.xy] = finalColor;
}