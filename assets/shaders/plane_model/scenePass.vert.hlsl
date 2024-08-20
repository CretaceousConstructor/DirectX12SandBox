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
    //float padding2 : PADDING2;

    float4 tangent : TANGENT;
    //float3 tangent : TANGENT;
    //float padding3;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float4 worldpos : POSITION;
    float4 color : COLOR;

    float2 uv : TEXCOORD0;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
};

struct LightState
{
    float4 position;
    float4 color;
    float4 falloff;

    float  far_plane;
    float3 padding;

    float4x4 view[6];
    float4x4 projection[6];
};

cbuffer SceneConstantBuffer : register(b0, space0)
{
    float4x4 model;
    float4x4 view;
    float4x4 projection;
    float4   camera_pos;
    float4   ambient_color;
};

cbuffer LightConstantBuffer : register(b1, space0)
{
    LightState lights[NUM_LIGHTS];
};

cbuffer LocalMatrix : register(b1, space1)
{
    float4x4 localMatrix;
}

VSOutput main(VSInput vs_in)
{
    VSOutput result;

    float4 new_position = float4(vs_in.position.xyz, 1.0f);
    new_position = mul(new_position, localMatrix);
    new_position = mul(new_position, model);

    result.worldpos = new_position;

    new_position = mul(new_position, view);
    new_position = mul(new_position, projection);

    result.position = new_position;
    result.color = vs_in.color;
    result.uv = vs_in.uv.xy;
    result.normal = vs_in.normal.xyz;
    result.tangent = vs_in.tangent.xyz;

    return result;
}


