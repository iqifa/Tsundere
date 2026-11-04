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

void main()
{
    v_WorldPos = (pc.model * vec4(aPos, 1.0)).xyz;
    vec4 worldPos = pc.model * vec4(aPos, 1.0);
    gl_Position = jitteredViewProj * worldPos;
    v_Normal = (pc.model * vec4(aNormal, 0.0)).xyz;
    v_TexCoords = aTexCoords;
    v_TexCoords.y = 1.0 - v_TexCoords.y;
}

#shader fragment
#version 430 core

layout(location = 0) out vec4 FragColor;

layout(location = 0) in vec2 v_TexCoords;
layout(location = 1) in vec3 v_Normal;
layout(location = 2) in vec3 v_WorldPos;

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
    int   _pad3[3];
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

    vec3 baseColor = u_BaseColor.rgb;
    vec4 texColor = texture(u_BaseColorMap, uv);
    baseColor *= mix(vec3(1.0), texColor.rgb, step(0.001, texColor.a + texColor.r + texColor.g + texColor.b));

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

    // Cheap pseudo-PBR split: diffuse * (1-metallic) + spec-ish by roughness
    vec3 diffuse = baseColor * lightColor.rgb * NdotL * (1.0 - u_Metallic);
    vec3 spec = lightColor.rgb * pow(max(dot(reflect(-L, N), normalize(-v_WorldPos)), 0.0), mix(4.0, 256.0, 1.0 - u_Roughness)) * (1.0 - u_Roughness);
    vec3 emissive = u_EmissiveColor * u_EmissiveStrength;

    FragColor = vec4(diffuse + spec + emissive, u_BaseColor.a);
}
