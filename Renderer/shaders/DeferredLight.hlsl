#include "PBRCommon.hlsli"
#include "LightDef.hlsli"

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
    // inv-view-proj is appended after SceneConstants on CPU side
    // (kept in a separate cbuffer to avoid breaking existing Geometry pass layout)
};

cbuffer ReconstructConstants : register(b2)
{
    matrix invViewProj; // CPU uploads: XMMatrixInverse(nullptr, view * proj)
};

struct VSOutput
{
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD;
};

// Full-screen triangle via SV_VertexID (no vertex buffer needed)
VSOutput VSMain(uint vertexID : SV_VertexID)
{
    VSOutput output;
    output.uv  = float2((vertexID << 1) & 2, vertexID & 2);
    output.pos = float4(output.uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return output;
}

Texture2D    g_AlbedoRough : register(t0); // RT0: albedo(rgb) + roughness(a)
Texture2D    g_NormalMetal : register(t1); // RT1: normal(rgb) + metallic(a)
Texture2D<float> g_Depth   : register(t2); // depth buffer (R32_FLOAT SRV)
SamplerState g_sampler     : register(s0);

// Reconstruct world-space position from NDC depth.
float3 ReconstructWorldPos(float2 uv, float depth)
{
    // NDC: x in [-1,1], y in [-1,1] (flip Y because UV origin is top-left)
    float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    float4 clip = float4(ndc, depth, 1.0);
    float4 world = mul(clip, invViewProj);
    return world.xyz / world.w;
}

float4 PSMain(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    float4 albedoRough = g_AlbedoRough.Sample(g_sampler, uv);
    float4 normalMetal = g_NormalMetal.Sample(g_sampler, uv);

    // Background detection: normal length == 0
    if (length(normalMetal.rgb) < 0.1f)
        return float4(0.2f, 0.2f, 0.2f, 1.0f);

    // Reconstruct world position from depth (saves one 128-bit RT worth of bandwidth)
    float  depth = g_Depth.Sample(g_sampler, uv);
    float3 P     = ReconstructWorldPos(uv, depth);

    float3 albedo    = albedoRough.rgb;
    float  roughness = albedoRough.a;
    float3 N         = normalize(normalMetal.rgb);
    float  metallic  = normalMetal.a;

    float3 V  = normalize(cameraPos - P);
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);

    float3 Lo = float3(0, 0, 0);
    for (int i = 0; i < numLights; i++)
    {
        Light l = lights[i];
        float3 LDir;
        float  attenuation = 1.0;

        if (l.type == 0)
        { // Directional
            LDir = normalize(-l.direction);
        }
        else if (l.type == 1)
        { // Point
            LDir = normalize(l.position - P);
            float dist = length(l.position - P);
            attenuation = 1.0 / (dist * dist + 0.0001);
        }
        else if (l.type == 2)
        { // Spot  (coneAngle stored in radians; converted on CPU side)
            LDir = normalize(l.position - P);
            float dist      = length(l.position - P);
            attenuation     = 1.0 / (dist * dist + 0.0001);
            float theta     = dot(LDir, normalize(-l.direction));
            float outerCos  = cos(l.coneAngle + 0.1745); // +~10 deg penumbra
            float innerCos  = cos(l.coneAngle);
            float epsilon   = innerCos - outerCos;
            float spot      = clamp((theta - outerCos) / epsilon, 0.0, 1.0);
            attenuation    *= spot;
        }

        float3 H     = normalize(V + LDir);
        float  NdotL = max(dot(N, LDir), 0.0);
        float  NDF   = DistributionGGX(N, H, roughness);
        float  G     = GeometrySmith(N, V, LDir, roughness);
        float3 F     = FresnelSchlick(max(dot(H, V), 0.0), F0);

        float3 num     = NDF * G * F;
        float  denom   = 4.0 * max(dot(N, V), 0.0) * NdotL;
        float3 specular = num / max(denom, 0.001);
        float3 kD       = (float3(1.0, 1.0, 1.0) - F) * (1.0 - metallic);

        float3 radiance = l.color * l.intensity * attenuation;
        Lo += (kD * albedo / PI + specular) * radiance * NdotL;
    }

    // TODO: Replace with IBL-based ambient for more realistic indirect lighting
    float3 ambient = float3(0.1, 0.1, 0.1) * albedo;
    float3 color   = ambient + Lo;

    // Reinhard tone mapping + gamma correction
    color = color / (color + float3(1.0, 1.0, 1.0));
    color = pow(color, float3(1.0 / 2.2, 1.0 / 2.2, 1.0 / 2.2));

    return float4(color, 1);
}
