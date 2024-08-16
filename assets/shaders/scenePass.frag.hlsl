#pragma enable_d3d12_debug_symbols
#define NUM_LIGHTS 3
#define SHADOW_DEPTH_BIAS 0.00005f

struct PSInput
{
    float4 position : SV_POSITION;
    float4 worldpos : POSITION;
    float2 uv : TEXCOORD0;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
};


struct LightState
{
    float4 position;
    float4 direction;
    float4 color;
    float4 falloff;
    float4x4 view;
    float4x4 projection;
};

cbuffer SceneConstantBuffer : register(b0, space0)
{
    float4x4 model;
    float4x4 view;
    float4x4 projection;
    float4 ambient_color;
};

cbuffer LightConstantBuffer : register(b1, space0)
{
    LightState lights[NUM_LIGHTS];
};


// padding rule is like a monster!!!
struct MaterialConstants
{
    float4 colorFactors;
    float4 metalRoughFactors;

    int albedoIndex;
    int albedoSamplerIndex;

    int metalRoughIndex;
    int metalRoughSamplerIndex;

    int normalIndex;
    int normalSamplerIndex;

    int emissiveIndex;
    int emissiveSamplerIndex;

    int occlusionIndex;
    int occlusionSamplerIndex;
    // padding, 
    int padding0;
    int padding1;

};



Texture2D ShadowMap : register(t0, space0);
sampler ShadowMapSampler : register(s0, space0);


cbuffer MaterialIndex : register(b0, space1)
{
    unsigned int materialIndex;
}

StructuredBuffer<MaterialConstants> MaterialStructuredBuffers[] : register(t0, space1);

Texture2D TextureTable[] : register(t0, space2); 
sampler TextureSampler[] : register(s0, space1);

//--------------------------------------------------------------------------------------
// Sample normal map, convert to signed, apply tangent-to-world space transform.
//--------------------------------------------------------------------------------------

float3 CalcPerPixelNormal(float2 vTexcoord, float3 vVertNormal, float3 vVertTangent)
{

    // First, get the MaterialConstants element from the buffer
    StructuredBuffer<MaterialConstants> material_structured_buffer = MaterialStructuredBuffers[materialIndex];

    int normal_index = material_structured_buffer[0].normalIndex;
    int sampler_normal_index = material_structured_buffer[0].normalSamplerIndex;
    
    // Compute tangent frame.
    vVertNormal = normalize(vVertNormal);
    vVertTangent = normalize(vVertTangent);


    float epsilon = 0.0001; // Small threshold to detect degeneracy

    // Check if tangent and normal are nearly the same
    if (abs(dot(vVertTangent, vVertNormal)) > (1 - epsilon))
    {
        // Choose an arbitrary orthogonal vector
        vVertTangent = normalize(abs(vVertNormal.x) > 0.9 ? float3(0, 1, 0) : float3(1, 0, 0));
    }

    // Re-orthogonalize tangent
    vVertTangent = normalize(vVertTangent - dot(vVertTangent, vVertNormal) * vVertNormal);

    float3 vVertBinormal = normalize(cross(vVertTangent, vVertNormal));
    float3x3 mTangentSpaceToWorldSpace = float3x3(vVertTangent, vVertBinormal, vVertNormal);

    // Compute per-pixel normal.
    float3 vBumpNormal = TextureTable[normal_index].Sample(TextureSampler[sampler_normal_index], vTexcoord).xyz;
    //float3 vBumpNormal = (float3)normalMap.Sample(sampleWrap, vTexcoord);
    vBumpNormal = 2.0f * vBumpNormal - 1.0f;

    return mul(vBumpNormal, mTangentSpaceToWorldSpace);
}



