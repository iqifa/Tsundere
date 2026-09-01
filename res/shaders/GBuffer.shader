#shader vertex
#version 330 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoords;
layout(location = 3) in vec3 aTangent;
layout(location = 4) in vec3 aBitangent;

[System]
uniform mat4 MVP_matrix;
uniform mat4 model;
uniform mat4 prevModel;
uniform mat4 viewProj;
uniform mat4 prevViewProj;
[System]

out vec3 v_FragPos;
out vec2 v_TexCoords;
out vec3 v_Normal;
out vec3 v_Tangent;
out vec3 v_Bitangent;
out vec4 v_CurrentClipPos;
out vec4 v_PreviousClipPos;

void main()
{
    vec4 worldPos = model * vec4(aPos, 1.0);
    v_FragPos = worldPos.xyz;
    v_TexCoords = aTexCoords;
    v_Normal = normalize(mat3(transpose(inverse(model))) * aNormal);
    v_Tangent = normalize(mat3(model) * aTangent);
    v_Bitangent = normalize(mat3(model) * aBitangent);

    gl_Position = MVP_matrix * vec4(aPos, 1.0);
    v_CurrentClipPos = viewProj * worldPos;
    v_PreviousClipPos = prevViewProj * prevModel * vec4(aPos, 1.0);
}

#shader fragment
#version 330 core

layout(location = 0) out vec3 o_Position;
layout(location = 1) out vec3 o_Normal;
layout(location = 2) out vec4 o_Albedo;
layout(location = 3) out vec4 o_Specular;
layout(location = 4) out vec2 o_Velocity;

in vec3 v_FragPos;
in vec2 v_TexCoords;
in vec3 v_Normal;
in vec3 v_Tangent;
in vec3 v_Bitangent;
in vec4 v_CurrentClipPos;
in vec4 v_PreviousClipPos;

uniform int hasNormalMap;

uniform sampler2D texture_diffuse1;
uniform sampler2D texture_specular1;
uniform sampler2D texture_normal1;

const float shininess = 32.0;

void main()
{
    vec3 normal;
    if (hasNormalMap == 1)
    {
        vec3 normalTex = texture(texture_normal1, v_TexCoords).rgb;
        normalTex = normalize(normalTex * 2.0 - 1.0);
        vec3 T = normalize(v_Tangent);
        vec3 B = normalize(v_Bitangent);
        vec3 N = normalize(v_Normal);
        mat3 TBN = mat3(T, B, N);
        normal = normalize(TBN * normalTex);
    }
    else
    {
        normal = normalize(v_Normal);
    }

    o_Position = v_FragPos;
    o_Normal = normal;
    o_Albedo = vec4(texture(texture_diffuse1, v_TexCoords).rgb, 1.0);
    o_Specular = vec4(texture(texture_specular1, v_TexCoords).rgb, shininess);

    vec3 currentNDC = v_CurrentClipPos.xyz / v_CurrentClipPos.w;
    vec3 previousNDC = v_PreviousClipPos.xyz / v_PreviousClipPos.w;
    o_Velocity = (currentNDC - previousNDC).xy * 0.5;
}
