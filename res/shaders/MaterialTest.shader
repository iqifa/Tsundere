#shader vertex
#version 430 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoords;
layout(location = 3) in vec3 aTangent;
layout(location = 4) in vec3 aBitangent;

[System]
uniform mat4 MVP_matrix;
uniform mat4 model;
[System]
out vec2 TexCoords;
out vec3 Normal;
out vec3 WorldPos;

void main()
{
    vec4 world = model * vec4(aPos, 1.0);
    gl_Position = MVP_matrix * vec4(aPos, 1.0);
    Normal = (model * vec4(aNormal, 0.0)).xyz;
    TexCoords = aTexCoords;
    TexCoords.y = 1.0 - TexCoords.y;
    WorldPos = world.xyz;
}

#shader fragment
#version 430 core

layout(location = 0) out vec4 FragColor;

[System]
uniform vec3 lightDir;
uniform vec3 lightColor;
[System]
in vec3 Normal;
in vec2 TexCoords;
in vec3 WorldPos;

// ---- Material-editable parameters (shown in the material inspector) ----
uniform sampler2D u_BaseColorMap;   // Texture2D slot
uniform vec4  u_BaseColor;          // color picker (vec4)
uniform vec3  u_EmissiveColor;      // color picker (vec3)
uniform vec2  u_Tiling;             // drag2
uniform vec2  u_Offset;             // drag2
uniform float u_Roughness;          // drag
uniform float u_Metallic;           // drag
uniform float u_EmissiveStrength;   // drag
uniform int   u_CheckerMode;        // drag int: 0=off 1=uv checker 2=world grid

void main()
{
    vec2 uv = TexCoords * u_Tiling + u_Offset;

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
        vec3 g = floor(WorldPos * 2.0);
        float grid = mod(g.x + g.y + g.z, 2.0);
        baseColor = mix(vec3(1.0, 0.6, 0.1), vec3(0.2, 0.3, 1.0), grid);
    }

    vec3 L = normalize(-lightDir);
    vec3 N = normalize(Normal);
    float NdotL = max(dot(N, L), 0.0);

    // Cheap pseudo-PBR split: diffuse * (1-metallic) + spec-ish by roughness
    vec3 diffuse = baseColor * lightColor * NdotL * (1.0 - u_Metallic);
    vec3 spec = lightColor * pow(max(dot(reflect(-L, N), normalize(-WorldPos)), 0.0), mix(4.0, 256.0, 1.0 - u_Roughness)) * (1.0 - u_Roughness);
    vec3 emissive = u_EmissiveColor * u_EmissiveStrength;

    FragColor = vec4(diffuse + spec + emissive, u_BaseColor.a);
}
