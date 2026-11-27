#shader compute
#version 430 core

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

#define TS_GBUFFER_READ
#include "include/DeferredShading.glsl"

layout(std140, binding = 0) uniform PerPass_DeferredLighting
{
    mat4  u_InvViewProjNoTrans;
    mat4  u_View;
    vec4  u_ViewPos;
    vec2  u_ViewportSize;
    vec2  u_ShadowMapSize;
    float u_Near;
    float u_Far;
    float u_LightCounts;
    vec4  u_Ambient;
};

layout(std430, binding = 1) readonly buffer LightBuffer { Light lights[]; };
layout(std430, binding = 2) readonly buffer SimpleTileList { uint simpleTiles[]; };
layout(rgba16f, binding = 5) writeonly uniform image2D u_LightOutput;
layout(binding = 13) uniform sampler2D u_Depth;

// UE-style Simple Tile Shader: guaranteed zero Complex pixels in this 8x8 tile.
// No shadingModel switch, minimal VGPR footprint.
void main()
{
    uint packedTile = simpleTiles[gl_WorkGroupID.x];
    ivec2 pixel = ivec2(packedTile & 0xFFFFu, packedTile >> 16u) * 8 + ivec2(gl_LocalInvocationID.xy);
    ivec2 size = ivec2(u_ViewportSize + 0.5);
    if (pixel.x >= size.x || pixel.y >= size.y)
        return;

    float depth = texelFetch(u_Depth, pixel, 0).r;
    if (depth >= 1.0)
        return;

    GBufferSurface g = ReadGBuffer(pixel);
    if (g.shadingModel == TS_UNLIT)
    {
        imageStore(u_LightOutput, pixel, vec4(g.baseColor + g.emissive, 1.0));
        return;
    }

    vec2 uv = (vec2(pixel) + 0.5) / u_ViewportSize;
    vec3 worldPos = GBufferWorldPosition(uv, depth, u_InvViewProjNoTrans);
    Surface surf = MakePBRSurface(worldPos, g.normal, u_ViewPos.xyz - worldPos, g.baseColor, g.metallic);

    vec3 result = g.emissive + EvaluateAmbient(surf, u_Ambient.rgb) * g.ao;
    int count = min(int(u_LightCounts + 0.5), lights.length());
    for (int i = 0; i < count; ++i)
    {
        result += EvaluateSimpleLight(lights[i], surf, g.roughness);
    }
    imageStore(u_LightOutput, pixel, vec4(result, 1.0));
}
