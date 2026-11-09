#shader vertex
#version 430 core

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
layout(location = 2) out vec3 v_WorldPos;
layout(location = 3) out vec4 v_CurrentClipPos;
layout(location = 4) out vec4 v_PreviousClipPos;

void main()
{
    vec4 worldPos = pc.model * vec4(aPos, 1.0);
    v_WorldPos = worldPos.xyz;
    gl_Position = jitteredViewProj * worldPos;
    v_Normal = transpose(inverse(mat3(pc.model))) * aNormal;
    v_TexCoords = aTexCoords;
    v_TexCoords.y = 1.0 - v_TexCoords.y;
    v_CurrentClipPos = viewProj * worldPos;
    v_PreviousClipPos = prevViewProj * pc.prevModel * vec4(aPos, 1.0);
}

#shader fragment
#version 430 core

layout(location = 0) out vec4 FragColor;
layout(location = 1) out vec2 MotionVector;

layout(location = 0) in vec2 v_TexCoords;
layout(location = 1) in vec3 v_Normal;
layout(location = 2) in vec3 v_WorldPos;
layout(location = 3) in vec4 v_CurrentClipPos;
layout(location = 4) in vec4 v_PreviousClipPos;

layout(std140, binding = 1) uniform PerFrame_Geometry
{
    vec4  lightDir;             // vec3 → vec4
    vec4  lightColor;           // vec3 → vec4
    float ambientStrength;
    float _pad0;
    float _pad1;
    float _pad2;
    vec4  viewPos;              // vec3 → vec4
    mat4  u_LightViewProj;
    int   hasNormalMap;
    int   u_ShadowMapEnabled;
    vec2  u_ShadowMapSize;
    float u_LightSize;
    int   _pad3;
    int   _pad4;
    int   _pad5;
};

// ---- Material-editable parameters (shown in the material inspector) ----
// One std140 block so Vulkan accepts it; SPIRV-Cross emits the same block for
// OpenGL and reflection exposes each member to the material editor by name.
layout(std140, binding = 3) uniform MaterialParams
{
    vec4  u_BaseColor;          // color picker (vec4)
    vec3  u_EmissiveColor;      // color picker (vec3)
    float _matpad0;
    vec2  u_Tiling;             // drag2
    vec2  u_Offset;             // drag2
    float u_Roughness;          // drag
    float u_Metallic;           // drag
    float u_EmissiveStrength;   // drag
    int   u_CheckerMode;        // drag int: 0=off 1=uv checker 2=world grid
    int   _matpad1[3];
};

layout(binding = 10) uniform sampler2D u_BaseColorMap;   // Texture2D slot

void main()
{
    vec2 uv = v_TexCoords * u_Tiling + u_Offset;

    vec3 baseColor = u_BaseColor.rgb * texture(u_BaseColorMap, uv).rgb;

    // Optional debug patterns
    if (u_CheckerMode == 1)
    {
        vec2 c = floor(uv * 8.0);
        float checker = mod(c.x + c.y, 2.0);
        baseColor = mix(vec3(0.9, 0.2, 0.8), vec3(0.1, 0.7, 0.9), checker);
    }
    else if (u_CheckerMode == 2)
    {
        vec3 g = floor(v_WorldPos * 2.0);
        float grid = mod(g.x + g.y + g.z, 2.0);
        baseColor = mix(vec3(1.0, 0.6, 0.1), vec3(0.2, 0.3, 1.0), grid);
    }

    vec3 L = normalize(-lightDir.xyz);
    vec3 N = normalize(v_Normal);
    float NdotL = max(dot(N, L), 0.0);

    float roughness = clamp(u_Roughness, 0.0, 1.0);
    float metallic = clamp(u_Metallic, 0.0, 1.0);
    vec3 V = normalize(viewPos.xyz - v_WorldPos);
    vec3 ambient = baseColor * lightColor.rgb * ambientStrength;
    vec3 diffuse = baseColor * lightColor.rgb * NdotL * (1.0 - metallic);
    float highlight = pow(max(dot(reflect(-L, N), V), 0.0), mix(4.0, 256.0, 1.0 - roughness));
    vec3 spec = lightColor.rgb * highlight * NdotL * (1.0 - roughness);
    vec3 emissive = u_EmissiveColor * u_EmissiveStrength;

    FragColor = vec4(ambient + diffuse + spec + emissive, u_BaseColor.a);
    vec2 currentNDC = v_CurrentClipPos.xy / v_CurrentClipPos.w;
    vec2 previousNDC = v_PreviousClipPos.xy / v_PreviousClipPos.w;
    MotionVector = (currentNDC - previousNDC) * 0.5;
}
