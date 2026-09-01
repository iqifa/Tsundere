#shader vertex
#version 420 core
layout (location = 0) in vec3 aPos;

layout(std140, binding = 0) uniform SkyBoxMatrices
{
    mat4 proj;
    mat4 view;
    mat4 prevViewProj;
};

layout(location = 0) out vec3 TexCoords;
layout(location = 1) out vec4 v_CurrentClipPos;
layout(location = 2) out vec4 v_PreviousClipPos;

void main()
{
    TexCoords = aPos;
    gl_Position = proj * view * vec4(aPos, 1.0);
    v_CurrentClipPos = gl_Position;
    v_PreviousClipPos = prevViewProj * vec4(aPos, 1.0);
}

#shader fragment
#version 420 core
layout(location = 0) out vec4 FragColor;
layout(location = 1) out vec2 MotionVector;

layout(binding = 10) uniform samplerCube skybox;

layout(location = 0) in vec3 TexCoords;
layout(location = 1) in vec4 v_CurrentClipPos;
layout(location = 2) in vec4 v_PreviousClipPos;

void main()
{
    FragColor = texture(skybox, TexCoords);

    vec3 currentNDC = v_CurrentClipPos.xyz / v_CurrentClipPos.w;
    vec3 previousNDC = v_PreviousClipPos.xyz / v_PreviousClipPos.w;
    MotionVector = (currentNDC - previousNDC).xy * 0.5;
}
