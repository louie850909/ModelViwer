cbuffer SceneConstants : register(b0)
{
    matrix mvp;
    matrix modelMatrix;
    matrix normalMatrix;
    float3 cameraPos;
    float  _pad1;
    float3 lightDir;
    float  _pad2;
    float4 baseColor;
};

struct VSInput
{
    float3 pos    : POSITION;
    float3 normal : NORMAL;
    float2 uv     : TEXCOORD;
};

struct VSOutput
{
    float4 pos      : SV_Position;
    float3 worldPos : POSITION;   // kept for possible future VS-only use; NOT written to RT
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD;
};

VSOutput VSMain(VSInput input)
{
    VSOutput output;
    output.pos      = mul(float4(input.pos, 1.0f), mvp);
    output.worldPos = mul(float4(input.pos, 1.0f), modelMatrix).xyz;
    output.normal   = normalize(mul(input.normal, (float3x3)normalMatrix));
    output.uv       = input.uv;
    return output;
}

Texture2D    g_baseColorMap : register(t0);
Texture2D    g_mrMap        : register(t1); // G=Roughness, B=Metallic
SamplerState g_sampler      : register(s0);

// PSOutput: 2 RTs only (WorldPos RT removed; world-pos reconstructed from depth in DeferredLight.hlsl)
struct PSOutput
{
    float4 AlbedoRoughness : SV_Target0; // RGB=Albedo, A=Roughness
    float4 NormalMetallic  : SV_Target1; // RGB=Normal, A=Metallic
};

PSOutput PSMain(VSOutput input)
{
    PSOutput output;

    float4 albedo = g_baseColorMap.Sample(g_sampler, input.uv);
    clip(albedo.a - 0.5f); // simple alpha test

    float4 mr = g_mrMap.Sample(g_sampler, input.uv);

    output.AlbedoRoughness = float4(albedo.rgb, mr.g);
    output.NormalMetallic  = float4(normalize(input.normal), mr.b);

    return output;
}
