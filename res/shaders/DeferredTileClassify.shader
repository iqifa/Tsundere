#shader compute
#version 430 core

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

#include "include/GBuffer.glsl"

layout(std140, binding = 0) uniform PerPass_TileClassify
{
    uvec2 u_ViewportSize;
    uvec2 u_TileCount;
};

// Indirect dispatch arguments (each uvec4.xyz is a DispatchIndirectCommand at offsets 0, 16, 32).
layout(std430, binding = 1) buffer TileIndirectArgs
{
    uvec4 simpleArgs;   // offset 0:  { simpleTileCount, 1, 1, 0 }
    uvec4 complexArgs;  // offset 16: { complexTileCount, 1, 1, 0 }
    uvec4 unlitArgs;    // offset 32: { unlitTileCount, 1, 1, 0 }
    uvec4 stats;        // offset 48: { skippedSkyTiles, totalTiles, 0, 0 }
};

layout(std430, binding = 2) writeonly buffer SimpleTileList  { uint simpleTiles[];  };
layout(std430, binding = 3) writeonly buffer ComplexTileList { uint complexTiles[]; };
layout(std430, binding = 4) writeonly buffer UnlitTileList   { uint unlitTiles[];   };

layout(rgba16f, binding = 5) writeonly uniform image2D u_LightOutput;

layout(binding = 10) uniform sampler2D u_GBufferNormal;
layout(binding = 13) uniform sampler2D u_Depth;
layout(binding = 14) uniform sampler2D u_GBufferEmissive;

shared uint s_TileFlags;

// Bit 0: has Simple Lit pixel
// Bit 1: has Complex Lit pixel
// Bit 2: has Unlit pixel
void main()
{
    if (gl_LocalInvocationIndex == 0u)
        s_TileFlags = 0u;
    barrier();

    uvec2 pixel = gl_GlobalInvocationID.xy;
    if (pixel.x < u_ViewportSize.x && pixel.y < u_ViewportSize.y)
    {
        float depth = texelFetch(u_Depth, ivec2(pixel), 0).r;
        if (depth >= 1.0)
        {
            // Initialize sky background to transparent black once here.
            imageStore(u_LightOutput, ivec2(pixel), vec4(0.0));
        }
        else
        {
            int shadingModel = int(round(texelFetch(u_GBufferNormal, ivec2(pixel), 0).w));
            int complexity   = int(round(texelFetch(u_GBufferEmissive, ivec2(pixel), 0).a));
            if (shadingModel == TS_UNLIT)
            {
                atomicOr(s_TileFlags, 4u);
            }
            else if (complexity >= TS_COMPLEXITY_COMPLEX || shadingModel >= TS_CLEAR_COAT)
            {
                atomicOr(s_TileFlags, 2u);
            }
            else
            {
                atomicOr(s_TileFlags, 1u);
            }
        }
    }

    barrier();

    if (gl_LocalInvocationIndex == 0u)
    {
        uvec2 tileCoord = gl_WorkGroupID.xy;
        if (tileCoord.x < u_TileCount.x && tileCoord.y < u_TileCount.y)
        {
            uint packedTile = (tileCoord.x & 0xFFFFu) | ((tileCoord.y & 0xFFFFu) << 16u);
            if ((s_TileFlags & 2u) != 0u)
            {
                uint idx = atomicAdd(complexArgs.x, 1u);
                complexTiles[idx] = packedTile;
            }
            else if ((s_TileFlags & 1u) != 0u)
            {
                uint idx = atomicAdd(simpleArgs.x, 1u);
                simpleTiles[idx] = packedTile;
            }
            else if ((s_TileFlags & 4u) != 0u)
            {
                uint idx = atomicAdd(unlitArgs.x, 1u);
                unlitTiles[idx] = packedTile;
            }
            else
            {
                atomicAdd(stats.x, 1u);
            }
        }
    }
}
