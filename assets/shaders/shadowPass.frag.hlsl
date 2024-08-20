#pragma enable_d3d12_debug_symbols
#define NUM_LIGHTS 3


struct LightState
{
    float4 position;
    float4 color;
    float4 falloff;
    float  far_plane;
    float3 padding;

    float4x4 views[6];
    float4x4 projections[6];
};

cbuffer LightConstantBuffer : register(b1, space0)
{
    LightState lights[NUM_LIGHTS];
};


struct PSInput
{

    float4 position : SV_POSITION;
    float3 world_pos : POSITION;
    uint rtIndex : SV_RenderTargetArrayIndex;

};


struct PSOutput
{
    float depth : SV_Depth;    // Output depth
};




PSOutput main(PSInput input)
{
    PSOutput result;

    // get distance between fragment and light source
    float lightDistance = length(input.world_pos.xyz - lights[0].position.xyz);
    
    // map to [0;1] range by dividing by far_plane
    lightDistance = lightDistance / (lights[0].far_plane * 2.f );
    
    // write this as modified depth
    result.depth = lightDistance;

    return result;
}