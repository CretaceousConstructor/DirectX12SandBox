#pragma enable_d3d12_debug_symbols
#define NUM_LIGHTS 3
#define SHADOW_DEPTH_BIAS 0.005f

struct PSInput
{
    float4 position : SV_POSITION;
    float4 world_pos : POSITION;
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



TextureCube ShadowMap : register(t0, space0);
SamplerState ShadowMapSampler : register(s0, space0);


cbuffer MaterialIndex : register(b0, space1)
{
    unsigned int materialIndex;
}


Texture2D     TextureTable[] : register(t0, space2); 
SamplerState  TextureSampler[] : register(s0, space1);


float ShadowCalculation(float3 world_pos)
{
    // get vector between fragment position and light position
    float3 light_to_frag= world_pos - lights[0].position.xyz;  

    // now get current linear depth as the length between the fragment and light position
    const float current_depth  = length(light_to_frag);

    light_to_frag =normalize(world_pos - lights[0].position.xyz);  

    float shadow  = 0.0;

    const float samples = 4.0;
    const float offset  = 0.01;
    
    for(float x = -offset; x < offset; x += offset / (samples * 0.5))
    {
        for(float y = -offset; y < offset; y += offset / (samples * 0.5))
        {
            for(float z = -offset; z < offset; z += offset / (samples * 0.5))
            {
                // use the light to fragment vector to sample from the depth map    
                //float closest_depth = ShadowMap.Sample(ShadowMapSampler, frag_to_light); 
                float closest_depth = ShadowMap.Sample(ShadowMapSampler, light_to_frag + float3(x, y, z)); 
                closest_depth *= (lights[0].far_plane * 2.f);   // undo mapping [0;1]
                if((current_depth - SHADOW_DEPTH_BIAS) > closest_depth){
                    shadow += 1.0;
                }
            }
        }
    }

    shadow /= (samples * samples * samples);

    return shadow;
}  




float4 main(PSInput input) : SV_TARGET
{
    const float4 albedo = input.color;
    const float3 pixel_normal = normalize(input.normal);

    // ambient
    const float3 ambient = ambient_color.xyz;

    // diffuse
    const float3 light_dir = normalize(lights[0].position.xyz - input.world_pos.xyz);
    float  diff = max(dot(light_dir, pixel_normal), 0.0);
    float3 diffuse = diff * lights[0].color;
    // specular
    float3 view_dir = normalize(camera_pos.xyz - input.world_pos.xyz);

    float  spec = 0.0;
    float3 halfwayDir = normalize(light_dir + view_dir);  
    spec = pow(max(dot(pixel_normal, halfwayDir), 0.0), 64.0);
    float3 specular = spec * lights[0].color;    
    //falloff

    float3 vLightToPixelUnNormalized = input.world_pos.xyz - lights[0].position.xyz;

    // Dist falloff = 0 at vFalloffs.x, 1 at vFalloffs.x - vFalloffs.y
    float fDist = length(vLightToPixelUnNormalized);
    float fDistFalloff = saturate((lights[0].falloff.x - fDist) / lights[0].falloff.y);

    // calculate shadow
    float shadow = ShadowCalculation(input.world_pos.xyz);                      

    float3 lighting = (ambient + fDistFalloff * (1.0 - shadow) * (diffuse + specular)) * albedo.xyz;    
    lighting = saturate(lighting);

    return float4(lighting, 1.0);

}
