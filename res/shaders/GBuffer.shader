#render_pass gbuffer
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

#define TS_GBUFFER_WRITE
#include "include/GBuffer.glsl"

layout(location = 0) in vec2 v_TexCoords;
layout(location = 1) in vec3 v_Normal;
layout(location = 2) in vec3 v_Tangent;
layout(location = 3) in vec3 v_Bitangent;
layout(location = 4) in vec4 v_CurrentClipPos;
layout(location = 5) in vec4 v_PreviousClipPos;

layout(std140, binding = 1) uniform PerFrame_Gbuffer
{
   int hasNormalMap;
   int _pad0;
   int _pad1;
   int _pad2;
};

// Material parameters baked into the GBuffer outputs. Same std140 layout as
// MaterialTest.shader's block, so Material::PackMaterialBlock packs both.
// Engine blocks are bindings 0/1/2; material blocks start at 3.
layout(std140, binding = 3) uniform MaterialParams
{
    vec4  u_BaseColor;          // multiplies the diffuse texture
    vec3  u_EmissiveColor;
    float _matpad0;
    vec2  u_Tiling;             // uv = v_TexCoords * u_Tiling + u_Offset
    vec2  u_Offset;
    float u_Roughness;          // perceptual roughness [0,1]
    float u_Metallic;           // metal fraction [0,1]
    float u_EmissiveStrength;
    int   u_CheckerMode;
    int   _matpad1[3];
};

layout(binding = 10) uniform sampler2D texture_diffuse1;
// Legacy specular is ignored; it does not encode metallic.
layout(binding = 12) uniform sampler2D texture_normal1;
layout(binding = 13) uniform sampler2D u_ShadowMap;

void main()
{
    vec2 uv = v_TexCoords * u_Tiling + u_Offset;

    // hasNormalMap == 0: interpolated vertex normal, already in world space.
    vec3 normal = normalize(v_Normal);
    if (hasNormalMap == 1)
    {
        vec3 normalTex = texture(texture_normal1, uv).rgb;
        normalTex = normalize(normalTex * 2.0 - 1.0);
        vec3 T = normalize(v_Tangent);
        vec3 B = normalize(v_Bitangent);
        vec3 N = normal;
        mat3 TBN = mat3(T, B, N);
        normal = normalize(TBN * normalTex);
    }

    GBufferSurface surface = DefaultGBufferSurface();
    surface.normal = normal;
    surface.shadingModel = TS_SHADING_MODEL;
    // Legacy imported diffuse textures contain sRGB bytes in UNORM storage.
    vec3 encoded = texture(texture_diffuse1, uv).rgb;
    vec3 linearColor = mix(encoded / 12.92, pow((encoded + 0.055) / 1.055, vec3(2.4)),
                           step(vec3(0.04045), encoded));
    surface.baseColor = linearColor * u_BaseColor.rgb;
    surface.metallic = u_Metallic;
    surface.roughness = u_Roughness;
    surface.emissive = u_EmissiveColor * u_EmissiveStrength;
    surface.velocity = (v_CurrentClipPos.xy / v_CurrentClipPos.w
                      - v_PreviousClipPos.xy / v_PreviousClipPos.w) * 0.5;
    WriteGBuffer(surface);
}
