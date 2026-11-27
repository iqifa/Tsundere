#shader compute
#version 430 core

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

#define TS_GBUFFER_READ
#include "include/GBuffer.glsl"

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

layout(std430, binding = 4) readonly buffer UnlitTileList { uint unlitTiles[]; };
layout(rgba16f, binding = 5) writeonly uniform image2D u_LightOutput;
layout(binding = 13) uniform sampler2D u_Depth;

void main()
{
    uint packedTile = unlitTiles[gl_WorkGroupID.x];
    ivec2 pixel = ivec2(packedTile & 0xFFFFu, packedTile >> 16u) * 8 + ivec2(gl_LocalInvocationID.xy);
    ivec2 size = ivec2(u_ViewportSize + 0.5);
    if (pixel.x >= size.x || pixel.y >= size.y)
        return;

    float depth = texelFetch(u_Depth, pixel, 0).r;
    if (depth >= 1.0)
        return;

    GBufferSurface g = ReadGBuffer(pixel);
    imageStore(u_LightOutput, pixel, vec4(g.baseColor + g.emissive, 1.0));
}
