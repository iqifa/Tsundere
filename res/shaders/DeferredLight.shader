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

layout(binding = 10) uniform sampler2D u_GBufferNormal;
layout(binding = 11) uniform sampler2D u_GBufferAlbedo;
layout(binding = 12) uniform sampler2D u_GBufferSpecular;
layout(binding = 13) uniform sampler2D u_Depth;
// layout(binding = 14) uniform samplerCube u_Skybox;

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

    vec4 clipPos = vec4(v_TexCoord * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 worldPos = u_InvViewProjNoTrans * clipPos;
    worldPos /= worldPos.w;

    vec4 albedo   = texture(u_GBufferAlbedo, v_TexCoord);
    vec4 specData = texture(u_GBufferSpecular, v_TexCoord);

    Surface surf;
    surf.P             = worldPos.xyz;
    surf.N             = normalize(texture(u_GBufferNormal, v_TexCoord).rgb);
    surf.V             = normalize(u_ViewPos.xyz - worldPos.xyz);
    surf.diffuseColor  = albedo.rgb;
    surf.specularColor = specData.rgb;
    surf.shininess     = specData.a * 255.0;

    vec3 result = vec3(0.0);
    int count = int(u_LightCounts + 0.5);
    for (int i = 0; i < count; ++i)
    {
        LightSample ls;
        if (SampleLight(lights[i], surf, ls))
            result += ShadeBRDF(surf, ls);
    }
    result += EvaluateAmbient(surf, u_Ambient.rgb);

    o_Color = vec4(result, 1.0);
}
