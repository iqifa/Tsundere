#shader vertex
#version 430 core

layout(location = 0) in vec3 a_Position;

layout(push_constant) uniform LightVolumePush
{
    mat4 u_ModelViewProj;
    int  u_LightIndex;
    int  _pad0;
    int  _pad1;
    int  _pad2;
} pc;

layout(location = 0) flat out int v_LightIndex;

void main()
{
    v_LightIndex = pc.u_LightIndex;
    gl_Position = pc.u_ModelViewProj * vec4(a_Position, 1.0);
}

#shader fragment
#version 430 core

#define TS_GBUFFER_READ
#include "include/DeferredShading.glsl"

layout(location = 0) flat in int v_LightIndex;
layout(location = 0) out vec4 o_Color;

layout(binding = 13) uniform sampler2D u_Depth;

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

layout(std430, binding = 1) readonly buffer LightBuffer
{
    Light lights[];
};

// Local Light Complex PSO: Stencil filters to (bit7=1, Complex=1, Lit=1).
// Full shading-model switch, additive blend.
void main()
{
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    float depth = texelFetch(u_Depth, pixel, 0).r;
    if (depth >= 1.0)
        discard;

    vec2 uv = gl_FragCoord.xy / u_ViewportSize;
    vec3 worldPos = GBufferWorldPosition(uv, depth, u_InvViewProjNoTrans);
    GBufferSurface g = ReadGBuffer(pixel);
    Surface surf = MakePBRSurface(worldPos, g.normal, u_ViewPos.xyz - worldPos, g.baseColor, g.metallic);

    vec3 contrib = EvaluateComplexLight(lights[v_LightIndex], g, surf);
    o_Color = vec4(contrib, 0.0);
}
