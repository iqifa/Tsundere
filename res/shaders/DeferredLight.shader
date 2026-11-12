#shader vertex
#version 430 core

layout(location = 0) in vec2 a_Position;
layout(location = 1) in vec2 a_TexCoord;

layout(location = 0) out vec2 v_TexCoord;

void main()
{
    v_TexCoord = a_TexCoord;
    gl_Position = vec4(a_Position, 0.0, 1.0);
}

#shader fragment
#version 430 core

#include "include/Lighting.glsl"

layout(location = 0) in vec2 v_TexCoord;

layout(location = 0) out vec4 o_Color;

#define TS_GBUFFER_READ
#include "include/GBuffer.glsl"
#include "include/DeferredPBR.glsl"
layout(binding = 13) uniform sampler2D u_Depth;

layout(std140, binding = 0) uniform PerPass_DeferredLighting
{
    mat4  u_InvViewProjNoTrans;     // offset 0
    mat4  u_View;                   // offset 64
    vec4  u_ViewPos;                // offset 128
    vec2  u_ViewportSize;           // offset 144
    vec2  u_ShadowMapSize;          // offset 152
    float u_Near;                   // offset 160
    float u_Far;                    // offset 164
    float u_LightCounts;            // offset 168
    vec4  u_Ambient;                // offset 176 (rgb = ambient radiance)
};

layout(std430, binding = 1) readonly buffer LightBuffer
{
    Light lights[];
};

void main(){
    float depth = texture(u_Depth, v_TexCoord).r;
    if (depth >= 1.0)
        discard;

    vec3 worldPos = GBufferWorldPosition(v_TexCoord, depth, u_InvViewProjNoTrans);

    GBufferSurface g = ReadGBuffer(ivec2(gl_FragCoord.xy));
    if (g.shadingModel == TS_UNLIT) {
        o_Color = vec4(g.baseColor + g.emissive, 1.0);
        return;
    }
    Surface surf;
    surf.P = worldPos;
    surf.N = g.normal;
    surf.V = normalize(u_ViewPos.xyz - worldPos);
    surf.diffuseColor = g.baseColor * (1.0 - g.metallic);
    surf.specularColor = mix(vec3(0.04), g.baseColor, g.metallic);
    surf.shininess = 1.0;
    vec3 result = g.emissive + EvaluateAmbient(surf, u_Ambient.rgb) * g.ao;
    int count = min(int(u_LightCounts + 0.5), lights.length());
    for (int i = 0; i < count; ++i) {
        LightSample ls;
        if (SampleLight(lights[i], surf, ls))
            result += ShadeMetallicRoughness(surf, ls, g.roughness);
    }
    o_Color = vec4(result, 1.0);
}