////--------------------------------------------------------------------------------------
//// Diffuse lighting calculation, with angle and distance falloff.
////--------------------------------------------------------------------------------------
float4 CalcLightingColor(float3 vLightPos, float3 vLightDir, float4 vLightColor, float4 vFalloffs, float3 vPosWorld, float3 vPerPixelNormal)
{
    float3 vLightToPixelUnNormalized = vPosWorld - vLightPos;

    // Dist falloff = 0 at vFalloffs.x, 1 at vFalloffs.x - vFalloffs.y
    float fDist = length(vLightToPixelUnNormalized);

    float fDistFalloff = saturate((vFalloffs.x - fDist) / vFalloffs.y);

    // Normalize from here on.
    float3 vLightToPixelNormalized = vLightToPixelUnNormalized / fDist;

    // Angle falloff = 0 at vFalloffs.z, 1 at vFalloffs.z - vFalloffs.w
    float fCosAngle = dot(vLightToPixelNormalized, vLightDir / length(vLightDir));
    float fAngleFalloff = saturate((fCosAngle - vFalloffs.z) / vFalloffs.w);

    // Diffuse contribution.
    float fNDotL = saturate(-dot(vLightToPixelNormalized, vPerPixelNormal));

    return vLightColor * fNDotL * fDistFalloff * fAngleFalloff;
}



//--------------------------------------------------------------------------------------
// Test how much pixel is in shadow, using 2x2 percentage-closer filtering.
//--------------------------------------------------------------------------------------
float4 CalcUnshadowedAmountPCF2x2(int lightIndex, float4 vPosWorld)
{
    // Compute pixel position in light space.
    float4 vLightSpacePos = vPosWorld;
    vLightSpacePos = mul(vLightSpacePos, lights[lightIndex].view);
    vLightSpacePos = mul(vLightSpacePos, lights[lightIndex].projection);

    vLightSpacePos.xyz /= vLightSpacePos.w;

    // Translate from homogeneous coords to texture coords.
    float2 vShadowTexCoord = 0.5f * vLightSpacePos.xy + 0.5f;
    vShadowTexCoord.y = 1.0f - vShadowTexCoord.y;

    // Depth bias to avoid pixel self-shadowing.
    float vLightSpaceDepth = vLightSpacePos.z - SHADOW_DEPTH_BIAS;

    float2 vShadowMapDims = float2(1280.0f, 720.0f); // need to keep in sync with .cpp file
    float4 vSubPixelCoords = float4(1.0f, 1.0f, 1.0f, 1.0f);

    // Find sub-pixel weights.
    vSubPixelCoords.xy = frac(vShadowMapDims * vShadowTexCoord);
    vSubPixelCoords.zw = 1.0f - vSubPixelCoords.xy;
    float4 vBilinearWeights = vSubPixelCoords.zxzx * vSubPixelCoords.wwyy;


    // 2x2 percentage closer filtering.
    float2 vTexelUnits = 1.0f / vShadowMapDims;

    float4 vShadowDepths;
    vShadowDepths.x = ShadowMap.Sample(ShadowMapSampler, vShadowTexCoord).x;
    vShadowDepths.y = ShadowMap.Sample(ShadowMapSampler, vShadowTexCoord + float2(vTexelUnits.x, 0.f)).x;
    vShadowDepths.z = ShadowMap.Sample(ShadowMapSampler, vShadowTexCoord + float2(0.f, vTexelUnits.y)).x;
    vShadowDepths.w = ShadowMap.Sample(ShadowMapSampler, vShadowTexCoord + vTexelUnits).x;

    float4 vShadowTests = select((vShadowDepths >= vLightSpaceDepth), 1.f, 0.f);


    return dot(vBilinearWeights, vShadowTests);
}

float4 main(PSInput input) : SV_TARGET
{
    // First, get the MaterialConstants element from the buffer
    StructuredBuffer<MaterialConstants> material_structured_buffer = MaterialStructuredBuffers[materialIndex];

    // Then, access its members like albedoIndex
    const int albedo_index         = material_structured_buffer[0].albedoIndex;
    const int sampler_albedo_index = material_structured_buffer[0].albedoSamplerIndex;

    const float4 albedo = TextureTable[albedo_index].Sample(TextureSampler[sampler_albedo_index], input.uv);

    float3  pixel_normal = CalcPerPixelNormal(input.uv, input.normal, input.tangent);
    float4 total_light = ambient_color;



    //for (int i = 0; i < NUM_LIGHTS; i++)
    for (int i = 0; i < 1; i++)
    {
        float4 light_pass = CalcLightingColor(lights[i].position.xyz, lights[i].direction.xyz, lights[i].color, lights[i].falloff, input.worldpos.xyz, pixel_normal);
        light_pass *= CalcUnshadowedAmountPCF2x2(i, input.worldpos);
        total_light += light_pass;
    }

    return albedo * saturate(total_light);
    

}
