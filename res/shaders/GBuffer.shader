#shader vertex
#version 420 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoords;
layout(location = 3) in vec3 aTangent;
layout(location = 4) in vec3 aBitangent;

// OpenGL: SPIRV-Cross lowers this block to `uniform DrawPush pc`.
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

layout(location = 0) out vec2 v_TexCoords;
layout(location = 1) out vec3 v_Normal;
layout(location = 2) out vec3 v_Tangent;
layout(location = 3) out vec3 v_Bitangent;
layout(location = 4) out vec4 v_CurrentClipPos;
layout(location = 5) out vec4 v_PreviousClipPos;

void main()
{
    vec4 worldPos = pc.model * vec4(aPos, 1.0);
    v_TexCoords = aTexCoords;
    v_Normal = normalize(mat3(transpose(inverse(pc.model))) * aNormal);
    v_Tangent = normalize(mat3(pc.model) * aTangent);
    v_Bitangent = normalize(mat3(pc.model) * aBitangent);

    gl_Position = jitteredViewProj * worldPos;
    v_CurrentClipPos = viewProj * worldPos;
    v_PreviousClipPos = prevViewProj * pc.prevModel * vec4(aPos, 1.0);
}

#shader fragment
#version 420 core

layout(location = 0) out vec4 o_Normal;
layout(location = 1) out vec4 o_Albedo;
layout(location = 2) out vec4 o_Specular;
layout(location = 3) out vec2 o_Velocity;

layout(location = 0) in vec2 v_TexCoords;
layout(location = 1) in vec3 v_Normal;
layout(location = 2) in vec3 v_Tangent;
layout(location = 3) in vec3 v_Bitangent;
layout(location = 4) in vec4 v_CurrentClipPos;
layout(location = 5) in vec4 v_PreviousClipPos;

layout(std140, binding = 1) uniform PerFrame_Gbuffer
{
   int hasNormalMap;
   int _pad0[3];
};

layout(binding = 10) uniform sampler2D texture_diffuse1;
layout(binding = 11) uniform sampler2D texture_specular1;
layout(binding = 12) uniform sampler2D texture_normal1;
layout(binding = 13) uniform sampler2D u_ShadowMap;

const float shininess = 32.0;

void main()
{
    // hasNormalMap == 0: interpolated vertex normal, already in world space.
    vec3 normal = normalize(v_Normal);
    if (hasNormalMap == 1)
    {
        vec3 normalTex = texture(texture_normal1, v_TexCoords).rgb;
        normalTex = normalize(normalTex * 2.0 - 1.0);
        vec3 T = normalize(v_Tangent);
        vec3 B = normalize(v_Bitangent);
        vec3 N = normal;
        mat3 TBN = mat3(T, B, N);
        normal = normalize(TBN * normalTex);
    }

    // RGBA16F stores the signed world normal. Lighting normalizes it on read.
    o_Normal = vec4(normal, 1.0);
    o_Albedo = vec4(texture(texture_diffuse1, v_TexCoords).rgb, 1.0);
    // RGBA8 alpha holds shininess/255. DeferredLight reconstructs it as alpha * 255.
    o_Specular = vec4(texture(texture_specular1, v_TexCoords).rgb, shininess / 255.0);

    vec3 currentNDC = v_CurrentClipPos.xyz / v_CurrentClipPos.w;
    vec3 previousNDC = v_PreviousClipPos.xyz / v_PreviousClipPos.w;
    o_Velocity = (currentNDC - previousNDC).xy * 0.5;
}
