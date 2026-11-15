#render_pass forward
#shader vertex
#version 430 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoords;

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
layout(location = 2) out vec4 v_CurrentClipPos;
layout(location = 3) out vec4 v_PreviousClipPos;

void main()
{
    vec4 worldPos = pc.model * vec4(aPos, 1.0);
    gl_Position = jitteredViewProj * worldPos;
    v_TexCoords = vec2(aTexCoords.x, 1.0 - aTexCoords.y);
    v_Normal = normalize(mat3(transpose(inverse(pc.model))) * aNormal);
    v_CurrentClipPos = viewProj * worldPos;
    v_PreviousClipPos = prevViewProj * pc.prevModel * vec4(aPos, 1.0);
}

#shader fragment
#version 430 core

layout(location = 0) in vec2 v_TexCoords;
layout(location = 1) in vec3 v_Normal;
layout(location = 2) in vec4 v_CurrentClipPos;
layout(location = 3) in vec4 v_PreviousClipPos;

layout(location = 0) out vec4 FragColor;
layout(location = 1) out vec2 MotionVector;

layout(std140, binding = 1) uniform PerFrame_Geometry
{
    vec4 lightDir;
    vec4 lightColor;
    float ambientStrength;
    float _pad0;
    float _pad1;
    float _pad2;
    vec4 viewPos;
    mat4 u_LightViewProj;
    int hasNormalMap;
    int u_ShadowMapEnabled;
    vec2 u_ShadowMapSize;
    float u_LightSize;
    int _pad3;
    int _pad4;
    int _pad5;
};

layout(std140, binding = 3) uniform MaterialParams
{
    vec4 u_BaseColor;
};

void main()
{
    vec3 N = dot(v_Normal, v_Normal) > 1e-8 ? normalize(v_Normal) : vec3(0.0, 1.0, 0.0);
    vec3 key = normalize(vec3(0.35, 0.85, 0.4));
    vec3 fill = normalize(vec3(-0.45, 0.25, -0.55));
    vec3 color = u_BaseColor.rgb * (0.22 + 0.78 * max(dot(N, key), 0.0)
        + 0.18 * max(dot(N, fill), 0.0));
    FragColor = vec4(color, 1.0);

    vec2 currentNDC = v_CurrentClipPos.xy / v_CurrentClipPos.w;
    vec2 previousNDC = v_PreviousClipPos.xy / v_PreviousClipPos.w;
    MotionVector = (currentNDC - previousNDC) * 0.5;
}
