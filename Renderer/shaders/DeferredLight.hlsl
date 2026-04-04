#include "PBRCommon.hlsli"
#include "LightDef.hlsli"

// b0: lightweight per-frame camera constants (8 DWORD total)
cbuffer LightPassConstants : register(b0)
{
    float3 cameraPos;
    float  _pad0;
    float3 mainLightDir; // kept for backward-compat; real lights come from LightBuffer b1
    float  _pad1;
};

// b1: LightBuffer  (defined in LightDef.hlsli)
// b2: ReconstructConstants
cbuffer ReconstructConstants : register(b2)
{
    matrix invViewProj;
};

struct VSOutput
{
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD;
};

VSOutput VSMain(uint vertexID : SV_VertexID)
{
    VSOutput output;
    output.uv  = float2((vertexID << 1) & 2, vertexID & 2);
    output.pos = float4(output.uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return output;
}

Texture2D        g_AlbedoRough : register(t0);
Texture2D        g_NormalMetal : register(t1);
Texture2D<float> g_Depth       : register(t2);
SamplerState     g_sampler     : register(s0);

float3 ReconstructWorldPos(float2 uv, float depth)
{
    float2 ndc  = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    float4 clip = float4(ndc, depth, 1.0);
    float4 world = mul(clip, invViewProj);
    return world.xyz / world.w;
}

float4 PSMain(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    float4 albedoRough = g_AlbedoRough.Sample(g_sampler, uv);
    float4 normalMetal = g_NormalMetal.Sample(g_sampler, uv);

    if (length(normalMetal.rgb) < 0.1f)
        return float4(0.2f, 0.2f, 0.2f, 1.0f);

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
        {
            LDir = normalize(-l.direction);
        }
        else if (l.type == 1)
        {
            LDir        = normalize(l.position - P);
            float dist  = length(l.position - P);
            attenuation = 1.0 / (dist * dist + 0.0001);
        }
        else if (l.type == 2)
        {
            LDir             = normalize(l.position - P);
            float dist       = length(l.position - P);
            attenuation      = 1.0 / (dist * dist + 0.0001);
            float theta      = dot(LDir, normalize(-l.direction));
            float outerCos   = cos(l.coneAngle + 0.1745);
            float innerCos   = cos(l.coneAngle);
            float epsilon    = innerCos - outerCos;
            float spot       = clamp((theta - outerCos) / epsilon, 0.0, 1.0);
            attenuation     *= spot;
        }

        float3 H     = normalize(V + LDir);
        float  NdotL = max(dot(N, LDir), 0.0);
        float  NDF   = DistributionGGX(N, H, roughness);
        float  G     = GeometrySmith(N, V, LDir, roughness);
        float3 F     = FresnelSchlick(max(dot(H, V), 0.0), F0);

        float3 num      = NDF * G * F;
        float  denom    = 4.0 * max(dot(N, V), 0.0) * NdotL;
        float3 specular = num / max(denom, 0.001);
        float3 kD       = (float3(1.0, 1.0, 1.0) - F) * (1.0 - metallic);

        float3 radiance = l.color * l.intensity * attenuation;
        Lo += (kD * albedo / PI + specular) * radiance * NdotL;
    }

    // TODO: Replace with IBL-based ambient
    float3 ambient = float3(0.1, 0.1, 0.1) * albedo;
    float3 color   = ambient + Lo;

    color = color / (color + float3(1.0, 1.0, 1.0));
    color = pow(color, float3(1.0 / 2.2, 1.0 / 2.2, 1.0 / 2.2));

    return float4(color, 1);
}
