#render_pass forward
#render_queue transparent
#shader vertex
#version 430 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;

layout(push_constant) uniform DrawPush
{
    mat4 model;
    mat4 prevModel;
} pc;

layout(std140, binding = 2) uniform PerFrame_Camera
{
    mat4 jitteredViewProj;
    mat4 viewProj;
    mat4 prevViewProj;
};

layout(location = 0) out vec3 v_Normal;

void main()
{
    gl_Position = jitteredViewProj * pc.model * vec4(aPos, 1.0);
    v_Normal = mat3(transpose(inverse(pc.model))) * aNormal;
}

#shader fragment
#version 430 core

#include "include/SceneTextures.glsl"

layout(location = 0) in vec3 v_Normal;
layout(location = 0) out vec4 FragColor;

layout(std140, binding = 1) uniform PerFrame_Geometry
{
    vec4 lightDir;
    vec4 lightColor;
    float ambientStrength;
    vec4 viewPos;
    mat4 u_LightViewProj;
    int hasNormalMap;
    int u_ShadowMapEnabled;
    vec2 u_ShadowMapSize;
    float u_LightSize;
    int _pad1;
    int _pad2;
    int _pad3;
};

layout(std140, binding = 3) uniform PerMaterial
{
    vec4 u_Tint;
};

void main()
{
    vec2 uv = SceneTextureUV();
    vec2 refractedUV = uv + normalize(v_Normal).xy * 0.015;
    if (SampleOpaqueDepth(refractedUV) < gl_FragCoord.z)
        refractedUV = uv;
    vec3 background = SampleOpaqueColor(refractedUV).rgb;
    vec3 tint = mix(u_Tint.rgb, vec3(0.3, 0.8, 1.0), 0.5);
    FragColor = vec4(background * tint + 0.05 * lightColor.rgb, 0.5 * u_Tint.a);
}
