#pragma enable_d3d12_debug_symbols
#define NUM_LIGHTS 3


struct VSInput
{
    float4 color : COLOR;

    float4 position : POSITION;
    //float3 position : POSITION;
    //float padding0 : PADDING0;

    float4 normal : NORMAL;
    //float3 normal : NORMAL;
    //float padding1 : PADDING1;

    float4 uv : TEXCOORD;
    //float2 uv : TEXCOORD;
    //float padding2 
    //float padding3 

    float4 tangent : TANGENT;
    //float3 tangent : TANGENT;
    //float padding4;
};

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
    float4   ambientColor;
};

cbuffer LightConstantBuffer : register(b1, space0)
{
    LightState lights[NUM_LIGHTS];
};

cbuffer LocalMatrix : register(b1, space1)
{
    float4x4 localMatrix;
}

PSInput main(VSInput vs_in)
{
    PSInput result;
    float4 new_position = float4(vs_in.position.xyz, 1.0f);
    
    new_position = mul(new_position, localMatrix);
    new_position = mul(new_position, model);

    result.worldpos = new_position;

    new_position = mul(new_position, lights[0].view);
    new_position = mul(new_position, lights[0].projection);

    result.position = new_position;
    result.uv = vs_in.uv.xy;
    result.normal = vs_in.normal.xyz;
    result.tangent = vs_in.tangent.xyz;

    return result;
}


