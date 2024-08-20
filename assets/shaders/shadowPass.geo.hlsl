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

struct GSInput
{
    float3 world_pos : POSITION;
};



struct GSOutput
{
    float4 position : SV_POSITION;
    float3 world_pos : POSITION;
    uint rt_index : SV_RenderTargetArrayIndex;
};


// Geometry shader that outputs to 6 cubemap faces
[maxvertexcount(18)] // Output a maximum of 18 vertices (3 per cubemap face)
void main(triangle GSInput input[3], inout TriangleStream<GSOutput> tri_stream)
{
    // Output the vertex to each of the 6 cubemap faces
    for (int face_index = 0; face_index < 6; ++face_index)
    {
        // Output the transformed vertices for the current face
        GSOutput output[3];
        for (int i = 0; i < 3; i++) {
            output[i].position = mul( float4(input[i].world_pos, 1.0f), lights[0].views[face_index]);
            output[i].position = mul( output[i].position, lights[0].projections[face_index]);
            output[i].world_pos =  input[i].world_pos;
            output[i].rt_index  = face_index;
        } 

        tri_stream.Append(output[0]);
        tri_stream.Append(output[1]);
        tri_stream.Append(output[2]);
        tri_stream.RestartStrip();
    }
}


