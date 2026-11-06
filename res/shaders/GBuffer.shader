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
    float u_Roughness;          // → shininess = exp2(mix(2, 8, 1-r)); 0.5 → legacy 32
    float u_Metallic;           // tints specular toward the base color
    float u_EmissiveStrength;
    int   u_CheckerMode;
    int   _matpad1[3];
};

layout(binding = 10) uniform sampler2D texture_diffuse1;
layout(binding = 11) uniform sampler2D texture_specular1;
layout(binding = 12) uniform sampler2D texture_normal1;
layout(binding = 13) uniform sampler2D u_ShadowMap;

// Roughness→shininess on the same curve the material editor exposes.
// 0.5 (the CreateDefault value) maps to the legacy constant 32.
float ShininessFromRoughness(float roughness)
{
    return exp2(mix(2.0, 8.0, 1.0 - clamp(roughness, 0.0, 1.0)));
}

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

    // RGBA16F stores the signed world normal. Lighting normalizes it on read.
    o_Normal = vec4(normal, 1.0);

    // Material parameters bake into the GBuffer outputs: DeferredLight is a
    // fullscreen pass with no per-draw state, so this is the only place a
    // material can influence lighting.
    vec3 baseColor = u_BaseColor.rgb;
    vec4 texColor = texture(texture_diffuse1, uv);
    vec3 albedo = texColor.rgb * baseColor;
    vec3 specular = texture(texture_specular1, uv).rgb * mix(vec3(1.0), baseColor, u_Metallic);

    o_Albedo = vec4(albedo, 1.0);
    // RGBA8 alpha holds shininess/255. DeferredLight reconstructs it as alpha * 255.
    o_Specular = vec4(specular, ShininessFromRoughness(u_Roughness) / 255.0);

    vec3 currentNDC = v_CurrentClipPos.xyz / v_CurrentClipPos.w;
    vec3 previousNDC = v_PreviousClipPos.xyz / v_PreviousClipPos.w;
    o_Velocity = (currentNDC - previousNDC).xy * 0.5;
}
